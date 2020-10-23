#pragma once

#ifndef GUI_INTERNAL
typedef void Window;
typedef void Widget;
#endif // !GUI_INTERNAL

#ifdef GUI_INTERNAL
typedef struct Widget {
	Vector2 position;

	struct Widget* parent;

	float margin;
	float height;

	enum {
		WIDGET_TYPE_LABEL = 0,
		WIDGET_TYPE_BUTTON
	} type;

	Layout layout;
	uint index;
} Widget;

typedef struct {
	Widget header;

	Text* text;
} Label;

typedef struct {
	Widget header;

	float padding;

	Text* text;
	Drawable* button_background;
} Button;

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

	Text* title;

	Drawable* drawables[2];
	Widget* widgets[64];
} Window;

#endif // GUI_INTERNAL

