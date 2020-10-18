#define RENDER_INTERNAL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "misc.h"
#include "linear_algebra.h"
#include "stack_allocator.h"
#include "render.h"

#define SCENE_DEFAULT_CAPACITY 10
#define CAMERA_SPEED 0.1f
#define MOUSE_SENSIBILLITY 0.01f

// Shader's uniform abstraction. 
typedef struct {
	enum { 
		UNIFORM_VEC3, UNIFORM_FLOAT, UNIFORM_BOOL 
	} type;

	union {
		Vector3 vec;
		float f;
		uint b;
	} data;

	GLuint location;
	GLboolean is_set;
} Uniform;

// Basically a collection of uniforms for a particular shader.
typedef struct {
	GLuint program_id;
	GLuint view_position_matrix_location;
	GLuint model_matrix_location;

	uint uniforms_count;
	Uniform uniforms[];
} Material;

// Array Buffer abstraction
typedef struct {
	void* data;
	uint size;
	GLuint buffer;
} Buffer;

// Biggest abstraction yet. A Drawable is a collection of array
// buffers, some flags, a material, a pointer to a position, and some flags.

#define DRAWBLE_MAX_BUFFER_COUNT 256

typedef struct {
	Buffer elements_buffer;

	Material* material;
	Vector3* position;
	GLuint* textures;

	GLuint vertex_array;
	uint textures_count;
	uint elements_count;
	uint buffer_count;
	uint flags;

	Buffer buffers[];
} Drawable;


/// A camera, that holds a position and a rotation to render
/// a scene correctly
typedef struct {
	Vector3 position;

	float rx;
	float ry;

	float perspective_matrix[16];	// Not having to recreate one every time
	float rotation_matrix[16];		// Needed for the direction
} Camera;

// Abstraction over text. Basically a collection of drawables, each with a 
// glyph texture, to end up with text
typedef struct {
	Vector3 position;
	Vector3 color;

	Drawable* drawable;
	char* string;

	float size;
	float angle;

	GLuint font_texture;
} Text;

typedef struct Widget {
	Vector2 position;

	struct Widget* parent;

	float margin;
	float height;

	enum {
		WIDGET_TYPE_LABEL = 0,
	} type;

	Layout layout;
	uint index;
} Widget;

typedef struct {
	Widget header;

	Text text;
} Label;

// Window UI : a width, height, mininum width and transparency.
// every drawable has its own container, differing from the original scene.
typedef struct {
	Vector3 position;

	float width;
	float height;
	float min_width;
	float min_height;

	float transparency;

	uint widgets_count;
	Layout layout;

	Text title;

	Drawable* drawables[2];
	Widget* widgets[64];
} Window;

#define SCENE_MAX_DRAWABLES 10
#define SCENE_MAX_GUI_ELEMENTS 10
#define SCENE_MAX_WINDOWS 10

#define SCENE_GUI_MODE (1 << 0)

// What you see on the screen. It is basically the container of every
// graphical aspect of the game : the 3D view and the 2D UI.
typedef struct {
	Window windows[SCENE_MAX_WINDOWS];
	Drawable* drawables[SCENE_MAX_DRAWABLES];

	Camera camera;

	uint flags;

	uint drawables_count;
	uint windows_count;

	uint selected_window;
} Scene;


uint glfw_last_character = 0;

static unsigned short rectangle_elements[] = { 0, 1, 2, 1, 3, 2 };

GLuint window_background_shader;
GLuint ui_texture_shader;
GLuint color_shader;
GLuint window_title_font_texture;

static char* color_uniforms[] = {
	"color", "model_position"
};

Drawable axis_drawable;
GLuint axis_shader;
Material* axis_material = NULL;

double last_xpos = -1.0, last_ypos = -1.0;

void render_initialize(void);

float random_float(void);

void window_draw(Window* window);

void window_set_position(Window* window, float x, float y);
void window_set_size(Window* window, float width, float height);
void window_set_transparency(Window* window, float transparency);

void material_use(Material* material, float* model_matrix, float* view_position_matrix);

void widget_draw(Window* window, Widget* widget, Vector3 position);
Vector3 widget_get_real_position(Widget* widget);
void widget_set_transparency(Widget* widget, float transparency);
float widget_get_height(Widget* widget);

void error(const char* s) {
	printf("[Error] %s\n", s);
	exit(1);
}

void camera_create_rotation_matrix(Mat4 destination, float rx, float ry) {
	float rotation_matrix_x[16];
	float rotation_matrix_y[16];

	mat4_create_rotation_x(rotation_matrix_x, rx);
	mat4_create_rotation_y(rotation_matrix_y, ry);

	mat4_mat4_mul(destination, rotation_matrix_y, rotation_matrix_x); // First, Y rotation, after X rotation
}

