#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <ncurses.h>

#include "def.h"
#include "utils.h"

void* ecalloc(size_t nmemb, size_t sz) {
    void* ptr;

    if(!(ptr = calloc(nmemb, sz))) {
        fprintf(stderr, "Error: Memory exhausted");
        exit(1);
    }

    return ptr;
}

void* ereallocarray(void* ptr, size_t nmemb, size_t sz) {
    void* nptr;

    if(!(nptr = realloc(ptr, nmemb*sz))) {
        fprintf(stderr, "Error: Memory exhausted");
        exit(1);
    }

    return nptr;
}

int streqi(const char* str1, const char* str2) {
	size_t i=0;

	for(; str1[i] && str2[i]; i++) {
		if(tolower(str1[i]) != tolower(str2[i]))
			return 0;
	}

	if(str1[i] != str2[i]) return 0;
	return 1;
}

/* Assumes $buf's capacity is at least: $strlen(buf) + $strlen(str) + 1 */
int encase_word(char* buf, const char* str) {
	const int blen = strlen(buf);
	const int slen = strlen(str);

	if(!slen || slen & 1) return -1;

	int dist = strlen(str) / 2;
	for(int i = blen+dist; i-dist >= 0; i--)
		buf[i] = buf[i-dist];

	for(int i=0; i<dist; i++)
		buf[i] = str[i];

	for(int i=0; i<dist; i++)
		buf[i+blen+dist] = str[i+dist];

	buf[blen+slen] = '\0';
	return 0;
}

static int hextoint(int ch) {
    static char* hex = "abcdefABCDEF";

    if(strchr(hex, ch) != NULL)
        return toupper(ch) - 55;

    return ch - '0';
}

int to_rgb(const char* str, RGB* rgb) {
    const char* ptr = str;
    int* buf[3] = {&rgb->r, &rgb->g, &rgb->b};

    for(size_t i=0; i<3; i++) {
        if(!isxdigit(ptr[0]) || !isxdigit(ptr[1]))
            return -1;
        
        *buf[i] = 16*hextoint(ptr[0]) + hextoint(ptr[1]);

        ptr+=2;
    }

    return 0;
}

void rgb_to_cursrgb(RGB* rgb) {
    rgb->r *= 1000 / 255;
    rgb->g *= 1000 / 255;
    rgb->b *= 1000 / 255;
}

long elapsed_ms(struct timespec* start, struct timespec* end) {
	return (end->tv_sec - start->tv_sec)*1e3
		 + (end->tv_nsec - start->tv_nsec)/1e6;
}

/* Assumes no word in $text or $matches is greater than $w, & that $text is non-empty */
int nlines_text(const Word* text, const Word* matches, size_t sz_text, size_t sz_matches, int w) {
	int line = 1;
	int x = 0;
	for(size_t i=0; i<sz_text; i++) {
		int len = (i < sz_matches)
			? max(text[i].len, matches[i].len)
			: text[i].len;
		x += len + 1;
		if(x > w) {
			x = len + 1;
			line++;
		}
	}
	
	return line;
}

static int parse_positive(const char* buf, int* value) {
	char* endptr;
	
	if(!isdigit(*buf))
		return -1;

	long temp = strtol(buf, &endptr, 10);
	if(temp > INT_MAX) 
		return -1;

	*value = (int)temp;

	return (endptr - buf);
}

/* Return the number of bytes parsed */
int parse_range(ConfRange* range) {
	const char* ptr = range->str;
	int result;

	if(range->str[0] == '\0' || strcmp(range->str, "-") == 0) {
		strcpy(range->str, "-");
		range->min = -1;
		range->max = -1;
		range->valid = true;
		return 0;
	}

	ptr += result = parse_positive(ptr, &range->min);
	if(result < 0)
		return -1;

	if(*ptr++ != '-') {
		range->max = range->min;
		return result;
	}

	ptr += result = parse_positive(ptr, &range->max);
	if(result < 0)
		return -1;

	return (ptr - range->str);
}

/* Return the number of bytes parsed */
int parse_punct(ConfPunct* punct, int type) {
	char buf[MAX_STRING_OPT];
	char* str;

	if(strlen(punct->str) > MAX_STRING_OPT)
		return -1;

	if(punct->str[0] == '\0') {
		punct->punct = NULL;
		punct->sz = 0;
		return 0;
	}

	strcpy(buf, punct->str);

	char** strs;
	if(split_str(buf, &strs, &punct->sz, " ") < 0)
		return -1;

	punct->punct = ecalloc(punct->sz, sizeof(Punctuation));
	for(size_t i=0 ; i<punct->sz; i++) {
		if(type == PUNCT_CIRCUMFIX && strlen(strs[i]) & 1) {
			free_strs(strs, punct->sz);
			free(strs);
			free(punct->punct);
			return -1;
		}

		punct->punct[i].str  = strs[i];
		punct->punct[i].type = type;
	}

	free(strs);
	return 0;
}

