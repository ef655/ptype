#define _POSIX_C_SOURCE 200809L

#include <ncurses.h>
#include <stddef.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

#include "def.h"
#include "utils.h"
#include "confsetters.h"

extern Config config;
extern ColorMap color_map;
extern char* ansi_colors[];
extern char* mode_strs[];
extern char** dict_files;
extern char** quotes_files;

static char* strs_on[]  = {"on", "yes", "true", NULL};
static char* strs_off[] = {"off", "no", "false", NULL};
static const char* whitespace = " \t\n";

static int confopt_toggle(const char* value, bool* var) {
	int ret;

	if((ret = find_stri(strs_on, value)) >= 0) {
		*var = true;
		return 0;
	}
	
	if((ret = find_stri(strs_off, value)) >= 0) {
		*var = false;
		return 0;
	}

	return -1;
}

static int confopt_range(const char* value, int* var, int minimum, int maximum) {
	if(!isdigit(value[0]))
		return -1;

	long val = strtol(value, NULL, 10);
	if(val == LONG_MAX && errno == ERANGE)
		return -1;
	
	*var = max(min(val, maximum), minimum);

	return 0;
}

static int read_word(const char* value, char* buf, size_t sz) {
    const char* ptr = value;
    size_t i=0;

    ptr += skipws(ptr, whitespace);
    for(; i < sz && isalnum(ptr[i]); i++)
        buf[i] = ptr[i];
    
    if(i == sz)
        return -1;

    buf[i] = '\0';
    ptr += i;
    return ptr - value;
}

static bool is_ansi_color(const char* str) {
    for(size_t i = 0; i < NUM_ANSI_COLORS; i++)
        if(streqi(str, ansi_colors[i])) return 1;
  
    return 0;
}

static int read_color(const char* value, char** color) {
    int ret;
    const size_t bsz = 32;
    char buf[bsz];
    const char* ptr = value;
    RGB rgb;

    if((ret = read_word(ptr, buf, bsz)) < 0)
        return -1;

    ptr += ret;
    if(is_ansi_color(buf) || to_rgb(buf, &rgb) == 0 
    || streqi(buf, "Default") || streqi(buf, "None"))
       *color = strdup(buf);

    else return -1;
    
    return ptr - value;
}

static int read_attr(const char* value, int* attr) {
    static const struct {char* str; unsigned attr;} attr_strs[] = {
        { "Bold",      A_BOLD      },
        { "Underline", A_UNDERLINE },
        { "Standout",  A_STANDOUT  },
        { "Blink",     A_BLINK     },
        { "Dim",       A_DIM       },
        { "Reverse",   A_REVERSE   },
        { "Invisible", A_INVIS     },
#ifdef A_ITALIC
        { "Italic",    A_ITALIC    },
#else
		{ "Italic",    A_UNDERLINE },
#endif
        { NULL,        0           }
    };

    int ret;
    const size_t bsz = 32;
    char buf[bsz];
    const char* ptr = value;
    
    if((ret = read_word(ptr, buf, bsz)) < 0)
        return -1;

    ptr += ret;

    for(size_t i=0; ; i++) {
        if(!attr_strs[i].str) return -1;
        if(streqi(attr_strs[i].str, buf)) {
            *attr = attr_strs[i].attr;
            break;
        }
    }

    return ptr - value;
}

static int confopt_set_attrs(const char* value, ConfigAttr* attr) {
    const char* ptr = value;
    char* color;
    int ret;
    ConfigAttr a;

    a.attrs = 0;
    if((ret = read_color(ptr, &color)) < 0) {
        return -1;
    }

    a.fg = color;
    ptr += ret;
    ptr += skipws(ptr, whitespace);
    if(!*ptr) {
        a.bg = strdup("None");
		*attr = a;
        return 0;
    }

    if(*ptr++ != ',') return -1;
    
    if((ret = read_color(ptr, &color)) < 0) {
        free(a.fg);
        return -1;
    }

    a.bg = color;
    ptr += ret;
    ptr += skipws(ptr, whitespace);
    if(!*ptr) { 
		*attr = a;
		return 0;
	}

    if(*ptr++ != ',') {
        free(a.fg);
        free(a.bg);
        return -1;
    }

    while(1) {
        int at;

        if((ret = read_attr(ptr, &at)) < 0) {
            free(a.fg);
            free(a.bg);
            return -1;
        }
        
        a.attrs |= at;
        ptr += ret;
        ptr += skipws(ptr, whitespace);
        if(!*ptr) break;
        if(*ptr++ != ',') return -1;
    }

	*attr = a;
	return 0;
}

