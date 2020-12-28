TARGET_DEBUG = Emergence_Debug
TARGET_RELEASE = Emergence_Release

LFLAGS = -lglfw -lGL -lGLEW -ldl -lpthread -lX11 -lXrandr -lXinerama -lXi -lm

debug:
	gcc *.c -o ${TARGET_DEBUG} -g -D _DEBUG ${LFLAGS}

release:
	gcc *.c -o ${TARGET_RELEASE} -O3 ${LFLAGS}
