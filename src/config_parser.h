#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdio.h>
#include <stdlib.h>

#define CONFE_FILE    -1
#define CONFE_MEM     -2
#define CONFE_PARSE   -3

typedef struct {
	char* name;
	char* value;
} ConfigOption;

typedef struct {
	ConfigOption* options;
	size_t sz;
} ConfigList;

void free_config(ConfigList*);
int read_config(FILE*, ConfigList*, char***);

#endif /* CONFIG_PARSER_H */