static int confopt_colors(const char* value) {
	return confopt_toggle(value, &config.colors_enabled);
}

static int confopt_color_border(const char* value) {
	return confopt_set_attrs(value, &config.attrs[CA_BORDER]);
}

static int confopt_color_text(const char* value) {
	return confopt_set_attrs(value, &config.attrs[CA_TEXT]);
}

static int confopt_color_error(const char* value) {
	return confopt_set_attrs(value, &config.attrs[CA_ERROR]);
}

static int confopt_color_typed(const char* value) {
	return confopt_set_attrs(value, &config.attrs[CA_TYPED]);
}

static int confopt_color_selected(const char* value) {
	return confopt_set_attrs(value, &config.attrs[CA_SELECTED]);
}

static int confopt_color_screen(const char* value) {
    if(confopt_set_attrs(value, &config.attrs[CA_SCREEN]) == -1)
        return -1;

    if(!streqi(config.attrs[CA_SCREEN].fg, "Default")) {
        free(config.attrs[CA_SCREEN].fg);
        config.attrs[CA_SCREEN].fg = strdup("Default");
    }

    if(streqi(config.attrs[CA_SCREEN].bg, "None")) {
        free(config.attrs[CA_SCREEN].bg);
        config.attrs[CA_SCREEN].bg = strdup("Default");
    }

    return 0;
}

static int confopt_color_window(const char* value) {
    if(confopt_set_attrs(value, &config.attrs[CA_WINDOW]) == -1)
        return -1;

    if(streqi(config.attrs[CA_WINDOW].fg, "None")) {
        free(config.attrs[CA_WINDOW].fg);
        config.attrs[CA_WINDOW].bg = strdup("Default");
    }

    if(streqi(config.attrs[CA_WINDOW].bg, "None")) {
        free(config.attrs[CA_WINDOW].bg);
        config.attrs[CA_WINDOW].bg = strdup("Default");
    }

    return 0;
}

static int confopt_dict(const char* value) {
	int ret;

	if((ret = find_str(dict_files, value)) < 0)
		return -1;
	
	config.idict = ret;

	return 0;
}

static int confopt_ideath(const char* value) {
	return confopt_toggle(value, &config.ideath);
}

static int confopt_border(const char* value) {
    return confopt_toggle(value, &config.border);
}

static int confopt_start_screen(const char* value) {
	return confopt_toggle(value, &config.start_screen);
}

static int confopt_text_window_height(const char* value) {
	return confopt_range(value, &config.main_height, MIN_MAIN_HEIGHT, MAX_MAIN_HEIGHT);
}

static int confopt_text_window_width(const char* value) {
	return confopt_range(value, &config.main_width, MIN_MAIN_WIDTH, MAX_MAIN_WIDTH);
}

static int confopt_timer(const char* value) {
	return confopt_range(value, &config.timer, MIN_TIMER, MAX_TIMER);
}

static int confopt_words(const char* value) {
	return confopt_range(value, &config.nwords, MIN_NWORDS, MAX_NWORDS);
}

static int confopt_mode(const char* value) {
	int ret;

	if((ret = find_stri(mode_strs, value)) < 0)
		return -1;
	
	config.main = ret;
	return ret;
}

static int confopt_punctuation(const char* value) {
	return confopt_range(value, &config.punctuation, 0, 100);
}

static int confopt_punct(const char* value, ConfPunct* punct, int type) {
	ConfPunct temp;

	if(strlen(value) >= MAX_STRING_OPT)
		return -1;

	strcpy(temp.str, value);
	if(parse_punct(&temp, type) < 0)
		return -1;

	strcpy(punct->str, temp.str);
	punct->punct = temp.punct;
	punct->sz    = temp.sz;
	punct->valid = true;
	return 0;
}

