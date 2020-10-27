TARGET = Emergence.exe

all:
	gcc *.c -o ${TARGET} -g3 -lglfw3 -lGL -lGLEW -ldl -lpthread -lX11 -lXrandr -lXinerama -lXi -lm