void camera_create_final_matrix(Mat4 destination, Mat4 perspective, Mat4 rotation, Vector3 position) {
	float translation_matrix[16];
	float temporary_matrix[16];

	vector3_neg(&position);
	mat4_create_translation(translation_matrix, position);

	mat4_mat4_mul(temporary_matrix, translation_matrix, rotation);
	mat4_mat4_mul(destination, temporary_matrix, perspective);
}

void camera_init(Camera* camera, Vector3 position, float rx, float ry, float far, float near) {
	camera->position = position;

	camera->rx = rx;
	camera->ry = ry;

	mat4_create_perspective(camera->perspective_matrix, far, near);
}

void camera_get_final_matrix(Camera* camera, Mat4 final_matrix) {
	camera_create_rotation_matrix(camera->rotation_matrix, camera->rx, camera->ry);
	camera_create_final_matrix(final_matrix, camera->perspective_matrix, camera->rotation_matrix, camera->position);
}

void camera_get_direction(Camera* camera, Vector3* direction, float speed) {
	Vector3 orientation = { 0.f, 0.f, 1.f };

	mat4_vector3_mul(direction, orientation, camera->rotation_matrix);
	vector3_scalar_mul(direction, *direction, speed);
}

void camera_translate(Camera* camera, Vector3 direction) {
	vector3_sub(&camera->position, camera->position, direction);
}

void camera_rotate(Camera* camera, float rx, float ry) {
	camera->rx += rx;
	camera->ry += ry;
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
	glfw_last_character = codepoint;
}

GLFWwindow* opengl_window_create(uint width, uint height, const char* title) {
	// Initialize GLFW and the opengl context
	glewExperimental = 1;

	if (!glfwInit()) {
		fprintf(stderr, "GLFW not initialized correctly !\n");
		return NULL;
	}

	glfwWindowHint(GLFW_SAMPLES, 4); // 4x antialiasing
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // We want OpenGL 3.3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // We don't want the old OpenGL

	GLFWwindow* window;
	window = glfwCreateWindow(width, height, title, NULL, NULL);

	if (window == NULL) {
		fprintf(stderr, "Failed to open GLFW window.");
		glfwTerminate();

		return NULL;
	}

	glfwSetCharCallback(window, character_callback);

	glfwMakeContextCurrent(window);

	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		glfwTerminate();

		return NULL;
	}

	// Disable double buffering
	glfwSwapInterval(1);

	// OpenGL settings
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glViewport(0, 0, width, height);

	// Initialize scene.
	render_initialize();

	return window;
}

Buffer array_buffer_create(uint size, int type, void* data) {
	GLuint array_buffer;
	glGenBuffers(1, &array_buffer);
	glBindBuffer(type, array_buffer);
	glBufferData(type, size, data, GL_DYNAMIC_DRAW);

	Buffer buffer;
	buffer.buffer = array_buffer;
	buffer.data = data;
	buffer.size = size;

	return buffer;
}

void array_buffer_update(Buffer* buffer) {
	glBindBuffer(GL_ARRAY_BUFFER, buffer->buffer);
	glBufferSubData(GL_ARRAY_BUFFER, 0, buffer->size, buffer->data);
}

GLuint texture_create(Image* image, bool generate_mipmap) {
	GLuint texture;
	glGenTextures(1, &texture);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image->width, image->height, 0, image->color_encoding, GL_UNSIGNED_BYTE, image->data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	if (generate_mipmap)
		glGenerateMipmap(GL_TEXTURE_2D);

	return texture;
}

void rectangle_vertices_set(float* rectangle_vertices, float width, float height) {
	rectangle_vertices[0] = 0.f; rectangle_vertices[1] = 0.f;
	rectangle_vertices[2] = width; rectangle_vertices[3] = 0.f;
	rectangle_vertices[4] = 0.f; rectangle_vertices[5] = height;
	rectangle_vertices[6] = width; rectangle_vertices[7] = height;
}

// Additional buffers : Vector3* buffer_data, uint data_number, GLuint data_layout
void drawable_init(Drawable* drawable, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_count, Material* material, Vector3* position, GLuint* textures, uint textures_count, uint flags) {
	drawable->buffer_count = declarations_count;
	drawable->material = material;
	drawable->position = position;
	drawable->textures = textures;
	drawable->textures_count = textures_count;
	drawable->flags = flags;

	glGenVertexArrays(1, &drawable->vertex_array);
	glBindVertexArray(drawable->vertex_array);

	for (uint i = 0; i < declarations_count; i++) {
		drawable->buffers[i] = array_buffer_create(declarations[i].data_size, GL_ARRAY_BUFFER, declarations[i].data);

		glBindBuffer(GL_ARRAY_BUFFER, drawable->buffers[i].buffer);
		glVertexAttribPointer(declarations[i].data_layout, declarations[i].stride, GL_FLOAT, GL_FALSE, declarations[i].stride * sizeof(float), NULL);

		glEnableVertexAttribArray(declarations[i].data_layout);
	}

	drawable->elements_count = elements_number;

	if (elements != NULL) {
		drawable->elements_buffer = array_buffer_create(elements_number * sizeof(unsigned short), GL_ELEMENT_ARRAY_BUFFER, elements);
		drawable->flags |= DRAWABLE_USES_ELEMENTS;
	}
	else {
		drawable->elements_buffer.buffer = 0;
		drawable->flags &= ~DRAWABLE_USES_ELEMENTS;
	}

	glBindVertexArray(0);
}

