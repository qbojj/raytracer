#include <embree4/rtcore.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

struct Vec3fa {
    float x, y, z, w;
};

struct Triangle {
    uint32_t v0, v1, v2;
};

struct ObjMesh {
    std::vector<glm::vec3> positions;
    std::vector<Triangle> triangles;
};

struct Camera {
    glm::vec3 eye{0.0f, 1.0f, 3.0f};
    glm::vec3 target{0.0f, 0.7f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float vfovDeg = 55.0f;
};

struct RenderOptions {
    int width = 1280;
    int height = 720;
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.7f, 1.0f, 0.4f));
    glm::vec3 background{0.65f, 0.8f, 1.0f};
    bool shadows = true;
    bool reflections = false;
};

struct Args {
    std::string inputPath;
    std::string outputPath;
    Camera camera;
    RenderOptions options;
};

static void usage() {
    std::cout
        << "Usage:\n"
        << "  ./raytracer <input.obj> <output.jpg> [options]\n\n"
        << "Options:\n"
        << "  --width <int>               Image width (default 1280)\n"
        << "  --height <int>              Image height (default 720)\n"
        << "  --fov <float>               Vertical FOV in degrees (default 55)\n"
        << "  --eye <x y z>               Camera position\n"
        << "  --target <x y z>            Camera look-at point\n"
        << "  --up <x y z>                Camera up vector\n"
        << "  --lightdir <x y z>          Direction from surface toward light\n"
        << "  --background <r g b>        Background color in [0,1]\n"
        << "  --shadows <0|1>             Enable one shadow ray (default 1)\n"
        << "  --reflections <0|1>         Enable one reflection ray (default 0)\n"
        << "  --help                      Show this help\n";
}

static int parseObjIndex(const std::string &token, int count) {
    if (token.empty()) {
        return -1;
    }
    const int idx = std::stoi(token);
    if (idx > 0) {
        return idx - 1;
    }
    if (idx < 0) {
        return count + idx;
    }
    return -1;
}

static bool parseFaceVertex(const std::string &token, int vCount, int &vIdx) {
    size_t slashPos = token.find('/');
    const std::string vPart = (slashPos == std::string::npos) ? token : token.substr(0, slashPos);
    vIdx = parseObjIndex(vPart, vCount);
    return vIdx >= 0 && vIdx < vCount;
}

static ObjMesh loadObj(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open OBJ file: " + path);
    }

    ObjMesh mesh;
    std::string line;
    int lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "v") {
            glm::vec3 v;
            if (!(iss >> v.x >> v.y >> v.z)) {
                throw std::runtime_error("Invalid vertex at line " + std::to_string(lineNo));
            }
            mesh.positions.push_back(v);
        } else if (tag == "f") {
            std::vector<int> faceIndices;
            std::string token;
            while (iss >> token) {
                int vIdx = -1;
                if (!parseFaceVertex(token, static_cast<int>(mesh.positions.size()), vIdx)) {
                    throw std::runtime_error("Invalid face vertex index at line " + std::to_string(lineNo));
                }
                faceIndices.push_back(vIdx);
            }

            if (faceIndices.size() < 3) {
                throw std::runtime_error("Face with <3 vertices at line " + std::to_string(lineNo));
            }

            for (size_t i = 1; i + 1 < faceIndices.size(); ++i) {
                Triangle tri{};
                tri.v0 = static_cast<uint32_t>(faceIndices[0]);
                tri.v1 = static_cast<uint32_t>(faceIndices[i]);
                tri.v2 = static_cast<uint32_t>(faceIndices[i + 1]);
                mesh.triangles.push_back(tri);
            }
        }
    }

    if (mesh.positions.empty() || mesh.triangles.empty()) {
        throw std::runtime_error("OBJ contains no renderable geometry: " + path);
    }

    return mesh;
}

static glm::vec3 sky(const glm::vec3 &dir, const glm::vec3 &bg) {
    const float t = 0.5f * (dir.y + 1.0f);
    const glm::vec3 zenith = bg;
    const glm::vec3 horizon = glm::vec3(0.95f, 0.97f, 1.0f);
    return glm::mix(horizon, zenith, glm::clamp(t, 0.0f, 1.0f));
}

static bool intersectScene(RTCScene scene, const glm::vec3 &origin, const glm::vec3 &dir, float tMin, float tMax,
                           RTCRayHit &rayhit) {
    RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);

    rayhit.ray.org_x = origin.x;
    rayhit.ray.org_y = origin.y;
    rayhit.ray.org_z = origin.z;
    rayhit.ray.tnear = tMin;
    rayhit.ray.dir_x = dir.x;
    rayhit.ray.dir_y = dir.y;
    rayhit.ray.dir_z = dir.z;
    rayhit.ray.time = 0.0f;
    rayhit.ray.tfar = tMax;
    rayhit.ray.mask = 0xFFFFFFFF;
    rayhit.ray.id = 0;
    rayhit.ray.flags = 0;

    rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

    rtcIntersect1(scene, &rayhit, &args);
    return rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID;
}

