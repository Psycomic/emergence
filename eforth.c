#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "eforth.h"

typedef struct {
	union {
		EForthException (*build_in)(DynamicArray*);
		DynamicArray* defined_function;
	} data;

	GLboolean is_build_in;
} EForthWord;

static HashTable* eforth_dictionnary;
static EForthWord eforth_word_point;
static EForthWord eforth_word_plus;
static EForthWord eforth_word_mul;

GLboolean is_blank(char c) {
	return c == ' ' || c == '\n' || c == '\t';
}

GLboolean is_number(char* stream) {
	if (*stream == '-')
		stream++;

	while (*stream >= '0' && *stream <= '9') {
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

EForthException eforth_lex(DynamicArray* parsed_strings, DynamicArray* words) {
	// Filling words
	
	for (uint i = 0; i < parsed_strings->size; i++) {
		char* str = *((char**)dynamic_array_at(parsed_strings, i));

		if (strcmp(str, ":") == 0) {
			uint j;
			for (j = i; strcmp(*((char**)dynamic_array_at(parsed_strings, j)), ";") != 0; j++) {
				if (j >= parsed_strings->size - 1)
					return EFORTH_NO_CLOSING_DELIMITER;
			}

			DynamicArray* new_word = malloc(sizeof(DynamicArray));
			DYNAMIC_ARRAY_CREATE(new_word, EForthObject);

			uint word_size = (j - 1) - i;

			DynamicArray word_strings;
			word_strings.element_size = sizeof(char*);
			word_strings.size = word_size;
			word_strings.capacity = word_size;
			word_strings.data = malloc(sizeof(char*) * word_size);

			memcpy(word_strings.data, (char**)parsed_strings->data + i, word_size);

			EForthException exception;
			if ((exception = eforth_lex(&word_strings, new_word)) != EFORTH_NO_ERROR)
				return exception;

			dynamic_array_destroy(&word_strings);

			i = j;
		}
		else if (is_number(str)) {
			EForthObject* obj = dynamic_array_push_back(words);

			obj->type = FORTH_TYPE_INT;
			obj->data.integer = atoi(str);
		}
		else {
			EForthObject* obj = dynamic_array_push_back(words);

			if (hash_table_get(eforth_dictionnary, str) == NULL) {
				printf(" %s ?\n", str);
				return EFORTH_UNKNOWN_WORD;
			}

			obj->type = FORTH_TYPE_WORD;
			obj->data.word = str;
		}
	}

	return EFORTH_NO_ERROR;
}

EForthException eforth_parse(char* stream, DynamicArray* words) {
	DynamicArray parsed_strings;
	DYNAMIC_ARRAY_CREATE(&parsed_strings, char*);

	char* new_stream = _strdup(stream);
	char* stream_end = new_stream + strlen(new_stream);

	// Parsing the text
	while (new_stream < stream_end) {
		while (is_blank(*(new_stream++)));
		new_stream--;

		if (*new_stream == '\0')
			break;

		int i;
		for (i = 0; !(is_blank(new_stream[i]) || new_stream[i] == '\0'); i++);

		new_stream[i] = '\0';

		char** str = dynamic_array_push_back(&parsed_strings);
		*str = new_stream;

		new_stream += (size_t)i + 1;
	}

	EForthException exception;
	if ((exception = eforth_lex(&parsed_strings, words)) != EFORTH_NO_ERROR) {
		dynamic_array_destroy(&parsed_strings);
		
		return exception;
	}

	dynamic_array_destroy(&parsed_strings);

	return EFORTH_NO_ERROR;
} 

EForthException eforth_point(DynamicArray* stack) {
	if (stack->size < 1)
		return EFORTH_STACK_UNDERFLOW;

	printf("%d ", *((int*)dynamic_array_last(stack)));
	
	dynamic_array_pop(stack, 1);

	return EFORTH_NO_ERROR;
}

EForthException eforth_plus(DynamicArray* stack) {
	if (stack->size < 2)
		return EFORTH_STACK_UNDERFLOW;

	int* a = dynamic_array_at(stack, stack->size - 1);
	int* b = dynamic_array_at(stack, stack->size - 2);

	dynamic_array_pop(stack, 2);

	*((int*)dynamic_array_push_back(stack)) = *a + *b;

	return EFORTH_NO_ERROR;
}

EForthException eforth_mul(DynamicArray* stack) {
	if (stack->size < 2)
		return EFORTH_STACK_UNDERFLOW;

	int* a = dynamic_array_at(stack, stack->size - 1);
	int* b = dynamic_array_at(stack, stack->size - 2);

	dynamic_array_pop(stack, 2);

	*((int*)dynamic_array_push_back(stack)) = *a * *b;

	return EFORTH_NO_ERROR;
}

EForthException eforth_execute(DynamicArray* program, DynamicArray* stack) {
	for (uint i = 0; i < program->size; i++) {
		EForthObject* current_word = dynamic_array_at(program, i);
		
		if (current_word->type == FORTH_TYPE_INT) {
			int* stack_top = dynamic_array_push_back(stack);
			*stack_top = current_word->data.integer;
		}
		else {
			EForthWord* word = hash_table_get(eforth_dictionnary, current_word->data.word);

			EForthException exception;
			if (word->is_build_in)
				exception = word->data.build_in(stack);
			else
				exception = eforth_execute(word->data.defined_function, stack);

			if (exception != EFORTH_NO_ERROR)
				return exception;
		}
	}

	return EFORTH_NO_ERROR;
}

EForthException eforth_eval(char* stream, DynamicArray* stack) {
	DynamicArray forth_words;
	DYNAMIC_ARRAY_CREATE(&forth_words, EForthObject);

	EForthException exception;
	if ((exception = eforth_parse(stream, &forth_words)) != EFORTH_NO_ERROR)
		goto error;

	if ((exception = eforth_execute(&forth_words, stack)) != EFORTH_NO_ERROR)
		goto error;

	printf(" ok\n");
	return EFORTH_NO_ERROR;

error:
	printf("Raised exception of type %d\n", exception);
	dynamic_array_clear(stack);

	return exception;
}

void eforth_initialize(void) {
	eforth_dictionnary = hash_table_create(64);

	eforth_word_point.is_build_in = GL_TRUE;
	eforth_word_point.data.build_in = &eforth_point;
	
	eforth_word_plus.is_build_in = GL_TRUE;
	eforth_word_plus.data.build_in = &eforth_plus;

	eforth_word_mul.is_build_in = GL_TRUE;
	eforth_word_mul.data.build_in = &eforth_mul;

	hash_table_set(eforth_dictionnary, ".", &eforth_word_point, sizeof(EForthWord));
	hash_table_set(eforth_dictionnary, "+", &eforth_word_plus, sizeof(EForthWord));
	hash_table_set(eforth_dictionnary, "*", &eforth_word_mul, sizeof(EForthWord));
}