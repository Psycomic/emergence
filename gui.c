#define GUI_INTERNAL

#include "render.h"
#include "gui.h"

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
	text_init(&window->title, title, monospaced_font_texture, 0.05f, window->position, ((float)M_PI) * 3 / 2, title_color);

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
	drawable_rectangle_init(background_drawable, window->width, window->height, material_create(window_background_shader, ui_material_uniforms, array_size(ui_material_uniforms)), GL_TRIANGLES, &window->position, 0x0);
	window->drawables[0] = background_drawable;

	Vector3 backgroud_color = {
		//2 / 255.f,
		//128 / 255.f,
		//219 / 255.f
		1 / 255.f,
		64 / 255.f,
		109 / 255.f
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

	drawable_init(text_bar_drawable, NULL, 2, text_bar_declarations, 1, material_create(color_shader, color_uniforms, array_size(color_uniforms)), GL_LINES, &window->position, NULL, 0, 0x0);
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

	material_set_uniform_float(background_drawable->material, 6, (float)glfwGetTime());

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
			widget_draw(window, window->widgets[i]);
		}
	}
	else if (window->layout == LAYOUT_GRID) {
		// If the window layout is set to grid (take up the max space in the window)
		error("Grid layout not supported");
	}
}

Vector2 window_get_max_position(Window* window) {
	Vector2 max_positon;

	max_positon.x = window->width * 0.95f;
	max_positon.y = window->height * 0.8f;

	return max_positon;
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

void widget_init(Widget* widget, Window* window, Widget* parent, float margin, Layout layout) {
	widget->parent = parent;
	widget->layout = layout;
	widget->margin = margin;

	widget->position.x = 0.f;
	widget->position.y = 0.f;

	widget->index = window->widgets_count;

	for (uint i = 0; i < window->widgets_count; i++) {
		if (window->widgets[i]->parent == parent && window->widgets[i]->index < widget->index) {
			widget->position.y -= window->widgets[i]->height + widget->margin;
		}
	}

	for (Widget* ptr = parent; ptr != NULL; ptr = ptr->parent) {
		for (uint i = 0; i < window->widgets_count; i++) {
			if (window->widgets[i]->parent == ptr->parent && window->widgets[i]->index > ptr->index) {
				window->widgets[i]->position.y -= widget->height + widget->margin;
			}
		}
	}

	window->widgets[window->widgets_count++] = widget;
}

Widget* widget_label_create(Window* window, Widget* parent, char* text, float text_size, Vector3 color, float margin, Layout layout) {
	Label* label = malloc(sizeof(Label));

	label->header.type = WIDGET_TYPE_LABEL;

	Vector3 text_position = { 0.f, 0.f, 0.f };
	text_init(&label->text, text, monospaced_font_texture, text_size, text_position, 0.f, color);

	label->header.height = text_get_height(&label->text);

	widget_init(label, window, parent, margin, layout);

	return label;
}

Widget* widget_button_create(Window* window, Widget* parent, char* text, float text_size, Vector3 color, float margin, float padding, Layout layout) {
	Button* button = malloc(sizeof(Button));

	button->header.type = WIDGET_TYPE_BUTTON;

	Vector3 text_position = { 0.f, 0.f, 0.f };
	text_init(&button->text, text, monospaced_font_texture, text_size, text_position, 0.f, color);

	button->header.height = text_get_height(&button->text) + padding * 2;
	float width = text_get_width(&button->text);
	float height = button->header.height;

	float* button_rectangle_vertices = malloc(sizeof(float) * 16);

	button_rectangle_vertices[0] = 0.f; button_rectangle_vertices[1] = 0.f, button_rectangle_vertices[2] = 0.f; button_rectangle_vertices[3] = height;
	button_rectangle_vertices[4] = 0.f; button_rectangle_vertices[5] = height; button_rectangle_vertices[6] = width; button_rectangle_vertices[7] = height;
	button_rectangle_vertices[8] = width; button_rectangle_vertices[9] = height; button_rectangle_vertices[10] = 0.f; button_rectangle_vertices[11] = height;
	button_rectangle_vertices[12] = 0.f; button_rectangle_vertices[13] = height; button_rectangle_vertices[14] = 0.f;  button_rectangle_vertices[15] = 0.f;

	ArrayBufferDeclaration button_background_declarations[] = {
		{button_rectangle_vertices, sizeof(float) * 16, 2, 0}
	};

	button->button_background = malloc(sizeof(Drawable) + sizeof(Buffer) * 1);

	Material* button_material = material_create(color_shader, color_uniforms, array_size(color_uniforms));
	drawable_init(button->button_background, NULL, 8, button_background_declarations, 1, button_material, GL_LINES, &window->position, NULL, 0, 0x0);

	widget_init(button, window, parent, margin, layout);
}

float widget_get_height(Widget* widget) {
	return widget->height;
}

void widget_label_draw(Window* window, Widget* widget, Vector3 position) {
	Label* label_widget = widget;

	static Vector3 label_shadow_displacement = { 0.005f, 0.f };

	text_set_position(&label_widget->text, position);

	Vector2 window_max_position = window_get_max_position(window);

	text_draw(&label_widget->text, &label_shadow_displacement,
		window_max_position.x - (position.x - window->position.x) - widget->margin * 2,
		window_max_position.y,
		window_get_anchor(window));
}

void widget_label_set_transparency(Widget* widget, float transparency) {
	text_set_transparency(&((Label*)widget)->text, transparency);
}

void widget_button_draw(Window* window, Widget* widget, Vector3 position) {
	Button* button = widget;

	text_set_position(&button->text, position);

	Vector2 window_max_position = window_get_max_position(window);

	text_draw(&button->text, NULL,
		window_max_position.x - (position.x - window->position.x) - widget->margin * 2,
		window_max_position.y,
		window_get_anchor(window));

	material_use(button->button_background->material, NULL, NULL);
	material_set_uniform_vec(button->button_background->material, 1, position);

	drawable_draw(button->button_background);
}

void widget_button_set_transparency(Widget* widget, float transparency) {
	Button* button = widget;

	text_set_transparency(&button->text, transparency);
	material_set_uniform_float(button->button_background->material, 2, transparency);
}

static void (*widget_draw_vtable[])(Window* window, Widget* widget, Vector3 position) = {
	[WIDGET_TYPE_LABEL] = &widget_label_draw,
	[WIDGET_TYPE_BUTTON] = &widget_button_draw,
};

static void (*widget_set_transparency_vtable[]) (Widget* widget, float transparency) = {
	[WIDGET_TYPE_LABEL] = &widget_label_set_transparency,
	[WIDGET_TYPE_BUTTON] = &widget_button_set_transparency,
};

void widget_draw(Window* window, Widget* widget) {
	Vector3 real_position = window_get_anchor(window);
	vector3_add(&real_position, real_position, widget_get_real_position(widget));

	widget_draw_vtable[widget->type](window, widget, real_position);
}

void widget_set_transparency(Widget* widget, float transparency) {
	widget_set_transparency_vtable[widget->type](widget, transparency);
}
