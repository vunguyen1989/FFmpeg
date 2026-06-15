.PHONY: all build clean run debug

TARGET := player
SRC := app.c

all: build

build:
	gcc -Wall -Wextra -Wno-deprecated-declarations -g -O0 \
		$(shell pkg-config --cflags libavutil libavformat libavcodec libswscale libswresample sdl2) \
		-o $(TARGET) $(SRC) \
		$(shell pkg-config --libs libavutil libavformat libavcodec libswscale libswresample sdl2)

run: build
	./$(TARGET) ../Iron_Man-Trailer_HD.mp4 10

debug: build
	lldb ./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -f frame*.ppm