Drawable* drawable_create(Scene* scene, unsigned short* elements, uint elements_number, ArrayBufferDeclaration* declarations, uint declarations_number, Material* material, Vector3* position, GLuint* textures, uint textures_count, uint flags) {
	Drawable* drawable = malloc(sizeof(Drawable) + declarations_number * sizeof(Buffer));

	scene->drawables[scene->drawables_count++] = drawable;

	drawable_init(drawable, elements, elements_number, declarations, declarations_number, material, position, textures, textures_count, flags);
	return drawable;
}

void drawable_destroy(Drawable* drawable) {
	for (uint i = 0; i < drawable->buffer_count; i++) {
		glDeleteBuffers(1, &drawable->buffers[i].buffer);
		free(drawable->buffers[i].data);
	}

	if (drawable->flags & DRAWABLE_USES_ELEMENTS) {
		glDeleteBuffers(1, &drawable->elements_buffer.buffer);
		free(drawable->elements_buffer.data);
	}

	free(drawable);
}

void drawable_draw(Drawable* drawable, GLenum mode) {
	for (uint i = 0; i < drawable->textures_count; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, drawable->textures[i]);
	}
	
	glBindVertexArray(drawable->vertex_array);

	if ((drawable->flags & DRAWABLE_USES_ELEMENTS) > 0)
		glDrawElements(mode, drawable->elements_count, GL_UNSIGNED_SHORT, NULL);
	else
		glDrawArrays(mode, 0, drawable->elements_count);
}

void drawable_update_buffer(Drawable* drawable, uint buffer_id) {
	array_buffer_update(&drawable->buffers[buffer_id]);
}

void drawable_update(Drawable* drawable) {
	for (uint i = 0; i < drawable->buffer_count; i++)
		array_buffer_update(&drawable->buffers[i]);
}

void drawable_rectangle_init(Drawable* drawable, float width, float height, Material* material, Vector3* position, uint flags) {
	float* rectangle_vertices = malloc(sizeof(float) * 8);
	rectangle_vertices_set(rectangle_vertices, width, height);

	ArrayBufferDeclaration rectangle_buffers[] = {
		{rectangle_vertices, sizeof(float) * 8, 2, 0},
	};

	drawable_init(drawable, rectangle_elements, array_size(rectangle_elements), rectangle_buffers, 1, material, position, NULL, 0, flags);
}

void drawable_rectangle_texture_init(Drawable* drawable, float width, float height, Material* material, Vector3* position, GLuint* textures, uint textures_count, float* texture_uv, uint flags) {
	float* rectangle_vertices = malloc(sizeof(float) * 8);
	rectangle_vertices_set(rectangle_vertices, width, height);

	ArrayBufferDeclaration rectangle_buffers[] = {
		{rectangle_vertices, sizeof(float) * 8, 2, 0},
		{texture_uv, sizeof(float) * 8, 2, 1}
	};

	drawable_init(drawable, rectangle_elements, array_size(rectangle_elements), rectangle_buffers, 2, material, position, textures, textures_count, flags);
}

void drawable_rectangle_set_size(Drawable* rectangle, float width, float height) {
	float* rectangle_vertices = rectangle->buffers[0].data;
	rectangle_vertices[0] = 0.f; rectangle_vertices[1] = 0.f;
	rectangle_vertices[2] = width; rectangle_vertices[3] = 0.f;
	rectangle_vertices[4] = 0.f; rectangle_vertices[5] = height;
	rectangle_vertices[6] = width; rectangle_vertices[7] = height;

	drawable_update_buffer(rectangle, 0);
}

GLuint shader_create(const char* vertex_shader_path, const char* fragment_shader_path) {
	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	// Read the Vertex Shader code from the file
	char VertexShaderCode[2048];
	read_file(VertexShaderCode, vertex_shader_path);

	// Read the Fragment Shader code from the file
	char FragmentShaderCode[2048];
	read_file(FragmentShaderCode, fragment_shader_path);

	GLint Result = GL_FALSE;
	int InfoLogLength;

	// Compile Vertex Shader
	printf("Compiling shader : %s\n", vertex_shader_path);
	char const* VertexSourcePointer = VertexShaderCode;

	glShaderSource(VertexShaderID, 1, &VertexSourcePointer, NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (InfoLogLength > 0) {
		char* VertexShaderErrorMessage = malloc(sizeof(char) * (InfoLogLength + 1));
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, VertexShaderErrorMessage);
		printf("%s\n", VertexShaderErrorMessage);

		free(VertexShaderErrorMessage);
	}

	// Compile Fragment Shader
	printf("Compiling shader : %s\n", fragment_shader_path);
	char const* FragmentSourcePointer = FragmentShaderCode;
	glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer, NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (InfoLogLength > 0) {
		char* FragmentShaderErrorMessage = malloc(sizeof(char) * (InfoLogLength + 1));
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, FragmentShaderErrorMessage);
		printf("%s\n", FragmentShaderErrorMessage);

		free(FragmentShaderErrorMessage);
	}

	// Link the program
	printf("Linking program\n");
	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);

	if (InfoLogLength > 0) {
		char* ProgramErrorMessage = malloc(sizeof(char) * (InfoLogLength + 1));
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		printf("%s\n", ProgramErrorMessage);

		free(ProgramErrorMessage);
	}

	glDetachShader(ProgramID, VertexShaderID);
	glDetachShader(ProgramID, FragmentShaderID);

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	return ProgramID;
}