int split_str(char* buf, char*** strs, size_t* sz, const char* delim) {
	size_t cap_strs = 8;

	if(!(*strs = calloc(cap_strs, sizeof(char*))))
		return -1;

	for(*sz=0; ; (*sz)++, buf=NULL) {
		char* token = strtok(buf, delim);
		if(!token)
			break;
	
		if(*sz == cap_strs) {
			char** temp = realloc(*strs, (cap_strs*=2)*sizeof(char*));
			if(!temp) {
				free_strs(*strs, *sz);
				free(*strs);
				return -1;
			}

			*strs = temp;
		}

		if(!((*strs)[*sz] = strdup(token))) {
			free_strs(*strs, *sz);
			free(*strs);
			return -1;
		}
	}

	return 0;
}

void normalize_confrange(ConfRange* range, int mmin, int mmax) {
	if(is_range_off(range)) return;
	if(range->max < range->min) range->max = range->min;
	range->min = min(max(range->min, mmin), mmax);
	range->max = min(max(range->max, mmin), mmax);
	(range->min == range->max)
		? sprintf(range->str, "%d", range->min)
		: sprintf(range->str, "%d-%d", range->min, range->max);
}

int fskipws(FILE* fd, const char* whitespace) {
    int c;
	
    while((c = fgetc(fd)) != 0 && strchr(whitespace, c));
    ungetc(c, fd);
    return 0;
}

int skipws(const char* str, const char* whitespace) {
    size_t i=0;
	for(; str[i]; i++)
		if(!strchr(whitespace, str[i])) break;

    return i;
}

int get_delims(FILE* fd, char* buf, size_t sz, const char* delims) {
    int c;
    size_t i=0;
    
    while(i < sz && (c = fgetc(fd)) != EOF) {
        buf[i++] = c;
        if(strchr(delims, c)) break;
    }

    if(i == sz) return -1;
    buf[i] = '\0';
    return 0;
}

void free_words(Word* chpp, size_t sz) {
	for(size_t i=0; i<sz; ++i)
		free(chpp[i].str);
	
	free(chpp);
	chpp = NULL;
}

void free_strs(char** strs, size_t sz) {
	for(size_t i=0; i<sz; i++)
		free(strs[i]);
}

void free_namevals(NameVal* lv, size_t sz) {
	for(size_t i=0; i<sz; i++) {
		free(lv[i].name);
		free(lv[i].val);
	}
}

void free_quote(Quote* quote) {
	free(quote->author);
	free(quote->source);
	free_words(quote->text, quote->sz);
}

void free_quotes(Quotes* quote) {
	for(size_t i=0; i<quote->sz; i++)
		free_quote(&quote->quotes[i]);
}

int find_str(char** strstr, const char* str) {
	for(int i=0; strstr[i] != NULL; i++)
		if(streq(strstr[i], str)) return i;

	return -1;
}

int find_stri(char** strstr, const char* str) {
	for(int i=0; strstr[i] != NULL; i++)
		if(streqi(strstr[i], str)) return i;

	return -1;
}

int sfind_str(char** strstr, size_t sz, const char* str) {
	for(size_t i=0; i<sz; i++)
		if(streq(strstr[i], str)) return i;

	return -1;
}

int sfind_stri(char** strstr, size_t sz, const char* str) {
	for(size_t i=0; i<sz; i++)
		if(streqi(strstr[i], str)) return i;

	return -1;
}

size_t strstr_len(char** strstr) {
	size_t i=0;
	for(; strstr[i]; i++);
	return i;
}

char** strstr_dup(char** strstr) {
	size_t len = strstr_len(strstr);
	char** ret = malloc(len+1 * sizeof(char**));
	if(ret == NULL)
		return NULL;

	for(size_t i=0; i<len; i++) {
		if((ret[i] = strdup(strstr[i])) == NULL) {
			free_strs(ret, i);
			free(ret);
			return NULL;
		}
	}

	return ret;
}

int strstr_add(char*** strstr, char* str) {
	size_t len = strstr_len(*strstr);	
	char* ts;
	char** tss = calloc(len+2, sizeof(char**));
	if(!tss) return -1;
	for(size_t i=0; i<len; i++)
		tss[i] = (*strstr)[i];

	if((ts = strdup(str)) == NULL) {
		free(tss);
		return -1;
	}

	tss[len] = ts;
	tss[len+1] = NULL;
	free(*strstr);
	*strstr = tss;
	return 0;
}

void strstr_remove(char** strstr, size_t n) {
	free(strstr[n]);
	for(size_t i=n; strstr[i]; i++) {
		strstr[i] = strstr[i+1];
	}
}

