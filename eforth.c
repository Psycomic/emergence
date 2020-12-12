#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "eforth.h"

GLboolean is_blank(char c) {
	return c == ' ' || c == '\n' || c == '\t';
}

GLboolean is_number(char* stream) {
	while (*stream >= '1' && *stream <= '9') {
		if (*(++stream) == '\0')
			return GL_TRUE;
	}

	return GL_FALSE;
}

void eforth_object_print(EForthObject* obj) {
	if (obj->type == FORTH_TYPE_INT)
		printf("%d", obj->data.integer);
	else
		printf("%s", obj->data.word);
}

void eforth_parse(char* stream, DynamicArray* words) {
	char* new_stream = _strdup(stream);
	char* stream_end = new_stream + strlen(new_stream);

	while (new_stream < stream_end) {
		while (is_blank(*(new_stream++)));
		new_stream--;

		if (*new_stream == '\0')
			break;

		int i;
		for (i = 0; !(is_blank(new_stream[i]) || new_stream[i] == '\0'); i++);

		new_stream[i] = '\0';

		EForthObject* obj = dynamic_array_push_back(words);
		
		if (is_number(new_stream)) {
			obj->type = FORTH_TYPE_INT;
			obj->data.integer = atoi(new_stream);
		}
		else {
			obj->type = FORTH_TYPE_WORD;
			obj->data.word = new_stream;
		}

		new_stream += (size_t)i + 1;
	}
} 