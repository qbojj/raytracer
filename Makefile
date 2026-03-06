CXX := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -pedantic -Wno-missing-field-initializers

EMBREE_CFLAGS := $(shell pkg-config --cflags embree4 2>/dev/null)
EMBREE_LIBS := $(shell pkg-config --libs embree4 2>/dev/null)

ifeq ($(strip $(EMBREE_LIBS)),)
EMBREE_LIBS := -lembree4
endif

CXXFLAGS += $(EMBREE_CFLAGS)

TARGET := raytracer
SRC := src/main.cpp
RENDER_DIR := renders
ZIP_NAME := raytracer_submission.zip
ZIP_CONTENTS := Makefile README.md src scenes

SINGLE_TRIANGLE_OBJ := scenes/single_triangle.obj
PYRAMID_ROOM_OBJ := scenes/pyramid_room.obj

SINGLE_TRIANGLE_JPG := $(RENDER_DIR)/single_triangle.jpg
PYRAMID_ROOM_JPG := $(RENDER_DIR)/pyramid_room.jpg

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(EMBREE_LIBS)

$(RENDER_DIR):
	mkdir -p $(RENDER_DIR)

render-single-triangle: $(TARGET) | $(RENDER_DIR)
	./$(TARGET) $(SINGLE_TRIANGLE_OBJ) $(SINGLE_TRIANGLE_JPG) \
		--width 1280 --height 720 \
		--eye 0.0 1.0 3.0 --target 0.0 0.6 0.0 --up 0 1 0 \
		--fov 55 --lightdir 0.7 1.0 0.4 --shadows 1 --reflections 0

render-pyramid-room: $(TARGET) | $(RENDER_DIR)
	./$(TARGET) $(PYRAMID_ROOM_OBJ) $(PYRAMID_ROOM_JPG) \
		--width 1600 --height 900 \
		--eye 2.8 1.7 3.2 --target 0.0 0.8 0.0 --up 0 1 0 \
		--fov 52 --lightdir 0.6 1.0 0.3 --shadows 1 --reflections 1

render-all: render-single-triangle render-pyramid-room
	@echo "Rendered scenes to $(RENDER_DIR)/"

zip-clean:
	rm -f $(ZIP_NAME)

zip: zip-clean
	zip -r $(ZIP_NAME) $(ZIP_CONTENTS)
	@echo "Created $(ZIP_NAME)"

clean:
	rm -f $(TARGET)
	rm -rf $(RENDER_DIR)

.PHONY: all clean render-single-triangle render-pyramid-room render-all zip zip-clean