Material* material_create(GLuint shader, char** uniforms_position, uint uniforms_count) {
	Material* new_material = malloc(sizeof(Material) + sizeof(Uniform) * uniforms_count);
	new_material->program_id = shader;

	new_material->uniforms_count = uniforms_count;

	new_material->view_position_matrix_location = glGetUniformLocation(shader, "view_position");
	new_material->model_matrix_location = glGetUniformLocation(shader, "model");

	for (uint i = 0; i < uniforms_count; i++) {
		new_material->uniforms[i].location = glGetUniformLocation(shader, uniforms_position[i]);
		new_material->uniforms[i].is_set = GL_FALSE;
	}

	return new_material;
}

void material_set_uniform_vec(Material* material, uint uniform_id, Vector3 vec) {
	material->uniforms[uniform_id].type = UNIFORM_VEC3;
	material->uniforms[uniform_id].data.vec = vec;
	material->uniforms[uniform_id].is_set = GL_TRUE;
}

void material_set_uniform_float(Material* material, uint uniform_id, float f) {
	material->uniforms[uniform_id].type = UNIFORM_FLOAT;
	material->uniforms[uniform_id].data.f = f;
	material->uniforms[uniform_id].is_set = GL_TRUE;
}

void material_set_uniform_bool(Material* material, uint uniform_id, uint b) {
	material->uniforms[uniform_id].type = UNIFORM_BOOL;
	material->uniforms[uniform_id].data.b = b;
	material->uniforms[uniform_id].is_set = GL_TRUE;
}

void material_use(Material* material, float* model_matrix, float* view_position_matrix) {
	glUseProgram(material->program_id);

	if (view_position_matrix)
		glUniformMatrix4fv(material->view_position_matrix_location, 1, GL_FALSE, view_position_matrix);
	if (model_matrix)
		glUniformMatrix4fv(material->model_matrix_location, 1, GL_FALSE, model_matrix);

	Uniform* uniforms = material->uniforms;

	for (uint i = 0; i < material->uniforms_count; i++) {
		if (uniforms[i].is_set) {
			GLuint uniform_id = uniforms[i].location;

			switch (material->uniforms[i].type) {
			case UNIFORM_VEC3:
				glUniform3f(uniform_id, uniforms[i].data.vec.x, uniforms[i].data.vec.y, uniforms[i].data.vec.z);
				break;
			case UNIFORM_FLOAT:
				glUniform1f(uniform_id, uniforms[i].data.f);
				break;
			case UNIFORM_BOOL:
				glUniform1i(uniform_id, uniforms[i].data.b);
				break;
			}
		}
	}
}

void text_set_color(Text* text, Vector3 color) {
	material_set_uniform_vec(text->drawable->material, 2, color);
}

void text_set_transparency(Text* text, float transparency) {
	material_set_uniform_float(text->drawable->material, 5, transparency);
}

void text_set_angle(Text* text, float angle) {
	text->angle = angle;

	material_set_uniform_float(text->drawable->material, 3, text->angle);
}

void text_set_position(Text* text, Vector3 position) {
	text->position = position;
}

float text_get_width(Text* text) {
	return strlen(text->string) * text->size;
}

float text_get_height(Text* text) {
	char* c = text->string;
	uint count = 0;

	do
	{
		if (*c == '\n')
			count++;
	} while (*(++c) != '\0');

	return text->size * (count + 1);
}