static int confopt_postfix(const char* value) {
	return confopt_punct(value, &config.postfix, PUNCT_POSTFIX);
}

static int confopt_circumfix(const char* value) {
	return confopt_punct(value, &config.circumfix, PUNCT_CIRCUMFIX);
}

static int confopt_confrange(const char* value, ConfRange* r, int min, int max) {
	ConfRange temp;

	if(strlen(value) >= MAX_STRING_OPT)
		return -1;

	strcpy(temp.str, value);
	if(parse_range(&temp) < 0)
		return -1;

	strcpy(r->str, temp.str);
	r->min = temp.min;
	r->max = temp.max;
	r->valid = true;
	normalize_confrange(r, min, max);

	return 0;
}

static int confopt_word_length(const char* value) {
	return confopt_confrange(value, &config.word_length, 0, MAX_WORD);
}

static int confopt_quote_length(const char* value) {
	return confopt_confrange(value, &config.quote_length, 1, MAX_QUOTE_LENGTH);
}

static int confopt_insert_frequency(const char* value) {
	return confopt_confrange(value, &config.quote_length, 1, 100);
}

static int confopt_digit_strings(const char* value) {
	return confopt_confrange(value, &config.digit_strs, 1, MAX_WORD);
}

static int confopt_word_filter(const char* value) {
	if(strlen(value) >= MAX_STRING_OPT)
		return -1;

	strcpy(config.wfilter.str, value);

	return 0;
}

static int confopt_hist_limit(const char* value) {
	return confopt_range(value, &config.hist_limit, MIN_HIST_LIMIT, MAX_HIST_LIMIT);
}

static int confopt_quotes(const char* value) {
	int ret;

	if((ret = find_str(quotes_files, value)) < 0)
		return -1;

	config.iquote = ret;
	return 0;
}

/* "ConfOptSetter" functions should not mutate their related config variable on failure. */
typedef int(*ConfOptSetter)(const char*);
static struct {char* name; ConfOptSetter func;} confopt_updater_map[] = { 
	{ .name = "mode",                .func = confopt_mode               },
	{ .name = "ideath",              .func = confopt_ideath             },
	{ .name = "timer",               .func = confopt_timer              },
	{ .name = "words",               .func = confopt_words              },
	{ .name = "history_limit",       .func = confopt_hist_limit         },
	{ .name = "punctuation",         .func = confopt_punctuation        },
	{ .name = "word_length",         .func = confopt_word_length        },
	{ .name = "quote_length",        .func = confopt_quote_length       },
	{ .name = "postfix",             .func = confopt_postfix            },
	{ .name = "circumfix",           .func = confopt_circumfix          },
	{ .name = "insert_frequency",    .func = confopt_insert_frequency   },
	{ .name = "digit_strings",       .func = confopt_digit_strings      },
	{ .name = "word_filter",         .func = confopt_word_filter        },
	{ .name = "dictionary",          .func = confopt_dict               },
	{ .name = "quotes",              .func = confopt_quotes             },
	{ .name = "colors",              .func = confopt_colors             },
	{ .name = "text_window_width",   .func = confopt_text_window_width  },
	{ .name = "text_window_height",  .func = confopt_text_window_height },
	{ .name = "border",              .func = confopt_border             },
	{ .name = "start_screen",        .func = confopt_start_screen       },
	{ .name = "color_border",        .func = confopt_color_border       },
	{ .name = "color_text",          .func = confopt_color_text         },
	{ .name = "color_error",         .func = confopt_color_error        },
	{ .name = "color_typed",         .func = confopt_color_typed        },
	{ .name = "color_selected",      .func = confopt_color_selected     },
	{ .name = "color_window",        .func = confopt_color_window       },
	{ .name = "color_screen",        .func = confopt_color_screen       },
	{ .name = NULL,                  .func = NULL                       }
};

int update_config_option(const char* name, const char* value) {
	for(size_t i=0; confopt_updater_map[i].name; i++)
		if(streq(confopt_updater_map[i].name, name))
			return confopt_updater_map[i].func(value);

	return -2;
}
