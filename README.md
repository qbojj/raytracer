# Simple OBJ Ray Tracer (Embree + GLM + stb_image_write)

This project implements a simple university ray tracing assignment:
- Loads geometry from Wavefront OBJ (`v` and `f` entries)
- Uses Intel Embree for ray-triangle intersections
- Uses GLM for camera/vector math
- Writes output as JPG via `stb_image_write`
- Supports primary rays plus optional one shadow ray and one reflection ray per pixel

## Build

On Arch Linux:

```bash
sudo pacman -S embree glm stb
make
```

On Ubuntu/Debian-like systems:

```bash
sudo apt install libembree-dev libglm-dev libstb-dev
make
```

This creates executable `./raytracer`.

## Usage

```bash
./raytracer <input.obj> <output.jpg> [options]
```

Options:
- `--width <int>` image width (default `1280`)
- `--height <int>` image height (default `720`)
- `--fov <float>` vertical field of view in degrees
- `--eye <x y z>` camera position
- `--target <x y z>` camera look-at point
- `--up <x y z>` camera up vector
- `--lightdir <x y z>` normalized direction from surface to light
- `--background <r g b>` sky/background tint in range `[0,1]`
- `--shadows <0|1>` enable one shadow ray (default `1`)
- `--reflections <0|1>` enable one reflection ray (default `0`)

The `eye/target/up/fov` parameters map directly to an OpenGL-style camera setup.

## Example Commands

```bash
./raytracer scenes/single_triangle.obj out_triangle.jpg
```

```bash
./raytracer scenes/pyramid_room.obj out_room.jpg \
  --width 1600 --height 900 \
  --eye 2.8 1.7 3.2 --target 0.0 0.8 0.0 --up 0 1 0 \
  --fov 52 --lightdir 0.6 1.0 0.3 --shadows 1 --reflections 1
```

## Makefile Render Targets

Generate all bundled scene renders:

```bash
make render-all
```

Generate a single scene render:

```bash
make render-single-triangle
```

```bash
make render-pyramid-room
```

Outputs are written to `renders/`.

## Project Structure

- `src/main.cpp` ray tracer implementation
- `src/stb_image_write.h` image writer header (single-header library)
- `scenes/*.obj` example scenes
- `Makefile` build rules