// Every glyph will be a 32 x 32 texture. This means the image is 64 x 416
void text_init(Text* text, char* string, GLuint font_texture, float size, Vector3 position, float angle, Vector3 color) {
	text->string = string;
	text->font_texture = font_texture;
	text->position = position;
	text->size = size;
	text->angle = angle;
	text->color = color;

	uint text_length = strlen(string);

	static char* ui_texture_uniforms[] = {
		"model_position",
		"is_text", 
		"color", 
		"angle", 
		"center_position",
		"transparency", 
		"max_width", 
		"max_height",
		"anchor_position"
	};

	Material* glyph_material = material_create(ui_texture_shader, ui_texture_uniforms, array_size(ui_texture_uniforms));
	material_set_uniform_bool(glyph_material, 1, GL_TRUE);
	material_set_uniform_vec(glyph_material, 2, text->color);
	material_set_uniform_float(glyph_material, 3, text->angle);
	material_set_uniform_float(glyph_material, 5, 1.f);

	float* drawable_uvs = malloc(sizeof(float) * 12 * text_length);
	float* drawable_vertices = malloc(sizeof(float) * 12 * text_length);

	const float height = 512,
		half_height = height / 32,
		width = 64,
		half_width = width / 32;

	int y_stride = 0;

	for (uint i = 0, j = 0; j < text_length; j++) {
		if (string[j] == '\n') {
			y_stride--;
			i = 0;
		}
		else {
			uint index = string[j] == ' ' ? 31 : string[j] - 'A';

			float x_pos = ((index % 2) * 32) / 64.f,
				y_pos = (1.f - 1 / half_height) - ((index / 2) * 32) / height;

			float uv_down_left[] = { x_pos, y_pos };
			float uv_down_right[] = { x_pos + 1.f / half_width, y_pos };
			float uv_up_left[] = { x_pos, (y_pos + (1.f / half_height)) };
			float uv_up_right[] = { x_pos + 1.f / half_width, (y_pos + (1.f / half_height)) };

			drawable_uvs[j * 12 + 0] = uv_up_left[0]; drawable_uvs[j * 12 + 1] = uv_up_left[1];
			drawable_uvs[j * 12 + 2] = uv_down_left[0]; drawable_uvs[j * 12 + 3] = uv_down_left[1];
			drawable_uvs[j * 12 + 4] = uv_up_right[0]; drawable_uvs[j * 12 + 5] = uv_up_right[1];

			drawable_uvs[j * 12 + 6] = uv_down_right[0]; drawable_uvs[j * 12 + 7] = uv_down_right[1];
			drawable_uvs[j * 12 + 8] = uv_up_right[0]; drawable_uvs[j * 12 + 9] = uv_up_right[1];
			drawable_uvs[j * 12 + 10] = uv_down_left[0]; drawable_uvs[j * 12 + 11] = uv_down_left[1];

			float vertex_up_left[2] = { i * size, size + y_stride * size };
			float vertex_up_right[2] = { i * size + size, size + y_stride * size };
			float vertex_down_left[2] = { i * size, y_stride * size };
			float vertex_down_right[2] = { i * size + size, y_stride * size };

			drawable_vertices[j * 12 + 0] = vertex_up_left[0]; drawable_vertices[j * 12 + 1] = vertex_up_left[1];
			drawable_vertices[j * 12 + 2] = vertex_down_left[0]; drawable_vertices[j * 12 + 3] = vertex_down_left[1];
			drawable_vertices[j * 12 + 4] = vertex_up_right[0]; drawable_vertices[j * 12 + 5] = vertex_up_right[1];

			drawable_vertices[j * 12 + 6] = vertex_down_right[0]; drawable_vertices[j * 12 + 7] = vertex_down_right[1];
			drawable_vertices[j * 12 + 8] = vertex_up_right[0]; drawable_vertices[j * 12 + 9] = vertex_up_right[1];
			drawable_vertices[j * 12 + 10] = vertex_down_left[0]; drawable_vertices[j * 12 + 11] = vertex_down_left[1];

			i++;
		}
	}

	text->drawable = malloc(sizeof(Drawable) + sizeof(Buffer) * 2);

	ArrayBufferDeclaration declarations[] = {
		{drawable_vertices, sizeof(float) * 12 * text_length, 2, 0},
		{drawable_uvs, sizeof(float) * 12 * text_length, 2, 1}
	};

	drawable_init(text->drawable, NULL, 6 * text_length, declarations, array_size(declarations), glyph_material, NULL, &text->font_texture, 1, 0x0);
}

Text* text_create(char* string, GLuint font_texture, float size, Vector3 position, float angle, Vector3 color) {
	Text* text = malloc(sizeof(Text));
	text_init(text, string, font_texture, size, position, angle, color);

	return text;
}

void text_draw(Text* text, Vector3* shadow_displacement, float max_width, float max_height, Vector3 anchor) {
	Vector3 text_position = text->position;

	material_set_uniform_float(text->drawable->material, 6, max_width);
	material_set_uniform_float(text->drawable->material, 7, max_height);
	
	material_set_uniform_vec(text->drawable->material, 4, text->position);

	material_set_uniform_vec(text->drawable->material, 8, anchor);

	material_use(text->drawable->material, NULL, NULL);

	if (shadow_displacement) {
		Vector3 shadow_color = { 0.f, 0.f, 0.f };
		Vector3 shadow_drawable_position;

		vector3_add(&shadow_drawable_position, text_position, *shadow_displacement);

		glUniform3f(text->drawable->material->uniforms[0].location, shadow_drawable_position.x, shadow_drawable_position.y, shadow_drawable_position.z);
		glUniform3f(text->drawable->material->uniforms[2].location, shadow_color.x, shadow_color.y, shadow_color.z);

		drawable_draw(text->drawable, GL_TRIANGLES);

		glUniform3f(text->drawable->material->uniforms[2].location, text->color.x, text->color.y, text->color.z);
	}

	glUniform3f(text->drawable->material->uniforms[0].location, text_position.x, text_position.y, text_position.z);

	drawable_draw(text->drawable, GL_TRIANGLES);
}

