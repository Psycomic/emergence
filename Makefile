TARGET_DEBUG = x64/Debug/Emergence
TARGET_RELEASE = x64/Release/Emergence

LFLAGS = -lglfw -lGL -lGLEW -ldl -lpthread -lX11 -lXrandr -lXinerama -lXi -lm

debug:
	gcc *.c -o ${TARGET_DEBUG} -g -D _DEBUG ${LFLAGS}

release:
	gcc *.c -o ${TARGET_RELEASE} -O3 ${LFLAGS}
