#ifndef IMAGES_HEADER
#define IMAGES_HEADER

#include <GL/glew.h>

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned short ushort;

typedef struct {
	uchar* data;
	uint width, height;
	GLuint color_encoding;
} Image;

int image_load_bmp(Image* image, const char* path);
int image_load_png(Image* image, const char* path);

void image_grayscale_transparency(Image* image);
void image_destroy(Image* image);

#endif // IMAGES_HEADER