static bool occludedScene(RTCScene scene, const glm::vec3 &origin, const glm::vec3 &dir, float tMin, float tMax) {
    RTCOccludedArguments args;
    rtcInitOccludedArguments(&args);

    RTCRay shadowRay{};
    shadowRay.org_x = origin.x;
    shadowRay.org_y = origin.y;
    shadowRay.org_z = origin.z;
    shadowRay.tnear = tMin;
    shadowRay.dir_x = dir.x;
    shadowRay.dir_y = dir.y;
    shadowRay.dir_z = dir.z;
    shadowRay.time = 0.0f;
    shadowRay.tfar = tMax;
    shadowRay.mask = 0xFFFFFFFF;
    shadowRay.id = 0;
    shadowRay.flags = 0;

    rtcOccluded1(scene, &shadowRay, &args);
    return shadowRay.tfar < 0.0f;
}

static glm::vec3 shadeHit(RTCScene scene, const RTCRayHit &hit, const glm::vec3 &rayDir, const RenderOptions &opt) {
    const glm::vec3 p(hit.ray.org_x + hit.ray.tfar * hit.ray.dir_x,
                      hit.ray.org_y + hit.ray.tfar * hit.ray.dir_y,
                      hit.ray.org_z + hit.ray.tfar * hit.ray.dir_z);

    glm::vec3 n(hit.hit.Ng_x, hit.hit.Ng_y, hit.hit.Ng_z);
    if (glm::dot(n, n) < 1e-12f) {
        n = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    n = glm::normalize(n);
    if (glm::dot(n, rayDir) > 0.0f) {
        n = -n;
    }

    const glm::vec3 baseColor(0.78f, 0.78f, 0.8f);
    const glm::vec3 ambient = 0.12f * baseColor;

    float vis = 1.0f;
    if (opt.shadows) {
        const glm::vec3 origin = p + n * 1e-4f;
        if (occludedScene(scene, origin, opt.lightDir, 1e-4f, std::numeric_limits<float>::infinity())) {
            vis = 0.0f;
        }
    }

    const float ndotl = std::max(glm::dot(n, opt.lightDir), 0.0f);
    glm::vec3 color = ambient + vis * ndotl * baseColor;

    if (opt.reflections) {
        const glm::vec3 reflDir = glm::reflect(rayDir, n);
        RTCRayHit reflHit{};
        const glm::vec3 reflOrigin = p + n * 1e-4f;
        if (intersectScene(scene, reflOrigin, reflDir, 1e-4f, std::numeric_limits<float>::infinity(), reflHit)) {
            glm::vec3 rn(reflHit.hit.Ng_x, reflHit.hit.Ng_y, reflHit.hit.Ng_z);
            if (glm::dot(rn, rn) > 1e-12f) {
                rn = glm::normalize(rn);
            } else {
                rn = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            if (glm::dot(rn, reflDir) > 0.0f) {
                rn = -rn;
            }

            const float rdiff = std::max(glm::dot(rn, opt.lightDir), 0.0f);
            const glm::vec3 reflCol = glm::vec3(0.6f, 0.65f, 0.7f) * (0.1f + 0.9f * rdiff);
            color = glm::mix(color, reflCol, 0.35f);
        } else {
            color = glm::mix(color, sky(reflDir, opt.background), 0.35f);
        }
    }

    return color;
}

static Args parseArgs(int argc, char **argv) {
    if (argc < 3) {
        usage();
        throw std::runtime_error("Not enough arguments");
    }

    Args args;
    args.inputPath = argv[1];
    args.outputPath = argv[2];

    auto requireFloat = [&](int &i) {
        if (i + 1 >= argc) {
            throw std::runtime_error("Missing float value for " + std::string(argv[i]));
        }
        ++i;
        return std::stof(argv[i]);
    };

    auto requireInt = [&](int &i) {
        if (i + 1 >= argc) {
            throw std::runtime_error("Missing integer value for " + std::string(argv[i]));
        }
        ++i;
        return std::stoi(argv[i]);
    };

    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];

        if (a == "--help") {
            usage();
            std::exit(0);
        } else if (a == "--width") {
            args.options.width = requireInt(i);
        } else if (a == "--height") {
            args.options.height = requireInt(i);
        } else if (a == "--fov") {
            args.camera.vfovDeg = requireFloat(i);
        } else if (a == "--eye") {
            args.camera.eye.x = requireFloat(i);
            args.camera.eye.y = requireFloat(i);
            args.camera.eye.z = requireFloat(i);
        } else if (a == "--target") {
            args.camera.target.x = requireFloat(i);
            args.camera.target.y = requireFloat(i);
            args.camera.target.z = requireFloat(i);
        } else if (a == "--up") {
            args.camera.up.x = requireFloat(i);
            args.camera.up.y = requireFloat(i);
            args.camera.up.z = requireFloat(i);
        } else if (a == "--lightdir") {
            args.options.lightDir.x = requireFloat(i);
            args.options.lightDir.y = requireFloat(i);
            args.options.lightDir.z = requireFloat(i);
        } else if (a == "--background") {
            args.options.background.r = requireFloat(i);
            args.options.background.g = requireFloat(i);
            args.options.background.b = requireFloat(i);
        } else if (a == "--shadows") {
            args.options.shadows = (requireInt(i) != 0);
        } else if (a == "--reflections") {
            args.options.reflections = (requireInt(i) != 0);
        } else {
            throw std::runtime_error("Unknown argument: " + a);
        }
    }

    if (args.options.width <= 0 || args.options.height <= 0) {
        throw std::runtime_error("Image dimensions must be positive");
    }
    if (args.camera.vfovDeg <= 1.0f || args.camera.vfovDeg >= 179.0f) {
        throw std::runtime_error("FOV must be in range (1, 179)");
    }
    if (glm::length(args.options.lightDir) < 1e-8f) {
        throw std::runtime_error("Light direction must be non-zero");
    }
    args.options.lightDir = glm::normalize(args.options.lightDir);

    return args;
}

