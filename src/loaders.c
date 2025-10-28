#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "loaders.h"
#include "def.h"
#include "utils.h"

static const char* whitespace = " \n\t";

static inline bool is_label_char(int c) {
	return isalnum(c) || c == '_' || c == '-';
}

static inline bool is_word_char(int c) {
	return isgraph(c);
}

static inline bool is_escape_char(int c) {
	return isgraph(c);
}

static inline bool is_whitespace(int c) {
	return c == ' ' || c == '\t' || c == '\n';
}

static void fskip_until(FILE* fd, bool(*pred)(int)) {
	int c;
	while((c = fgetc(fd)) != EOF && !pred(c));
	ungetc(c, fd);
}

static int fread_while(FILE* fd, char** value, bool(*pred)(int)) {
    int c;
    size_t sz; 
	size_t cap_value = 16;
	fpos_t pos;
	
	fgetpos(fd, &pos);
	
	if(!(*value = calloc(cap_value, sizeof(char*))))
		return MEM_ERROR;

    for(sz=0; (c = fgetc(fd)) != EOF && pred(c); sz++) {
		if(sz == cap_value-1) {
			char* temp;

			if(!(temp = realloc(*value, (cap_value*=2)*sizeof(char*)))) {
				free(*value);
				fsetpos(fd, &pos);
				return MEM_ERROR;
			}

			*value = temp;
		}

        (*value)[sz] = c; 
	}

    ungetc(c, fd);
	if(sz == 0) return PARSE_ERROR;
    if(ferror(fd)) return FILE_ERROR;
    (*value)[sz] = '\0';
    return 0;
}

static int fread_label(FILE* fd, char** label) {
    int c, result;
	fpos_t pos;

	fgetpos(fd, &pos);
    fskipws(fd, whitespace);
    if((result = fread_while(fd, label, is_label_char)) < 0)
        return result;

    fskipws(fd, whitespace);
    if((c = fgetc(fd)) != ':') {
		fsetpos(fd, &pos);
        free(*label);
        return PARSE_ERROR;
    }

	return 0;
}

static int fread_word(FILE* fd, char** word, int delim) {
	const size_t bsz = 2048;
	char buf[bsz];
	int c;

	fpos_t pos;
	fgetpos(fd, &pos);

	size_t i=0;
	for(i=0; i<bsz; i++) {
		c = fgetc(fd);
		if(c == delim || c == EOF) { 
			ungetc(c, fd);
			break;
		}

		if(c == '\\') {
			c = fgetc(fd);
			if(!is_escape_char(c)) {
				fsetpos(fd, &pos);
				return PARSE_ERROR;
			}
		}

		if(!is_word_char(c)) {
			ungetc(c, fd);
			break;
		}
			
		buf[i] = c;
	}

	if(i == 0 || i > MAX_WORD) {
		fsetpos(fd, &pos);
		return PARSE_ERROR;
	}

	buf[i] = '\0';
	if(!(*word = strdup(buf))) {
		fsetpos(fd, &pos);
		return MEM_ERROR;
	}

	return 0;
}

static bool is_graph_str(const char* str) {
	for(; *str; str++)
		if(!isgraph(*str))
			return 0;

	return 1;
}

static bool is_print_str(const char* str) {
	for(; *str; str++)
		if(!isprint(*str))
			return 0;

	return 1;
}

static int fread_words(FILE* fd, Word** words, size_t* sz, int delim) {
	int result, c;
    size_t cap_words = 32;
	
    *sz = 0;
	if(!(*words = calloc(cap_words, sizeof(Word))))
		return MEM_ERROR;

	while(true) {
        fskipws(fd, whitespace);
		c = fgetc(fd);
		if(c == delim) break;
		if(c == EOF) {
			free_words(*words, *sz);
			return PARSE_ERROR;
		}

		ungetc(c, fd);

		if(*sz == cap_words) {
            void* temp;

			if(!(temp = realloc(*words, (cap_words*=2)*sizeof(Word)))) {
			    free_words(*words, *sz);
                return MEM_ERROR;
            }

            *words = temp;
		}

		if((result = fread_word(fd, &(*words)[*sz].str, delim)) != 0) {
			if(result == MEM_ERROR) {
			    free_words(*words, *sz);
                return MEM_ERROR;
			}

			fskip_until(fd, is_whitespace);
			continue;
		}

		(*words)[*sz].len = strlen((*words)[*sz].str);
		(*sz)++;
	}
	
	return 0;
}

int load_dictionary(FILE* fd, Dictionary* dict) {
	return fread_words(fd, &dict->words, &dict->sz, EOF);
}

static int load_quote_text(FILE* fd, Quote* quote) {
	int result, c;

	fskipws(fd, whitespace);
	if((c = fgetc(fd)) != '{') {
		ungetc(c, fd);
		return PARSE_ERROR;
	}

	if((result = fread_words(fd, &quote->text, &quote->sz, '}')) < 0)
		return result;

	return 0;
}

