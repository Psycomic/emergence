#include "ulisp.h"

#include <string.h>

#define nil NULL

LispObject* ulisp_read(const char* stream) {
	char* string = strdup(stream);

	LispObject* new_list = malloc(sizeof(LispObject));

	while (*(string++) == ' ');

	if (*string == '(') { // if the input is a list
		new_list->type = LISP_LIST;

		while (*(++string) == ' ');
		if (*(string + 1) == ')') {
			new_list->data.list = nil;
			goto end;
		}
		else {
			char* first_symbol = string + 1;

		}
	} else { // Is a symbol
		char* ptr = string;
		while (*(ptr++) != ' ');
		*ptr = '\0';

		new_list->type = LISP_SYMBOL;
		new_list->data.symbol = string;
	}

end:
	return new_list;
}