Scene* scene_create(Vector3 camera_position) {
	Scene* scene = malloc(sizeof(Scene));
	camera_init(&scene->camera, camera_position, 0.f, 0.f, 1000.f, 0.1f);

	scene->flags = 0x0;
	scene->drawables_count = 0;
	scene->selected_window = 0;
	scene->windows_count = 0;

	return scene;
}

void scene_draw(Scene* scene, Vector3 clear_color) {
	glClearColor(clear_color.x, clear_color.y, clear_color.z, 0.01f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	float camera_final_matrix[16];
	camera_get_final_matrix(&scene->camera, camera_final_matrix);

	for (uint i = 0; i < scene->drawables_count; i++) {
		Drawable* drawable = scene->drawables[i];

		float position_matrix[16];
		mat4_create_translation(position_matrix, *drawable->position);

		// Drawing the elements added to the scene
		if (drawable->flags & DRAWABLE_NO_DEPTH_TEST)
			glDisable(GL_DEPTH_TEST);
		else
			glEnable(GL_DEPTH_TEST);

		material_use(drawable->material, position_matrix, camera_final_matrix);
		drawable_draw(drawable, GL_TRIANGLES);

		if (drawable->flags & DRAWABLE_SHOW_AXIS) {
			// Drawing the elements axis
			glDisable(GL_DEPTH_TEST);

			material_use(axis_drawable.material, position_matrix, camera_final_matrix);
			drawable_draw(&axis_drawable, GL_LINES);
		}
	}

	if (scene->flags & SCENE_GUI_MODE) {
		glDisable(GL_DEPTH_TEST);

		for (uint i = 0; i < scene->windows_count; i++)
			window_draw(&scene->windows[(i + scene->selected_window + 1) % scene->windows_count]);
	}
}

void scene_handle_events(Scene* scene, GLFWwindow* window) {
	if (glfw_last_character == 'e') 
		scene->flags ^= SCENE_GUI_MODE;

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	int width, height;
	glfwGetWindowSize(window, &width, &height);

	if (scene->flags & SCENE_GUI_MODE && scene->windows_count > 0) {
		for (uint i = 0; i < scene->windows_count; i++)
			if (i == scene->selected_window)
				window_set_transparency(&scene->windows[i], 1.f);
			else
				window_set_transparency(&scene->windows[i], 0.3f);

		Window* selected_window = &scene->windows[scene->selected_window];
		float screen_x = ((float)xpos / (float)width) * 2 - 1,
			screen_y = -(((float)ypos / (float)height) * 2 - 1);

		if (glfw_last_character == ' ')
			scene->selected_window = (scene->selected_window + 1) % scene->windows_count;

		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) {
			window_set_position(selected_window, screen_x - selected_window->width / 2, screen_y - selected_window->height / 2);
		}
		if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) {
			float new_width = screen_x - selected_window->position.x;
			float new_height = screen_y - selected_window->position.y;

			window_set_size(selected_window, new_width, new_height);
		}
	}
	else {
		Vector3 camera_direction;
		camera_get_direction(&scene->camera, &camera_direction, CAMERA_SPEED);

		// Keyboard input handling 
		if (glfwGetKey(window, GLFW_KEY_W)) {
			camera_translate(&scene->camera, camera_direction);
		}
		if (glfwGetKey(window, GLFW_KEY_S)) {
			vector3_neg(&camera_direction);
			camera_translate(&scene->camera, camera_direction);
		}

		if (last_xpos == -1.0 || last_ypos == -1.0) {
			last_xpos = xpos;
			last_ypos = ypos;
		}

		// Rotate the camera proporitionaly to the mouse position
		camera_rotate(&scene->camera, ((float)ypos - last_ypos) * MOUSE_SENSIBILLITY, -((float)xpos - last_xpos) * MOUSE_SENSIBILLITY);
	}

	last_xpos = xpos;
	last_ypos = ypos;
	
	glfw_last_character = 0;
}

void window_set_position(Window* window, float x, float y) {
	window->position.x = x;
	window->position.y = y;

	Material* background_material = window->drawables[0]->material;
	material_set_uniform_vec(background_material, 2, window->position);
	material_set_uniform_vec(background_material, 3, window->position);

	Vector3 text_position = window->position;
	text_position.y += 0.1f * window->height;
	text_position.x += 0.1f * window->width;

	text_set_position(&window->title, text_position);

	material_set_uniform_vec(window->drawables[1]->material, 1, window->position);
}

