TARGET_DEBUG = Emergence_Debug
TARGET_RELEASE = Emergence_Release

CFLAGS = -Wall -std=c99
LFLAGS = -lglfw -lGL -lGLEW -ldl -lpthread -lX11 -lXrandr -lXinerama -lXi -lm
CC = gcc

debug:
	${CC} *.c -o ${TARGET_DEBUG} -g -D _DEBUG ${CFLAGS} ${LFLAGS}

release:
	${CC} *.c -o ${TARGET_RELEASE} -O3 -DNDEBUG ${CFLAGS} ${LFLAGS}

assembly:
	nasm -f elf64 stack.asm -o stack.o
	gcc stack.o -o stack
	rm stack.o