/* remove duplicates */
void strstr_remdup(char** strstr) {
	for(size_t i=0; strstr[i]; i++) {
		for(size_t j=0; strstr[j]; j++) {
			if(i == j) continue;

			if(streq(strstr[i], strstr[j]))
				strstr_remove(strstr, j);
		}
	}
}

int vsnprintf_fit(char* buf, size_t sz, int width, const char* fmt, va_list ap) {
    const int sz_ellipsis = 2; /* number of dots in the ellipsis */
	int ret;
	
	ret = vsnprintf(buf, sz, fmt, ap);
	if(ret < 0) return ret;
	if(ret > width) {
		if(width < sz_ellipsis) buf[0] = '\0';
		else {
			int i=width;
			for(; i-sz_ellipsis >= 0 && buf[i-sz_ellipsis-1] == ' '; i--);
			buf[i] = '\0';
            for(int j=1; j<=sz_ellipsis; j++)
                buf[i-j] = '.';
		}
	}

	return ret;
}

int snprintf_fit(char* buf, size_t sz, int width, const char* fmt, ...) {
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vsnprintf_fit(buf, sz, width, fmt, ap);
	va_end(ap);

	return ret;
}

FILE* path_fopen(char** path, const char* fname) {
	size_t bsz = 4096;
	char buf[bsz];
	FILE* file;
	for(size_t i=0; path[i]; i++) {
		snprintf(buf, bsz, "%s/%s", path[i], fname); 

		if((file = fopen(buf, "r")) == NULL) {
			if(errno == ENOENT) continue;
			break;
		}

		else return file;
	}

	return NULL;
}

/* Given path to a directory, return the contents of said directory. */
int dir_contents(char* dirpath, char*** contents) {
	size_t csz = 16;
	DIR* dir;
	struct dirent* entry;
	size_t i;

	if(!(dir = opendir(dirpath))) return -1;
	
	if(!(*contents = malloc(csz * sizeof(char*)))) {
		closedir(dir);
		return -1;
	}

	i = 0;

	for(; errno=0, (entry = readdir(dir)) != NULL;) {
		if(strcmp(entry->d_name, ".") == 0
		|| strcmp(entry->d_name, "..") == 0)
			continue;

		if(i == csz-1) {
			char** temp;
			csz *= 2;

			if((temp = realloc(*contents, csz*sizeof(char*))) == NULL) {
				free_strs(*contents, i);
				free(*contents);
				closedir(dir);
			}
			*contents = temp;
		}

		if(((*contents)[i] = strdup(entry->d_name)) == NULL) {
			closedir(dir);
			free_strs(*contents, i);
			free(*contents);
			return -1;
		}

		i++;
	}

	(*contents)[i] = NULL;
	closedir(dir);
	if(errno != 0) {
		free_strs(*contents, i);
		free(*contents);
		return -1; 
	}

	return i;
}

int dirs_contents(char** dir_list, char*** contents, int strict) {
	size_t csz = 8;
	size_t sz = 0;
	
	if(!(*contents = malloc(csz * sizeof(char**))))
		return -1;

	for(size_t i=0; dir_list[i]; i++) {
		char** list;
		int n = dir_contents(dir_list[i], &list);

		if(n < 0) {
			if(!strict) continue;
			free_strs(*contents, sz);
			free(*contents);
			return -1;
		}

		if(n + sz >= csz) {
			void* temp;

			csz = csz*2 + n;
			if((temp = realloc(*contents, csz * sizeof(char**))) == NULL) {
				free_strs(list, n);
				free(list);
				free_strs(*contents, sz);
				free(*contents);
				return -1;
			}

			*contents = temp;
		}

		for(int j=0; j<n; j++)
			(*contents)[sz+j] = list[j];

		free(list);
		sz += n;
	}

	(*contents)[sz] = NULL;
	return sz;
}

/* Create directory $path and all it's parent directories. */
/* On failure errno is set to anything mkdir() may set. */
int create_dir(const char* path, unsigned mode) {
	const size_t bsz = 2048;
	char buf[bsz];
	const char* ptr = path;
	const size_t plen = strlen(path);
	int err;

	while(*ptr && !(*ptr == '/' && !*(ptr+1))) {
		if(!(ptr = strchr(ptr+1, '/')))
			ptr = path+plen;	

		memcpy(buf, path, ptr-path);
		buf[ptr-path] = '\0';
		if(mkdir(buf, mode) < 0) {
			err = errno;
			if(errno == EEXIST) continue;
			return -1;
		}
	}

	/* final directory node already exists. */
	if(err == EEXIST) {
		errno = EEXIST;
		return -1;
	}

	return 0;
}