void window_set_size(Window* window, float width, float height) {
	window->width = max(width, window->min_width);
	window->height = max(height, window->min_height);

	Material* background_material = window->drawables[0]->material;

	material_set_uniform_float(background_material, 4, window->width);
	material_set_uniform_float(background_material, 5, window->height);

	Vector3 text_position = window->position;
	text_position.y += 0.1f * window->height;
	text_position.x += 0.1f * window->width;

	text_set_position(&window->title, text_position);

	drawable_rectangle_set_size(window->drawables[0], window->width, window->height);

	float* text_bar_vertices = window->drawables[1]->buffers[0].data;
	text_bar_vertices[0] = 0.1f * window->width; text_bar_vertices[1] = window->height * 0.9f;
	text_bar_vertices[2] = 0.1f * window->width; text_bar_vertices[3] = 0.1f * window->height;

	drawable_update_buffer(window->drawables[1], 0);
}

void window_set_transparency(Window* window, float transparency) {
	window->transparency = transparency;
	material_set_uniform_float(window->drawables[0]->material, 1, window->transparency);

	for (uint i = 0; i < window->widgets_count; i++)
		widget_set_transparency(window->widgets[i], transparency);
}

Window* window_create(Scene* scene, float width, float height, float* position, char* title) {
	Window* window = &scene->windows[scene->windows_count++];

	window->min_width = 0.6f;
	window->min_height = 0.5f;

	window->position.z = 0;

	window->widgets_count = 0;
	window->layout = LAYOUT_PACK;

	Vector3 title_color = { 0.2f, 0.2f, 1 };
	text_init(&window->title, title, window_title_font_texture, 0.05f, window->position, ((float) M_PI) * 3 / 2, title_color);

	static char* ui_material_uniforms[] = {
		"color",
		"transparency", 
		"position",
		"model_position",
		"width",
		"height",
		"time"
	};

	Drawable* background_drawable = malloc(sizeof(Drawable) + sizeof(Buffer) * 1);
	drawable_rectangle_init(background_drawable, window->width, window->height, material_create(window_background_shader, ui_material_uniforms, array_size(ui_material_uniforms)), &window->position, 0x0);
	window->drawables[0] = background_drawable;

	Vector3 backgroud_color = {
		2 / 255.f,
		128 / 255.f,
		219 / 255.f
	};

	material_set_uniform_vec(background_drawable->material, 0, backgroud_color);

	Drawable* text_bar_drawable = malloc(sizeof(Drawable) + sizeof(Buffer) * 1);

	float* line_vertices = malloc(sizeof(float) * 4);
	ArrayBufferDeclaration text_bar_declarations[] = {
		{line_vertices, sizeof(float) * 4, 2, 0}
	};

	Vector3 bar_color = {
		0.6f, 0.6f, 0.6f
	};

	drawable_init(text_bar_drawable, NULL, 2, text_bar_declarations, 1, material_create(color_shader, color_uniforms, array_size(color_uniforms)), &window->position, NULL, 0, 0x0);
	window->drawables[1] = text_bar_drawable;

	material_set_uniform_vec(window->drawables[1]->material, 0, bar_color);

	window_set_size(window, width, height);
	window_set_position(window, position[0], position[1]);
	window_set_transparency(window, 0.9f);

	return window;
}

Vector3 window_get_anchor(Window* window) {
	Vector3 window_anchor = { 
		window->position.x + window->width * 0.1f + 0.05f, 
		window->position.y + window->height * 0.9f,
		0.f
	};

	return window_anchor;
}

void window_draw(Window* window) {
	// Drawing the background
	Drawable* background_drawable = window->drawables[0];

	material_set_uniform_float(background_drawable->material, 6, (float) glfwGetTime());

	material_use(background_drawable->material, NULL, NULL);
	drawable_draw(background_drawable, GL_TRIANGLES);

	// Drawing the title's line
	Drawable* line_drawable = window->drawables[1];

	material_use(line_drawable->material, NULL, NULL);
	drawable_draw(line_drawable, GL_LINES);

	// Drawing the window's title
	Vector3 shadow_displacement = { 0.01f, 0.f, 0.f };
	text_draw(&window->title, &shadow_displacement, window->height * 0.8f - 0.05f, 1.f, window->title.position);
	
	// Drawing widgets
	if (window->layout == LAYOUT_PACK) {
		// If the window layout is set to pack (every element on top of the next)
		for (uint i = 0; i < window->widgets_count; i++) {
			Vector3 real_position = window_get_anchor(window);
			vector3_add(&real_position, real_position, widget_get_real_position(window->widgets[i]));

			widget_draw(window, window->widgets[i], real_position);
		}
	}
	else if (window->layout == LAYOUT_GRID) {
		// If the window layout is set to grid (take up the max space in the window)
		error("Grid layout not supported");
	}
}