int main(int argc, char **argv) {
    try {
        const Args args = parseArgs(argc, argv);

        std::cout << "Loading OBJ: " << args.inputPath << "\n";
        const ObjMesh mesh = loadObj(args.inputPath);
        std::cout << "  vertices:  " << mesh.positions.size() << "\n";
        std::cout << "  triangles: " << mesh.triangles.size() << "\n";

        RTCDevice device = rtcNewDevice(nullptr);
        if (!device) {
            throw std::runtime_error("Failed to create Embree device");
        }

        RTCScene scene = rtcNewScene(device);
        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);

        auto *v = static_cast<Vec3fa *>(
            rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, sizeof(Vec3fa),
                                    mesh.positions.size()));
        auto *t = static_cast<Triangle *>(
            rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, sizeof(Triangle),
                                    mesh.triangles.size()));

        for (size_t i = 0; i < mesh.positions.size(); ++i) {
            v[i].x = mesh.positions[i].x;
            v[i].y = mesh.positions[i].y;
            v[i].z = mesh.positions[i].z;
            v[i].w = 0.0f;
        }
        for (size_t i = 0; i < mesh.triangles.size(); ++i) {
            t[i] = mesh.triangles[i];
        }

        rtcCommitGeometry(geom);
        rtcAttachGeometry(scene, geom);
        rtcReleaseGeometry(geom);
        rtcCommitScene(scene);

        const int width = args.options.width;
        const int height = args.options.height;
        std::vector<uint8_t> image(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);

        const glm::vec3 forward = glm::normalize(args.camera.target - args.camera.eye);
        const glm::vec3 right = glm::normalize(glm::cross(forward, args.camera.up));
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float fovRad = glm::radians(args.camera.vfovDeg);
        const float halfH = std::tan(fovRad * 0.5f);
        const float halfW = aspect * halfH;

        std::cout << "Rendering " << width << "x" << height << "...\n";
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float u = ((x + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f) * halfW;
                const float vScr = (1.0f - (y + 0.5f) / static_cast<float>(height) * 2.0f) * halfH;
                const glm::vec3 rayDir = glm::normalize(forward + u * right + vScr * up);

                RTCRayHit rayhit{};
                glm::vec3 color;
                if (intersectScene(scene, args.camera.eye, rayDir, 0.001f, std::numeric_limits<float>::infinity(),
                                   rayhit)) {
                    color = shadeHit(scene, rayhit, rayDir, args.options);
                } else {
                    color = sky(rayDir, args.options.background);
                }

                color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                color = glm::pow(color, glm::vec3(1.0f / 2.2f));

                const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
                image[idx + 0] = static_cast<uint8_t>(std::round(color.r * 255.0f));
                image[idx + 1] = static_cast<uint8_t>(std::round(color.g * 255.0f));
                image[idx + 2] = static_cast<uint8_t>(std::round(color.b * 255.0f));
            }
        }

        if (stbi_write_jpg(args.outputPath.c_str(), width, height, 3, image.data(), 95) == 0) {
            throw std::runtime_error("Failed to write output image: " + args.outputPath);
        }

        rtcReleaseScene(scene);
        rtcReleaseDevice(device);

        std::cout << "Saved image: " << args.outputPath << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