static int load_quote_labels(FILE* fd, Quote* quote) {
    static char* quote_labels[] = { "author", "source", NULL };
    char** quote_vals[] = { &quote->author, &quote->source };
    const size_t bsz = 2048;
    char buf[bsz];
    fpos_t pos;

    quote->author = NULL;
    quote->source = NULL;
    while(true) {
        int i, blen;
        char* label;

	    fskipws(fd, whitespace);
        if(fread_label(fd, &label) < 0)
            break;

		fskipws(fd, whitespace);
        if((i = find_stri(quote_labels, label)) < 0
		|| !fgets(buf, bsz, fd)
		|| (blen = strlen(buf)) == 1) {
            free(quote->author);
            free(quote->source);
            free(label);
            return PARSE_ERROR;
        }

        free(label);
		buf[blen-1] = '\0'; /* remove newline */

		if(!is_print_str(buf)) {
            free(quote->author);
            free(quote->source);
            return PARSE_ERROR;
		}

        if(*quote_vals[i]) free(*quote_vals[i]);
        if(!(*quote_vals[i] = strdup(buf))) {
            free(quote->author);
            free(quote->source);
            return MEM_ERROR;
        }
    }

    return 0;
}

static void find_end_of_quote(FILE* fd) {
	int c;
	while((c = fgetc(fd)) != EOF && c != '}')
		if(c == '\\') (void)fgetc(fd);
}

int load_quotes(FILE* fd, Quotes* quotes) {
	int ret;
	size_t cap_quotes = 32;

	if(!(quotes->quotes = calloc(cap_quotes, sizeof(Quote))))
		return MEM_ERROR;

	quotes->sz = 0;
	while(true) {
        int c;

		fskipws(fd, whitespace);
        if((c = fgetc(fd)) == EOF) break; 
        ungetc(c, fd);

		if(quotes->sz == cap_quotes) {
            void* temp;

			if(!(temp = realloc(quotes->quotes, (cap_quotes*=2)*sizeof(Quote)))) {
                free_quotes(quotes);
                free(quotes->quotes);
                return MEM_ERROR;
            }

            quotes->quotes = temp;
        }

		Quote* const curr_quote = &quotes->quotes[quotes->sz];
        if((ret = load_quote_labels(fd, curr_quote)) < 0) {
			find_end_of_quote(fd);
			continue;
        }

		if((ret = load_quote_text(fd, curr_quote)) < 0) {
			free(curr_quote->author);
			free(curr_quote->source);
			find_end_of_quote(fd);
			continue;
		}

		quotes->sz++;
	}
	
	return 0;
}

int fread_hist_text(FILE* fd, Word** text) {
	size_t cap_text = 64;
	size_t bsz = 256;
	char* word = malloc(bsz);

	if(!word) return -1;
	if(!(*text = calloc(cap_text, sizeof(Word)))) {
		free(word);
		return -1;
	}

	fskipws(fd, whitespace);
	/* ascii-STX (start of text) */
	if(fgetc(fd) != '\002') {
		free(word);
		free(*text);
		return -1;
	}

	size_t i=0;
	for(; ; i++) {
		int c;

		/* ascii-ETX (end of text) */
		if((c = fgetc(fd)) == '\003') break;
		ungetc(c, fd);
		if(i == cap_text) {
			Word* temp;
			if(!(temp = realloc(*text, (cap_text*=2)*sizeof(Word)))) {
				free(word);	
				free_words(*text, i);
				return -1;
			}

			*text = temp;
		}

		if(getdelim(&word, &bsz, 0, fd) < 0) {
			free(word);	
			free_words(*text, i);
			return -1;
		}

		if(!is_print_str(word)
		|| !((*text)[i].str = strdup(word))) {
			free(word);
			free_words(*text, i);
			return -1;
		}

		(*text)[i].len = strlen(word);
	}

	free(word);
	return i;
}

int load_hist(FILE* fd, History* h) {
	const size_t bsz = 1024;
	char buf[bsz];
	const int cap = 64;

	if(!(h->stats = calloc(cap, sizeof(NameVal))))
		return -1;

	h->sz_stats=0;
	for(; ; h->sz_stats++) {
		char c, *label;

		fskipws(fd, whitespace);
		c = fgetc(fd);
		ungetc(c, fd);
		if(c == EOF || c == STX) break;

		if(h->sz_stats == cap-1) {
			free_namevals(h->stats, h->sz_stats);
			free(h->stats);
			return -1;
		}

		if((fread_label(fd, &label)) < 0) {
			free_namevals(h->stats, h->sz_stats);
			free(h->stats);
			return -1;
		}

		fskipws(fd, " \t");
		if(get_delims(fd, buf, bsz, "\n") < 0
		|| strlen(buf) <= 1) {
			free_namevals(h->stats, h->sz_stats);
			free(h->stats);
			free(label);
			return -1;
		}

		buf[strlen(buf)-1] = '\0';
		if(!is_print_str(buf) 
		|| !(h->stats[h->sz_stats].val = strdup(buf))) {
			free_namevals(h->stats, h->sz_stats);
			free(h->stats);
			free(label);
			return -1;
		}

		h->stats[h->sz_stats].name = label;
	}

	memset(&h->stats[h->sz_stats], 0, sizeof(NameVal));

	if((h->nwords = fread_hist_text(fd, &h->text)) < 0) {
		free_namevals(h->stats, h->sz_stats);
		free(h->stats);
		return -1;
	}

	if((h->nmatches = fread_hist_text(fd, &h->matches)) < 0) {
		free_words(h->text, h->nwords);
		free_namevals(h->stats, h->sz_stats);
		free(h->stats);
		return -1;
	}

	if(h->nwords < h->nmatches) {
		free_words(h->text, h->nwords);
		free_words(h->matches, h->nmatches);
		free_namevals(h->stats, h->sz_stats);
		free(h->stats);
		return -1;
	}

	return 0;
}
