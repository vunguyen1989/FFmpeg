.PHONY: all build clean run debug

# FFmpeg source directory (3 levels up from doc/examples/videoplayer)
FFMPEG_DIR := ../../..

TARGET := player
SRC := app.c

# Static libraries from local FFmpeg build
FFMPEG_LIBS := \
	$(FFMPEG_DIR)/libavutil/libavutil.a \
	$(FFMPEG_DIR)/libavformat/libavformat.a \
	$(FFMPEG_DIR)/libavcodec/libavcodec.a \
	$(FFMPEG_DIR)/libswscale/libswscale.a \
	$(FFMPEG_DIR)/libswresample/libswresample.a

# Include directories
FFMPEG_INC := -I$(FFMPEG_DIR)/libavutil -I$(FFMPEG_DIR)/libavcodec -I$(FFMPEG_DIR)/libavformat -I$(FFMPEG_DIR)/libswscale -I$(FFMPEG_DIR)/libswresample

# SDL2 (adjust path if needed)
SDL2_INC := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/opt/homebrew/include/SDL2")
SDL2_LIB := $(shell pkg-config --libs sdl2 2>/dev/null || echo "-L/opt/homebrew/lib -lSDL2")

# Apple frameworks (required for static FFmpeg on macOS)
FRAMEWORKS := \
	-framework AudioToolbox \
	-framework VideoToolbox \
	-framework CoreFoundation \
	-framework CoreMedia \
	-framework CoreVideo \
	-framework CoreServices \
	-framework Security \
	-framework CoreAudio

# Compiler flags
CFLAGS := -Wall -Wextra -Wno-deprecated-declarations -g -O0 $(FFMPEG_INC) $(SDL2_INC)
LDFLAGS := $(FFMPEG_LIBS) -lm -lz -lpthread -liconv -llzma -lbz2 $(SDL2_LIB) $(FRAMEWORKS)

all: build

build:
	@echo "Building with static FFmpeg libraries..."
	gcc $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

run: build
	./$(TARGET) $(FFMPEG_DIR)/Iron_Man-Trailer_HD.mp4 10

debug: build
	lldb ./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -f frame*.ppm