Vector3 widget_get_real_position(Widget* widget) {
	Vector3 widget_position = {
		widget->position.x + widget->margin, 
		widget->position.y - widget->margin,
		0.f
	};

	if (!widget->parent) {
		return widget_position;
	}
	else {
		widget_position.y -= widget->parent->height;

		Vector3 real_position,
			parent_position = widget_get_real_position(widget->parent);

		vector3_add(&real_position, widget_position, parent_position);

		return real_position;
	}
}

Widget* widget_label_create(Window* window, Widget* parent, char* text, float text_size, Vector3 text_color, float margin, Layout layout) {
	Label* label = malloc(sizeof(Label));

	label->header.type = WIDGET_TYPE_LABEL;

	label->header.parent = parent;
	label->header.layout = layout;
	label->header.margin = margin;

	label->header.position.x = 0.f;
	label->header.position.y = 0.f;

	label->header.index = window->widgets_count;

	text_init(&label->text, text, window_title_font_texture, text_size, widget_get_real_position(label, window), 0.f, text_color);

	label->header.height = text_get_height(&label->text);
	label->header.index = window->widgets_count;

	for (uint i = 0; i < window->widgets_count; i++) {
		if (window->widgets[i]->parent == parent && window->widgets[i]->index < label->header.index) {
			label->header.position.y -= window->widgets[i]->height + label->header.margin;
		}
	}

	for (Widget* ptr = parent; ptr != NULL; ptr = ptr->parent) {
		for (uint i = 0; i < window->widgets_count; i++) {
			if (window->widgets[i]->parent == ptr->parent && window->widgets[i]->index > ptr->index) {
				window->widgets[i]->position.y -= label->header.height + label->header.margin;
			}
		}
	}

	window->widgets[window->widgets_count++] = label;

	return label;
}

float widget_get_height(Widget* widget) {
	float height = 0.f;

	switch (widget->type) {
		case WIDGET_TYPE_LABEL:
			height = text_get_height(&((Label*)widget)->text);
			break;
	}

	return height + widget->margin;
}

void widget_label_draw(Window* window, Widget* widget, Vector3 position) {
	Label* label_widget = widget;
	position.y -= label_widget->text.size;

	static Vector3 label_shadow_displacement = { 0.005f, 0.f };

	text_set_position(&label_widget->text, position);

	text_draw(&label_widget->text, &label_shadow_displacement, 
		window->width * 0.95f - (position.x - window->position.x) - widget->margin * 2,
		window->height * 0.8f, window_get_anchor(window));
}

static void (*widget_draw_vtable[])(Window* window, Widget* widget, Vector3 position) = {
	[WIDGET_TYPE_LABEL] = &widget_label_draw
};

void widget_draw(Window* window, Widget* widget, Vector3 position) {
	widget_draw_vtable[widget->type](window, widget, position);
}

void widget_set_transparency(Widget* widget, float transparency) {
	switch (widget->type) {
	case WIDGET_TYPE_LABEL:
		text_set_transparency(&((Label*)widget)->text, transparency);
		break;
	}
}

void render_initialize(void) {
	// Initializing the drawable axis
	static Vector3 blue = { 0, 0, 1 };

	static Vector3 axis[] = {
		0.f, 0.f, 0.f,
		1.f, 0.f, 0.f,
		0.f, 1.f, 0.f,
		0.f, 0.f, 1.f
	};

	static unsigned short axis_elements[] = {
		0, 1, 0, 2, 0, 3
	};

	static ArrayBufferDeclaration axis_buffers[] = {
		{axis, sizeof(axis), 3, 0}
	};

	static Vector3 axis_position = { 0.f, 0.f, 0.f };

	static char* axis_shader_uniforms[] = {
		"color"
	};

	axis_shader = shader_create("./shaders/vertex_shader_uniform_color.glsl", "./shaders/fragment_shader_uniform_color.glsl");

	axis_material = material_create(axis_shader, axis_shader_uniforms, 1);
	material_set_uniform_vec(axis_material, 0, blue);

	drawable_init(&axis_drawable, axis_elements, 6, axis_buffers, 1, axis_material, &axis_position, NULL, 0, 0x0);

	window_background_shader = shader_create("./shaders/vertex_shader_ui_background.glsl", "./shaders/fragment_shader_ui_background.glsl");
	ui_texture_shader = shader_create("./shaders/vertex_shader_ui_texture.glsl", "./shaders/fragment_shader_texture.glsl");

	color_shader = shader_create("./shaders/vertex_shader_ui_background.glsl", "./shaders/fragment_shader_uniform_color.glsl");

	Image font_image;
	if (image_load_bmp(&font_image, "./fonts/monospace.bmp") >= 0)
		printf("Success loading lain !\n");
	else
		printf("Error when loading image !\n");

	window_title_font_texture = texture_create(&font_image, 1);
	image_destroy(&font_image);
}


#undef RENDER_INTERNAL