#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

#include "config_parser.h"

static const char whitespace[] = " \t";

static inline bool is_name_char(int ch) {
	return isalnum(ch) || strchr("_-", ch);
}

static inline bool is_val_char(int ch) {
	return (ch >= 0x20 && ch < 0x7f && !strchr("#\"\\", ch)) || ch == '\t';
}

static inline bool is_quoted_char(int ch) {
	return (ch >= 0x20 && ch < 0x7f && !strchr("\"\\", ch)) || ch == '\t';
}

static inline bool is_escape_char(int ch) {
	return (ch >= 0x20 && ch < 0x7f) || ch == '\t';
}

static void free_strs(char** strs, size_t sz) {
	for(size_t i=0; i<sz; i++)
		free(strs[i]);
}

static void free_config_options(ConfigOption* options, size_t sz) {
	for(size_t i=0; i<sz; i++) {
		free(options[i].name);
		free(options[i].value);
	}
}

void free_config(ConfigList* config) {
	free_config_options(config->options, config->sz);
	free(config->options);
}

/* return the number of leading whitespace in str. */
/* characters considered whitespace given by whitespace. */
static int skipws(const char* str, const char* whitespace) {
	for(size_t i=0; ; ++i)
		if(!str[i] || !strchr(whitespace, str[i]))
			return i;
}

/* lower case every character in str. */
static void tolowerstr(char* str) {
	for(int i=0; str[i] != '\0'; i++)
		str[i] = tolower(str[i]);
}

static ConfigOption* find_option(ConfigList* config, const char* name) {
	for(size_t i=0; i<config->sz; i++)
		if(strcmp(config->options[i].name, name) == 0)
			return &config->options[i];

	return NULL;
}

static void decode_quoted_string(char* buf) {
	int j=0;
	for(int i=0; buf[i]; i++, j++) {
		if(buf[i] == '\\') i++;
		buf[j] = buf[i];
	}

	buf[j] = '\0';
}

static int parse_quoted(const char* ptr_buf, char** value) {
	const char* ptr = ptr_buf;

	if(*ptr++ != '"')
		return CONFE_PARSE;

	while(true) {
		if(is_quoted_char(*ptr)) ptr++;

		else if(*ptr == '\\') {
			if(!is_escape_char(*(++ptr)))
				return CONFE_PARSE;

			ptr++;
		}

		else break;
	}

	if(*ptr++ != '"')
		return CONFE_PARSE;

	if(!(*value = strndup(ptr_buf+1, (ptr-1) - (ptr_buf+1))))
		return CONFE_MEM;

	decode_quoted_string(*value);
	return (ptr - ptr_buf);
}

static int parse_config_value(const char* ptr_buf, char** value) {
	const char* ptr = ptr_buf;

	ptr += skipws(ptr, whitespace);

	const char* end = ptr;
	for(; is_val_char(*end); end++);
	while(end != ptr_buf && strchr(whitespace, *(end-1))) end--;
	if(end <= ptr)
		return CONFE_PARSE;

	if(!(*value = strndup(ptr, end-ptr)))
		return CONFE_MEM;
 
	return (ptr - ptr_buf);
}

static int parse_config_option(const char* ptr_buf, ConfigOption* option, const char** error) {
	static const char* err_value = "bad config-option value";
	static const char* err_equals = "'=' not found";
	static const char* err_name = "bad config-option name";
	const char* ptr = ptr_buf;
	int result;

	for(; is_name_char(*ptr); ptr++);
	if(ptr == ptr_buf) {
		if(error) *error = err_name;
		return CONFE_PARSE;
	}

	if(!(option->name = strndup(ptr_buf, (ptr - ptr_buf))))
		return CONFE_MEM;

	tolowerstr(option->name);

	ptr += skipws(ptr, whitespace);
	if(*ptr++ != '=') {
		free(option->name);
		if(error) *error = err_equals;
		return CONFE_PARSE;
	}

	ptr += skipws(ptr, whitespace);
	
	ptr += result = (*ptr == '"')
		? parse_quoted(ptr, &option->value)
		: parse_config_value(ptr, &option->value);
	if(result < 0) {
		free(option->name);
		if(error) *error = err_value;
		return result;
	}

	return (ptr - ptr_buf);
}

int read_config(FILE* fd_conf, ConfigList* config, char*** errors) {
	int result;
	const size_t bsz = 1024;
	char buf[bsz];
	size_t errors_cap = 8;
	size_t config_cap = 32;
	int line_num = 1;        /* Line number tracker for reporting errors */
	size_t sz_errors = 0; 

	config->sz = 0;
	config->options = calloc(config_cap, sizeof(ConfigOption));
	if(!config->options)
		return CONFE_MEM;

	*errors = calloc(errors_cap, sizeof(char*));
	if(!config->options) {
		free(config->options);
		return CONFE_MEM;
	}

	for(;; line_num++) {
		const char* opt_err;
		char* line = NULL;
		size_t sz_line;
		result = getline(&line, &sz_line, fd_conf);
		if(result < 0) {
			free(line);
			
			if(errno != ENOMEM)
				break;

			free_config_options(config->options, config->sz);
			free(config->options);
			free_strs(*errors, sz_errors);
			free(*errors);
			return CONFE_MEM;
		}

		char* ptr_line = line;
		ptr_line += skipws(line, whitespace);
		if(strchr("#\n", *ptr_line)) {
			free(line);
			continue;
		}

		if(config->sz == config_cap) {
			ConfigOption* temp;

			temp = realloc(config->options, (config_cap*=2)*sizeof(ConfigOption));
			if(!temp) {
				free(line);
				free_config_options(config->options, config->sz);
				free(config->options);
				free_strs(*errors, sz_errors);
				free(*errors);
				return CONFE_MEM;
			}
			
			config->options = temp; 
		}

		result = parse_config_option(ptr_line, &config->options[config->sz], &opt_err);
		if(result < 0) {
			if(result == CONFE_MEM) {
				free(line);
				free_config_options(config->options, config->sz);
				free(config->options);
				free_strs(*errors, sz_errors);
				free(*errors);
				return CONFE_MEM;
			}
			
			if(sz_errors == errors_cap-1) {
				char** temp;

				temp = realloc(*errors, (errors_cap*=2)*sizeof(char*));
				if(!temp) {
					free(line);
					free_config_options(config->options, config->sz);
					free(config->options);
					free_strs(*errors, sz_errors);
					free(*errors);
					return CONFE_MEM;
				}
				
				*errors = temp; 
			}

			snprintf(buf, bsz, "%d %s", line_num, opt_err);
			if(!((*errors)[sz_errors] = strdup(buf))) {
				free(line);
				free_config_options(config->options, config->sz);
				free(config->options);
				free_strs(*errors, sz_errors);
				free(*errors);
				return CONFE_MEM;
			}

			sz_errors++;
		}

		else {
			ConfigOption* opt_ptr = find_option(config, config->options[config->sz].name);
			if(opt_ptr) {
				free(config->options[config->sz].name);
				free(opt_ptr->value);
				opt_ptr->value = config->options[config->sz].value;
			}

			else config->sz++;
		}

		free(line);
	}

	(*errors)[sz_errors] = NULL;
	
	return 0;
}
