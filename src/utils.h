#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "def.h"

#define is_curses_key(key) (KEY_MIN < key && key < KEY_MAX)
#define keyname_cmp(key, str) (strcmp(keyname(key), str) == 0)

#define wvalign_center(win, len) (align_center(getmaxy(win), len))
#define whalign_center(win, len) (align_center(getmaxx(win), len))
#define wvalignstr_center(win, str) (wvalign_center(win, strlen(str)))
#define whalignstr_center(win, str) (whalign_center(win, strlen(str)))

#define walign_right(win, len, dist) (align_end(getmaxx(win), len, dist))
#define walign_bottom(win, len, dist) (align_end(getmaxy(win), len, dist))
#define walignstr_right(win, str, dist) (walign_right(win, strlen(str), dist))
#define walignstr_bottom(win, str, dist) (walign_bottom(win, strlen(str), dist))

#define arr_size(x) (sizeof(x)/sizeof(*x))
static inline bool streq(const char* str1, const char* str2) {
	return strcmp(str1, str2) == 0;
}

static inline int max(int a, int b) { return (a > b) ? a : b; }
static inline int min(int a, int b) { return (a < b) ? a : b; }

static inline int align_center(unsigned max, unsigned len) { 
    return (max - len) / 2; 
}

static inline int align_end(unsigned max, unsigned len, unsigned dist) {
	return max - (len + dist); 
}

static inline int word_visual_len(const TypeText* tt, int word_num) {
	return max(tt->text[word_num].len, tt->matches[word_num].len);
}

/* tests if range is set to it's "null" value */
static inline bool is_range_off(const ConfRange* r) {
	return r->min < 0 && r->max < 0;
}

void* ecalloc(size_t, size_t);
void* ereallocarray(void*, size_t, size_t);

int streqi(const char*, const char*);

int encase_word(char*, const char*);

int to_rgb(const char*, RGB*);
void rgb_to_cursrgb(RGB*);

long elapsed_ms(struct timespec*, struct timespec*);

int nlines_text(const Word*, const Word*, size_t, size_t, int);

int parse_range(ConfRange*);
int parse_punct(ConfPunct*, int);

int split_str(char*, char***, size_t*, const char*);

void normalize_confrange(ConfRange*, int, int);

bool contains_ch(const char*, int);
int fskipws(FILE*, const char*);
int skipws(const char*, const char*);
int get_delims(FILE*, char*, size_t, const char*);

void free_words(Word*, size_t);
void free_strs(char**, size_t);
void free_namevals(NameVal*, size_t);
void free_quote(Quote* quote);
void free_quotes(Quotes* quote);

int find_str(char**, const char*);
int find_stri(char**, const char*);
int sfind_str(char**, size_t, const char*);
int sfind_stri(char**, size_t, const char*);

size_t strstr_len(char** const);
char** strstr_dup(char**);
int strstr_add(char***, char*);
void strstr_remove(char**, size_t);
void strstr_remdup(char**);

int vsnprintf_fit(char*, size_t, int, const char*, va_list);
int snprintf_fit(char*, size_t, int, const char*, ...);

FILE* path_fopen(char**, const char*);

int dir_contents(char*, char***);
int dirs_contents(char**, char***, int);

int create_dir(const char*, unsigned);

#endif /* UTILS_H */
