TARGET_DEBUG = Emergence_Debug
TARGET_RELEASE = Emergence_Release

CFLAGS = -Wall -std=c99 `pkg-config --cflags icu-uc icu-io`
LFLAGS = -lglfw -lGL -lGLEW -ldl -lpthread -lX11 -lXrandr -lXinerama -lXi -lm `pkg-config --libs icu-uc icu-io`
CC = gcc

debug:
	${CC} *.c -o ${TARGET_DEBUG} -g -D _DEBUG ${CFLAGS} ${LFLAGS}

release:
	${CC} *.c -o ${TARGET_RELEASE} -O3 -DNDEBUG ${CFLAGS} ${LFLAGS}
