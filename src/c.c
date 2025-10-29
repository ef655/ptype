#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500

#include <config.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ncurses.h>
#include <panel.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <locale.h>
#include <limits.h>
#include <regex.h>

#include "def.h"
#include "drw.h"
#include "utils.h"
#include "loaders.h"
#include "confsetters.h"
#include "config_parser.h"

enum { ARG_UNSET, ARG_OFF, ARG_ON };

#define VERSION_STR PACKAGE_NAME" "PACKAGE_VERSION
#define PTYPE_OPTIONS "[-vhcQw1]"
#define USAGE_STR "Usage: "PACKAGE_NAME" "PTYPE_OPTIONS
#define HELP_STR                                                       \
	USAGE_STR                                                          \
	"\n"                                                               \
	"Options:\n"                                                       \
	"    -v              Print the version.\n"                         \
	"    -h              Print this message.\n"                        \
	"    -c =on|=off     Turn colors on/off at startup.\n"             \
	"    -Q              If stdin is a pipe read it as a quote file\n" \
	"    -w              Omit warnings from being printed.\n"          \
	"                    and set the starting mode to Quote.\n"        \
	"    -1              Automatically quit after one test.\n"         \

struct {
	int color;
	bool quote;
	bool oneshot;
	bool warnings;
} ptype_args = {
	.color = 0,
	.quote = false,
	.oneshot = false,
	.warnings = true,
};

#define STDIN_NAME "stdin"

#define DEF_ATTR_BORDER   {.fg="Magenta", .bg="None",    .attrs=0}
#define DEF_ATTR_TEXT     {.fg="Default", .bg="None",    .attrs=0}
#define DEF_ATTR_TYPED    {.fg="Green",   .bg="None",    .attrs=0}
#define DEF_ATTR_ERROR    {.fg="Red",     .bg="None",    .attrs=0}
#define DEF_ATTR_SELECTED {.fg="Blue",    .bg="None",    .attrs=0}
#define DEF_ATTR_WINDOW   {.fg="Default", .bg="Default", .attrs=0}
#define DEF_ATTR_SCREEN   {.fg="Default", .bg="Default", .attrs=0}
	
/* The default colors should be ansi colors, in case
 * RGB is not supported on the terminal. */
const ConfigAttr default_attrs[NUM_CONF_ATTRS] = {
    [CA_BORDER]   = DEF_ATTR_BORDER,
	[CA_TEXT]     = DEF_ATTR_TEXT,
    [CA_TYPED]    = DEF_ATTR_TYPED,
    [CA_ERROR]    = DEF_ATTR_ERROR,
    [CA_SELECTED] = DEF_ATTR_SELECTED,
    [CA_WINDOW]   = DEF_ATTR_WINDOW,
    [CA_SCREEN]   = DEF_ATTR_SCREEN,
};

/* .attr's fg and bg fields are statically allocated here, 
 * whereas the config setters dynamically allocate them.
 * Thus, these fields should be freed diligently. */
Config config = {
	.main             = M_NORMAL,
	.timer            = 20,
	.nwords           = 20,
	.ideath           = false,
	.punctuation      = 20,
	.hist_limit       = 25,
    .colors_enabled   = true,
    .start_screen     = true,
    .border           = true,
#if MIN_MAIN_WIDTH <= 55
	.main_width       = 55,
#else
	.main_width       = MIN_MAIN_WIDTH,
#endif
	.main_height      = 5,
	.idict            = 0,
	.iquote           = 0,
	.word_length      = {.str = "-",  .valid = true, .min = -1, .max = -1},
	.quote_length     = {.str = "-",  .valid = true, .min = -1, .max = -1},
	.insert_freq      = 10,
	.digit_strs       = {.str = "-",  .valid = true, .min = -1, .max = -1},
	.wfilter          = {.str = "\0", .valid = false},
	.postfix          = {.str = "\0", .valid = true, .punct = NULL, .sz = 0},
	.circumfix        = {.str = "\0", .valid = true, .punct = NULL, .sz = 0},

    .attrs[CA_BORDER]   = DEF_ATTR_BORDER,
	.attrs[CA_TEXT]     = DEF_ATTR_TEXT,
    .attrs[CA_TYPED]    = DEF_ATTR_TYPED,
    .attrs[CA_ERROR]    = DEF_ATTR_ERROR,
    .attrs[CA_SELECTED] = DEF_ATTR_SELECTED,
    .attrs[CA_WINDOW]   = DEF_ATTR_WINDOW,
    .attrs[CA_SCREEN]   = DEF_ATTR_SCREEN,
};

GlobalAttrs attributes = {
	.border   = 0,
	.text     = 0,
	.typed    = 0,
	.error    = 0,
	.selected = 0,
};

#define PATHS_MAX 8

ColorMap color_map           = {.colors = NULL, .sz = 0};
Dictionary loaded_dict       = {.words  = NULL, .sz = 0};
Dictionary stdin_dict        = {.words  = NULL, .sz = 0}; 
Dictionary filtered_dict     = {.words  = NULL, .sz = 0};
Quotes loaded_quotes         = {.quotes = NULL, .sz = 0};
Quotes stdin_quotes          = {.quotes = NULL, .sz = 0};
Quotes filtered_quotes       = {.quotes = NULL, .sz = 0};
bool colors_started          = false;
int default_color            = 0;
const char* progname         = NULL;
Screen* screens              = NULL;
char* user_config_dir        = NULL;
char* user_state_dir         = NULL;
char* user_hist_dir          = NULL;
char** quotes_files		     = NULL;
char** dict_files		     = NULL;
char* path_quotes[PATHS_MAX] = {NULL};
char* path_dicts[PATHS_MAX]  = {NULL};
char** log_messages          = NULL;
size_t sz_log			     = 0;
bool filtered_dict_valid     = false;
bool filtered_quotes_valid   = false;
char* mode_strs[] = {
	[M_NORMAL] = "Normal",
	[M_TIMED]  = "Timed",
	[M_QUOTE]  = "Quote",
	NULL
};

char* ansi_colors[NUM_ANSI_COLORS] = { 
    "Black",
    "Red", 
    "Green",
    "Yellow",
    "Blue",
    "Magenta",
    "Cyan",
    "White",
};

/* Function Index */

double accuracy(size_t, size_t);
int add_color_map_entry(char*);
int add_to_history(const TypeText* tt, const Stat* st);
double avg_word_len(const TypeText*);
ScreenNum begin_test(void);
void cleanup(void);
int cmp_str_ascend(const void*, const void*);
void cycle_mode(void);
void driver_range(void);
void driver_select(void);
void driver_select_next(void);
void driver_select_prev(void);
void driver_toggle(void);
int errlog(const char*, ...);
void fix_bkgd(void);
void free_text(TypeText*);
void free_quote(Quote*);
void free_quotes(Quotes*);
void free_hist(History*);
int gen_digit_str(char*, int, int);
int gen_from_dict(TypeText*, bool);
int gen_from_quotes(TypeText*);
int gen_insertion(char*);
int gen_timed(TypeText*, int width);
int gen_word(char*, bool);
int gen_text(TypeText*, int);
int get_dict(const char*, Dictionary*);
void get_stats(const TypeText*, Stat*, long);
int get_time_str(char*, size_t);
int get_quotes(const char*, Quotes*);
char* get_user_config_dir(void);
char* get_user_state_dir(void);
void init(void);
int init_color_map(void);
int init_color_pairs(void);
int init_options(Option**);
int init_pair_str(int, const char*, const char*);
int init_screens(void);
int init_stdin_dict(void);
int init_stdin_quote(void);
void init_stat(Stat*);
void init_text(TypeText*);
bool is_filtered_word(const char*);
bool is_filtered_quote(const Quote*);
ScreenNum loop_hist(void);
void loop_hist_stat(void);
ScreenNum loop_main(void);
ScreenNum loop_start(void);
ScreenNum loop_test(void);
ScreenNum loop_opt(void);
void num_chars_typed(const TypeText*, int*, int*);
int optfunc_circumfix(void);
int optfunc_confpunct(ConfPunct*, int);
int optfunc_dict(void);
int optfunc_digit_strs(void);
int optfunc_range(ConfRange*, int, int);
int optfunc_postfix(void);
int optfunc_quote_length(void);
int optfunc_quotes(void);
int optfunc_word_filter(void);
int optfunc_word_length(void);
void opt_first(void);
void opt_last(void);
void opt_next(void);
void opt_prev(void);
void opt_select(void);
void print_log(void);
void pull_word_next(TypeText*, size_t);
void push_word_next(TypeText*, size_t);
int punctuate(char*);
void regen_filtered_dict(void);
void regen_filtered_quotes(void);
int reset_attrs(void);
void revert_rgb_colors(void);
ScreenNum screen_hist(void);
ScreenNum screen_main(void);
ScreenNum screen_stat(void);
ScreenNum screen_start(void);
ScreenNum screen_opt(void);
void sig_nothing(int);
int tt_addch(WINDOW*, TypeText*, int);
void tt_delch(WINDOW*, TypeText*);
void tt_fix_line(TypeText*, int, int);
void tt_fix_all_lines(TypeText*, int);
void tt_init_word(TypeText*, const char*, size_t);
int update_config(const ConfigList*);
int write_hist(FILE*, const TypeText*, const Stat*);
double wpm(size_t, long);

/******************/

/* Return the 'accuracy' as a percentage. */
double accuracy(size_t ntyped, size_t ncorrect) {
	return (double)ncorrect/ntyped * 100;
}

int add_color_map_entry(char* color) {
    RGB rgb;

    /* not an RGB string */
    if(to_rgb(color, &rgb) != 0)
        return 1;
        
    /* already entered */
    if(sfind_stri(color_map.colors, color_map.sz, color) != -1)
        return 0;

    rgb_to_cursrgb(&rgb);
    if(init_color(color_map.sz, rgb.r, rgb.g, rgb.b) == ERR)
        return -1; /* terminal doesn't support changing colors */

    color_map.colors[color_map.sz++] = color;
    return 0;
}

int add_to_history(const TypeText* tt, const Stat* st) {
	FILE* fd;
	const size_t bsz = 1024;
	const size_t tsz = 128;
	char buf[bsz];
	char time[tsz];

	if(!user_hist_dir) return -1;
	get_time_str(time, tsz);
	snprintf(buf, bsz, "%s/%s", user_hist_dir, time);
	if(!(fd = fopen(buf, "w"))) return -1;
	write_hist(fd, tt, st);
	fclose(fd);
	return 0;
}

double avg_word_len(const TypeText* tt) {
	if(tt->curr_word == 0)
		return 0;

	size_t sum = 0;
	for(int i=0; i<tt->curr_word; ++i)
		sum += tt->text[i].len;

	return (float)sum / tt->curr_word;
}

ScreenNum begin_test(void) {
	MainScrData* const data = screens[SCR_MAIN].data;
	Stat* const st = &((StatScrData*)screens[SCR_STAT].data)->st;
	static bool failed_before = false;
	const bool timed = config.main == M_TIMED && config.timer;
	struct timespec time_end;
	ScreenNum scrnum;
    long ms;

    init_stat(st);
	clock_gettime(CLOCK_MONOTONIC, &data->time_start);
	data->test_started = true;
	scrnum = loop_test();
	data->test_started = false;
	clock_gettime(CLOCK_MONOTONIC, &time_end);
    ms = (timed)
		? min(elapsed_ms(&data->time_start, &time_end), config.timer*1e3)
		: elapsed_ms(&data->time_start, &time_end);

    get_stats(&data->tt, st, ms);
	if(add_to_history(&data->tt, st) < 0) {
		/* only write the error once */
		if(!failed_before) errlog("Warning: Failed to write history file");
		failed_before = true;
	}

	return scrnum;
}

void cleanup(void) {
	if(!isendwin()) endwin();
}

int cmp_str_descend(const void* a, const void* b) {
	const char* const* a1 = a;
	const char* const* b1 = b;
	return strcmp(*b1, *a1);
}

void cycle_mode(void) {
	MainScrData* const mdata = screens[SCR_MAIN].data;
	const int nudge = (config.border ? 1 : 0);

	config.main = (config.main+1) % NUM_MODES;
	if(mdata->tt.text) free_text(&mdata->tt);
	gen_text(&mdata->tt, getmaxx(mdata->win_text)-nudge*2);
}

void driver_range(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	OptRange* const opt = &data->options[data->opt_idx].range;
	int c, save=*opt->val;

	redraw_opt();
	while((c = wgetch(data->win_opt)) != ERR) {
		switch(c) {
			case ESC: case ' ': 
				if(opt->func && opt->func() != 0)
					*opt->val = save;
				return;
			case KEY_DOWN: case 'j':
				*opt->val = max(*opt->val-1, opt->rmin);
				break;
			case KEY_UP: case 'k':
				*opt->val = min(*opt->val+1, opt->rmax);
				break;
			case 'g':
				*opt->val = opt->rmin;
				break;
			case 'G':
				*opt->val = opt->rmax;
				break;
			case KEY_RESIZE:
				fix_bkgd();
				break;
			default:
				if(keyname_cmp(c, "^J"))
					*opt->val = max(*opt->val-5, opt->rmin);
				else if(keyname_cmp(c, "^K"))
					*opt->val = min(*opt->val+5, opt->rmax);

		}

		redraw_opt();
	}
}

void driver_select_next(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	OptSelect* const opt = &data->options[data->opt_idx].select;
	int save = *opt->selected;
		
	redraw_opt();
	while(true) {
	 	*opt->selected = (*opt->selected == 0) ? opt->len_list-1 : *opt->selected-1;

		if(opt->func) {
			if(*opt->selected == save || opt->func() == 0)
				return;
        }

		else return;
	}
}

void driver_select_prev(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	OptSelect* const opt = &data->options[data->opt_idx].select;

	int save = *opt->selected;
	while(true) {
		*opt->selected = (*opt->selected+1) % opt->len_list;
		if(opt->func) {
			if(*opt->selected == save || opt->func() == 0)
				return; 
		}
		else return;
	}

}

void driver_select(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	OptSelect* const opt = &data->options[data->opt_idx].select;
	int c;

	if(opt->len_list <= 1) return;
	redraw_opt();
	while((c = wgetch(data->win_opt)) != ERR) {
		switch(c) {
			case ESC: case ' ':
				return;
			case KEY_DOWN: case 'j':
				driver_select_prev();
				break;
			case KEY_UP: case 'k':
				driver_select_next();
				break;
			case KEY_RESIZE:
				fix_bkgd();
				break;
		}

		redraw_opt();
	}
}

void driver_string(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	OptString* const opt = &data->options[data->opt_idx].string;
	int c;

	opt->pos = strlen(opt->val);

	redraw_opt();
	while((c = wgetch(data->win_opt))) {
		switch(c) {
			case ESC:
				*opt->valid = (opt->func && opt->func() == 0);
				return;
			case KEY_LEFT:
				if(opt->pos != 0)
					opt->pos--;
				break;
			case KEY_RIGHT:
				if(opt->pos != (int)strlen(opt->val))
					opt->pos++;
				break;
			case KEY_DOWN:
				opt->pos = 0;
				break;
			case KEY_UP:
				opt->pos = strlen(opt->val);
				break;
			case KEY_RESIZE:
				fix_bkgd();
				break;
			default:
				if((c == KEY_BACKSPACE || keyname_cmp(c, "^?") || keyname_cmp(c, "^H"))
				&& opt->pos) {
					int i=opt->pos;
					do opt->val[i-1] = opt->val[i]; 
					while(opt->val[i++]);
					opt->pos--;
				}

				else if(keyname_cmp(c, "^W")) {
					int i=opt->pos;
					do opt->val[i-opt->pos] = opt->val[i]; 
					while(opt->val[i++]);
					opt->pos = 0;
				}

				else if(isprint(c) && strlen(opt->val) != MAX_STRING_OPT-1) {
					for(int i=strlen(opt->val); i>=opt->pos; i--)
						opt->val[i+1] = opt->val[i];

					if(opt->val[opt->pos] == '\0')
						opt->val[opt->pos+1] = '\0';

					opt->val[opt->pos++] = c;
				}
		}

		redraw_opt();
	}
}

void driver_toggle(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	OptToggle* const opt = &data->options[data->opt_idx].toggle;
	*opt->toggle = !(*opt->toggle);

    if(opt->func) opt->func();

    redraw_opt();
}

int errlog(const char* fmt, ...) {
	static size_t cap_log = 8;
	const size_t bsz = 2048;
	char buf[bsz];
	char buf2[bsz];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, bsz, fmt, ap);
	va_end(ap);

	/* The log should be updated to distinguish between errors and warnings properly */
	if(!ptype_args.warnings && strncmp("Warning", buf, 7) == 0)
		return 0;

	if(!log_messages || sz_log == cap_log)
		log_messages = ereallocarray(log_messages, cap_log*=2, sizeof(char*));

	snprintf(buf2, bsz, "%s: %s", progname, buf);
	if(!(log_messages[sz_log++] = strdup(buf2)))
		exit(1);

	return 0;
}

void fix_bkgd(void) {
	clear();
}

/* free a previously initialized TypeText structure and its members */
void free_text(TypeText* tt) {
	free_words(tt->text, tt->nwords);
	free_words(tt->matches, tt->nwords);
	free(tt->lines);
	memset(tt, 0, sizeof(TypeText)); /* set pointers to NULL. */
}

void free_puncts(Punctuation* punct, size_t sz) {
	for(size_t i=0; i<sz; i++)
		free(punct[i].str);
}

void free_hist(History* hist) {
	free_words(hist->text, hist->nwords);
	free_words(hist->matches, hist->nmatches);
	free_namevals(hist->stats, hist->sz_stats);
	free(hist->stats);
}

/* $tt should be freed and set to NULL before calling this function
 * unless $append is true */
int gen_from_dict(TypeText* tt, bool append) {
	const int initial_timed = 64;
	char buf[MAX_WORD+1];
	const int start = tt->nwords;

	if(!filtered_dict_valid) {
		regen_filtered_dict();
		filtered_dict_valid = true;
	}

	if(!append || !tt->text) {
		tt->nwords = (append) ? initial_timed : config.nwords;
		tt->text = ecalloc(tt->nwords, sizeof(Word));
		tt->matches = ecalloc(tt->nwords, sizeof(Word));
	}

	else if(append) {
		tt->text = ereallocarray(tt->text, tt->nwords*=2, sizeof(Word));
		tt->matches = ereallocarray(tt->matches, tt->nwords, sizeof(Word));
	}

	for(int i=start; i<tt->nwords; i++) {
		bool capitalize = config.punctuation && i>0 
			&& strchr("?.!", tt->text[i-1].str[tt->text[i-1].len-1]);

		if(gen_word(buf, capitalize) < 0) {
			free_text(tt);
			return -1;
		}

		tt_init_word(tt, buf, i);
	}

	return 0;
}

/* $tt should be freed and set to NULL before calling this function */
int gen_from_quotes(TypeText* tt) {
	if(!filtered_quotes_valid) {
		regen_filtered_quotes();
		filtered_quotes_valid = true;
	}

	if(filtered_quotes.sz == 0)
		return -1;
	
	const Quote* const quote = &filtered_quotes.quotes[rand() % filtered_quotes.sz];

	tt->text = ecalloc(quote->sz, sizeof(Word));
	tt->matches = ecalloc(quote->sz, sizeof(Word));
	for(size_t i=0; i<quote->sz; i++)
		tt_init_word(tt, quote->text[i].str, i);

	tt->nwords = quote->sz;
	tt->author = quote->author;
	tt->source = quote->source;

	return 0;
}

int gen_insertion(char* buf) {
	ConfRange* const dr = &config.digit_strs;

	if(is_range_off(dr) || gen_digit_str(buf, dr->min, dr->max) < 0)
		return -1;

	return 0;
}

/* Used to initialize text for timed mode and generate text dynamically
 * during the test whenever text stops filling the text window */
int gen_timed(TypeText* tt, int width) {
	while(tt->nlines-tt->curr_line < config.main_height+1) {
		const size_t start = tt->nwords;

		if(gen_from_dict(tt, true) < 0)
			return -1;

		for(int i = start; i<tt->nwords; i++)
			tt->lines[tt->nlines-1].len += tt->text[i].len + 1;
		
		tt_fix_line(tt, tt->nlines-1, width);
	}

	return 0;
}

/* $buf's size is assumed to be greater than max */
int gen_digit_str(char* buf, int min, int max) {
	if(max < min || min < 0) return -1;

	const int len = (rand() % (max+1 - min)) + min;
	for(int i=0; i<len; i++)
		buf[i] = '0' + (rand() % 10);

	buf[len] = 0;
	return 0;
}

/* Randomly generate a word from the filtered dictionary
 * $buf should be at least of size MAX_WORD+1 */
int gen_word(char* buf, bool force_capitalize) {		
	ConfRange* const dr = &config.digit_strs;

	if((filtered_dict.sz == 0 || (rand() % 100) < config.insert_freq) && gen_insertion(buf) == 0);
	else {
		if(filtered_dict.sz == 0) return -1;
		strcpy(buf, filtered_dict.words[rand() % filtered_dict.sz].str);
		if(force_capitalize) buf[0] = toupper(buf[0]);
	}

	if((rand() % 100) < config.punctuation)
		punctuate(buf);

	return 0;
}

/* Generate text to be typed for test.
 * $tt is reallocated; any memory previously allocated
 * for $tt should be freed before calling this function. 
 * On failure $tt is zero'd. */
int gen_text(TypeText* tt, int width) {
    init_text(tt);

	switch(config.main) {
	case M_TIMED: return gen_timed(tt, width);
	case M_NORMAL:
		if(gen_from_dict(tt, false) < 0)
			return -1;

		break;
	case M_QUOTE:
		if(gen_from_quotes(tt) < 0)
			return -1;
	}
	
	/* all words pushed on to the first line. */
	for(int i=0; i<tt->nwords; i++)
		tt->lines[0].len += tt->text[i].len + 1;

	tt_fix_line(tt, 0, width);
	return 0;
}

void get_stats(const TypeText* tt, Stat* st, long elapsed_ms) {
    st->elapsed_ms = elapsed_ms;
    num_chars_typed(tt, &st->ntyped, &st->ncorrect);
    st->wtyped = tt->curr_word;
    st->wpm = wpm(st->ncorrect, st->elapsed_ms);
    st->acc = accuracy(st->raw_typed, st->raw_correct);
    st->awl = avg_word_len(tt);
    st->author = tt->author;
    st->source = tt->source;
}

int get_time_str(char* buf, size_t sz) {
	const size_t bsz = 128;
	char bbuf[bsz];
	struct timeval tv;

	gettimeofday(&tv, NULL);
	struct tm* t = localtime(&tv.tv_sec); 
	strftime(bbuf, bsz, "%Y-%m-%d-%H:%M:%S", t);
	return snprintf(buf, sz, "%s,%03lu", bbuf, tv.tv_usec/1000);
}

char* get_user_config_dir(void) {
	const size_t bsz = 2048;
	char buf[bsz];
	char* var;

	/* use $XDG_CONFIG_HOME if defined, otherwise use $HOME. */
	if((var = getenv("XDG_CONFIG_HOME"))) {
		sprintf(buf, "%s/%s", var, DIR_USER_CONFIG);
		return strdup(buf);
	}
	
	if((var = getenv("HOME"))) {
		sprintf(buf, "%s/.config/"DIR_USER_CONFIG, var);
		return strdup(buf);
	}
	
	return NULL;
}

char* get_user_state_dir(void) {
	const size_t bsz = 2048;
	char buf[bsz];
	char* var;

	if((var = getenv("XDG_STATE_HOME"))) {
		sprintf(buf, "%s/"DIR_STATE"/"DIR_HISTORY, var);
		return strdup(buf);
	}

	if((var = getenv("HOME"))) {
		sprintf(buf, "%s/.local/state/"DIR_STATE, var);
		return strdup(buf);
	}

	return NULL; 
}

/* Generic initalization of program. */
void init(void) {
	int result;
	FILE* config_file;
	char** config_errors;
	char* user_dicts_dir = NULL, *user_quotes_dir = NULL;
	char* path_config[PATHS_MAX];
	int pd_len=0, pq_len=0, pc_len=0;
	ConfigList conflist;

	srand(time(NULL));
	setlocale(LC_ALL, "");
	atexit(print_log);

	if((user_config_dir = get_user_config_dir())) {
		user_dicts_dir  = ecalloc(strlen(user_config_dir) + strlen(DIR_DICTS) + 2, sizeof(char));
		user_quotes_dir = ecalloc(strlen(user_config_dir) + strlen(DIR_QUOTES) + 2, sizeof(char));
		sprintf(user_dicts_dir,  "%s/%s", user_config_dir, DIR_DICTS);
		sprintf(user_quotes_dir, "%s/%s", user_config_dir, DIR_QUOTES);

		path_config[pc_len++] = strdup(user_config_dir);
		path_dicts[pd_len++]  = strdup(user_dicts_dir);
		path_quotes[pq_len++] = strdup(user_quotes_dir);
	} else errlog("Warning: Failed to find user config directory!"
			      " (ensure the $HOME or $XDG_CONFIG_HOME environment variable is set)");

	path_config[pc_len++] = strdup(SYSTEM_CONFIG);
	path_dicts[pd_len++]  = strdup(SYSTEM_CONFIG"/"DIR_DICTS);
	path_quotes[pq_len++] = strdup(SYSTEM_CONFIG"/"DIR_QUOTES);

	path_config[pc_len++] = NULL;
	path_dicts[pd_len++]  = NULL;
	path_quotes[pq_len++] = NULL;
	
	if((user_state_dir = get_user_state_dir())) {
		user_hist_dir = ecalloc(strlen(user_state_dir) + strlen(DIR_HISTORY) + 2,
			                    sizeof(char));
		sprintf(user_hist_dir, "%s/%s", user_state_dir,  DIR_HISTORY);
	} else {
		errlog("Warning: Failed to find user state directory!"
			   " (ensure the $HOME or $XDG_STATE_HOME environment variable is set)");
		user_hist_dir = NULL;
	}

	if(dirs_contents(path_dicts, &dict_files, 0) < 0) {
		errlog("Warning: Failed to read all dictionary directories");
		dict_files = strstr_dup((char**){NULL});
	}

	strstr_remdup(dict_files);
	if((result = find_str(dict_files, STDIN_NAME)) != -1) {
		errlog("Warning: Forbidden dictionary name 'stdin' found; ignoring it!");
		strstr_remove(dict_files, result);
	}

	free(user_dicts_dir);
	if(dirs_contents(path_quotes, &quotes_files, 0) < 0) {
		errlog("Warning: Failed to read all quotes directories");
		quotes_files = strstr_dup((char**){NULL});
	}

	if((result = find_str(quotes_files, STDIN_NAME)) != -1) {
		errlog("Warning: Forbidden quotes file name 'stdin' found; ignoring it!");
		strstr_remove(quotes_files, result);
	}

	strstr_remdup(quotes_files);
	free(user_quotes_dir);
	if(user_hist_dir)
		if(create_dir(user_hist_dir, 0755) < 0)
			if(errno != EEXIST) errlog("Warning: Failed to create history directory");

	if((config_file = path_fopen(path_config, FILE_CONFIG))) {
		result = read_config(config_file, &conflist, &config_errors);
		fclose(config_file);
		if(result == 0) {
			for(char** ptr=config_errors; *ptr; ptr++) {
				errlog("Warning: %s:%s!", FILE_CONFIG, *ptr);
				free(*ptr);
			}

			free(config_errors);

			if(update_config(&conflist) < 0)
				errlog("Warning: Bad config file; using default config!");

			free_config(&conflist);
		}

		else errlog("Warning: Insufficient memory to load config file; using default config!");
	}

	else errlog("Warning: Unable to open config file; using default config!");

	free_strs(path_config, pc_len);

	/* command line argument configuration overrides */

	if(ptype_args.color) 
		config.colors_enabled = ptype_args.color == ARG_ON ? true : false;

	if(ptype_args.quote)
		config.main = M_QUOTE;

	if(ptype_args.oneshot)
		config.start_screen = false;

	/* */

	if(!isatty(STDIN_FILENO)) {
		const char* tty;

		if(!ptype_args.quote) {
			if(init_stdin_dict() < 0)
				errlog("Warning: Failed to read dictionary from stdin; ignoring it!");
			else config.idict = find_str(dict_files, STDIN_NAME);
		}

		else {
			if(init_stdin_quote() < 0)
				errlog("Warning: Failed to read quotes from stdin; ignoring it!");
			else config.iquote = find_str(quotes_files, STDIN_NAME);
		}

		if(!(tty = ttyname(STDOUT_FILENO))
		|| !freopen(tty, "r", stdin)) {
			errlog("Error: Failed to reset stdin to controlling terminal"
			       " after reading from pipe!");
			exit(1);
		}
	}

	if(*dict_files) {
		if(!streq(dict_files[config.idict], STDIN_NAME)) {
			if(get_dict(dict_files[config.idict], &loaded_dict) != 0) {
				errlog("Warning: Failed to load selected dictionary!");
				strstr_remove(dict_files, config.idict);
				for(config.idict=0; dict_files[0];) {
					if(get_dict(dict_files[0], &loaded_dict) == 0)
						break;

					strstr_remove(dict_files, 0);
				}

				if(!dict_files[0])
					loaded_dict = (Dictionary){.words=NULL, .sz=0};
			}
		} else loaded_dict = stdin_dict;
	}

	if(*quotes_files) {
		if(!streq(quotes_files[config.iquote], STDIN_NAME)) {
			if(get_quotes(quotes_files[config.iquote], &loaded_quotes) != 0) {
				errlog("Warning: Failed to load selected quotes!");
				strstr_remove(quotes_files, config.iquote);
				for(config.iquote=0; quotes_files[0];) {
					if(get_quotes(quotes_files[0], &loaded_quotes) == 0)
						break;

					strstr_remove(quotes_files, 0);
				}

				if(!quotes_files[0])
					loaded_quotes = (Quotes){.quotes=NULL, .sz=0};
			}
		} else loaded_quotes = stdin_quotes;
	}

	initscr(); 
	atexit(cleanup);
	cbreak(); noecho(); nonl();
	curs_set(0); set_escdelay(0); 
	if(ptype_args.color != ARG_OFF) {
		if(has_colors() && start_color() == OK) {
			colors_started = true;
			default_color = (use_default_colors() == OK) ? -1 : 0;
    	    if(init_color_map() < 0) {
    	        errlog("Warning: Config requests RGB colors,"
					   " but the terminal does not support them;"
					   " reverting RGB colors to defaults!");

				revert_rgb_colors();
			}

    	    if(init_color_pairs() < 0)
				errlog("Warning: Failed to properly initialize colors");
		} 
	}
	
    reset_attrs();
	init_screens();
}


int init_color_map(void) {
    const size_t colormap_capacity = 32;

    color_map.colors = ecalloc(colormap_capacity, sizeof(char*));
    color_map.sz = 0;
    for(; color_map.sz < NUM_ANSI_COLORS; color_map.sz++)
        color_map.colors[color_map.sz] = ansi_colors[color_map.sz];

    if(!can_change_color()) {
        /* only ansi colors are available */
		revert_rgb_colors();
        return 0;
	}

    for(size_t i=0; i < NUM_CONF_ATTRS; i++)
        if(add_color_map_entry(config.attrs[i].fg) < 0
        || add_color_map_entry(config.attrs[i].bg) < 0)
            return -1;

    return 0;
}

int color_num_from_str(const char* str, int* color) {
    int ret;

    if(streqi(str, "Default")) *color = default_color;
    else if(streqi(str, "None")) *color = -2;
    else if((ret = sfind_stri(color_map.colors, color_map.sz, str)) >= 0)
        *color = ret;
    else return -1;

    return 0;
}

int init_pair_ca(int num, ConfigAttr* ca) {
    int fg, bg;

    if((color_num_from_str(ca->fg, &fg)) < 0
    || (color_num_from_str(ca->bg, &bg)) < 0)
        return -1;

    if(fg == -2 && color_num_from_str(config.attrs[CA_WINDOW].fg, &fg) < 0)
        return -1;

    if(bg == -2 && color_num_from_str(config.attrs[CA_WINDOW].bg, &bg) < 0)
        return -1;

    if(init_pair(num, fg, bg) == ERR)
		return -1;

    return 0;
}

int init_color_pairs(void) {
	if(init_pair_ca(CP_BORDER,   &config.attrs[CA_BORDER])   < 0
    || init_pair_ca(CP_TEXT,     &config.attrs[CA_TEXT])     < 0
    || init_pair_ca(CP_TYPED,    &config.attrs[CA_TYPED])    < 0
    || init_pair_ca(CP_ERROR,    &config.attrs[CA_ERROR])    < 0
    || init_pair_ca(CP_SELECTED, &config.attrs[CA_SELECTED]) < 0
    || init_pair_ca(CP_SCREEN,   &config.attrs[CA_SCREEN])   < 0
    || init_pair_ca(CP_WINDOW,   &config.attrs[CA_WINDOW])   < 0)
		return -1;

    return 0;
}

static inline void setopt(Option* opt, const char* label, OptType type, void* data) {
	opt->label = label;	
	opt->type  = type;
	switch(type) {
		case OPT_TOGGLE: opt->toggle = *(OptToggle*)data; break;
		case OPT_RANGE:  opt->range  = *(OptRange*)data;  break;
		case OPT_SELECT: opt->select = *(OptSelect*)data; break;
		case OPT_STRING: opt->string = *(OptString*)data; break;
		default:;
	}
}

static inline OptSelect setopt_select(
		int* selected, char** list, 
		size_t len_list, int(*func)(void)) {
	return (OptSelect) {
		.selected = selected, .list = list, 
		.len_list = len_list, .func = func
	};
}

static inline OptRange setopt_range(int rmin, int rmax, int* val, int(*func)(void)) {
	return (OptRange){.rmin=rmin, .rmax=rmax, .val=val, .func=func};
}

static inline OptToggle setopt_toggle(bool* toggle, int(*func)(void)) {
	return (OptToggle){.toggle=toggle, .func=func};
}

static inline OptString setopt_string(char* val, bool* valid, int(*func)(void)) {
	return (OptString){.val=val, .pos=0, .valid=valid, .func=func};
}

int update_config(const ConfigList* conflist) {
	int ret;

	for(size_t i=0; i<conflist->sz; i++) {
		if((ret = update_config_option(conflist->options[i].name, 
								conflist->options[i].value)) < 0) {
			if(ret == -1)
				errlog("Warning: Failed to apply config option: %s!", conflist->options[i].name);

			else if(ret == -2)
				errlog("Warning: Config option doesn't exist: %s!", conflist->options[i].name);
		}
	}
	
	return 0;
}

int init_options(Option** options) {
	size_t sz = 0;
	Option* opt = ecalloc(128, sizeof(Option));

	OptSelect mode   = setopt_select(&config.main,   mode_strs,    NUM_MODES,                NULL); 
	OptSelect dict   = setopt_select(&config.idict,  dict_files,   strstr_len(dict_files),   optfunc_dict); 
	OptSelect quotes = setopt_select(&config.iquote, quotes_files, strstr_len(quotes_files), optfunc_quotes); 

	OptRange lhist       = setopt_range(MIN_HIST_LIMIT,  MAX_HIST_LIMIT,  &config.hist_limit,  NULL);
	OptRange words       = setopt_range(MIN_NWORDS,      MAX_NWORDS,      &config.nwords,      NULL);
	OptRange punct       = setopt_range(0,               100,             &config.punctuation, NULL);
	OptRange insert_freq = setopt_range(0,               100,             &config.insert_freq, NULL);
	OptRange timer       = setopt_range(MIN_TIMER,       MAX_TIMER,       &config.timer,       NULL);
	OptRange main_width  = setopt_range(MIN_MAIN_WIDTH,  MAX_MAIN_WIDTH,  &config.main_width,  NULL);
	OptRange main_height = setopt_range(MIN_MAIN_HEIGHT, MAX_MAIN_HEIGHT, &config.main_height, NULL);

	OptToggle ideath = setopt_toggle(&config.ideath,         NULL);
	OptToggle border = setopt_toggle(&config.border,         NULL);
	OptToggle colors = setopt_toggle(&config.colors_enabled, reset_attrs);

	OptString word_length  = setopt_string(config.word_length.str,  &config.word_length.valid,  optfunc_word_length);
	OptString quote_length = setopt_string(config.quote_length.str, &config.quote_length.valid, optfunc_quote_length);
	OptString digit_strs   = setopt_string(config.digit_strs.str,   &config.digit_strs.valid,   optfunc_digit_strs);
	OptString word_filter  = setopt_string(config.wfilter.str,      &config.wfilter.valid,      optfunc_word_filter);
	OptString circumfix    = setopt_string(config.circumfix.str,    &config.circumfix.valid,    optfunc_circumfix);
	OptString postfix      = setopt_string(config.postfix.str,      &config.postfix.valid,      optfunc_postfix);
	
	setopt(&opt[sz++], "Mode",                  OPT_SELECT,  &mode);
	setopt(&opt[sz++], "Instant Death",         OPT_TOGGLE,  &ideath);
	setopt(&opt[sz++], "Timer",                 OPT_RANGE,   &timer);
	setopt(&opt[sz++], "Words",                 OPT_RANGE,   &words);
	setopt(&opt[sz++], "Punctuation",           OPT_RANGE,   &punct);
	setopt(&opt[sz++], "Extra Postfix Punct",   OPT_STRING,  &postfix);
	setopt(&opt[sz++], "Extra Circumfix Punct", OPT_STRING,  &circumfix);
	setopt(&opt[sz++], "Word Filter",           OPT_STRING,  &word_filter);
	setopt(&opt[sz++], "Word Length",           OPT_STRING,  &word_length);
	setopt(&opt[sz++], "Quote Length",          OPT_STRING,  &quote_length);
	setopt(&opt[sz++], "Insertion Frequency",   OPT_RANGE,   &insert_freq);
	setopt(&opt[sz++], "Digit Strings",         OPT_STRING,  &digit_strs);
	setopt(&opt[sz++], "Files",                 OPT_SECTION, NULL);
	setopt(&opt[sz++], "Dictionary",            OPT_SELECT,  &dict);
	setopt(&opt[sz++], "Quotes",                OPT_SELECT,  &quotes);
	setopt(&opt[sz++], "History Limit",         OPT_RANGE,   &lhist);
	setopt(&opt[sz++], "Visuals",               OPT_SECTION, NULL);
	setopt(&opt[sz++], "Border",                OPT_TOGGLE,  &border);
	setopt(&opt[sz++], "Main Width",            OPT_RANGE,   &main_width);
	setopt(&opt[sz++], "Main Height",           OPT_RANGE,   &main_height);

    if(colors_started)
	    setopt(&opt[sz++], "Colors", OPT_TOGGLE, &colors);

	*options = opt;
	return sz;
}

/* Allocate and initialize data structures associated with each screen */
int init_screens(void) {
	screens = ecalloc(NUM_SCREENS, sizeof(Screen));

	/* Start screen */
	StartScrData* ssd  = ecalloc(1, sizeof(StartScrData));
	ssd->win_logo      = newwin(0,0,0,0);
	ssd->win_status    = newwin(0,0,0,0);
	ssd->pan_logo      = new_panel(ssd->win_logo);
	ssd->pan_status    = new_panel(ssd->win_status);
	screens[SCR_START] = (Screen){screen_start, ssd};
	hide_panel(ssd->pan_logo);
	hide_panel(ssd->pan_status);
    wbkgd(ssd->win_logo, COLOR_PAIR(CP_WINDOW));
    wbkgd(ssd->win_status, COLOR_PAIR(CP_WINDOW));
	keypad(ssd->win_status, true);
	leaveok(ssd->win_logo, true);
	leaveok(ssd->win_status, true);

	/* Main screen */
	MainScrData* msd  = ecalloc(1, sizeof(MainScrData));
	msd->win_text     = newwin(0,0,0,0);
	msd->pan_text     = new_panel(msd->win_text);
	screens[SCR_MAIN] = (Screen){screen_main, msd};
	hide_panel(msd->pan_text);
    wbkgd(msd->win_text, COLOR_PAIR(CP_WINDOW));
	keypad(msd->win_text, true);

	/* Option Screen */
	OptScrData* osd  = ecalloc(1, sizeof(OptScrData));
	osd->win_opt     = newwin(0,0,0,0);
	osd->pan_opt     = new_panel(osd->win_opt);
	screens[SCR_OPT] = (Screen){screen_opt, osd};
	osd->selected    = false;
	osd->opt_idx     = 1;
	osd->nopts       = init_options(&osd->options);
	hide_panel(osd->pan_opt);
    wbkgd(osd->win_opt, COLOR_PAIR(CP_WINDOW));
	keypad(osd->win_opt, true);

	/* Stat screen */
	StatScrData* stsd = ecalloc(1, sizeof(StatScrData));
	stsd->win_info    = newwin(0,0,0,0);
	stsd->pan_info    = new_panel(stsd->win_info);
	screens[SCR_STAT] = (Screen){screen_stat, stsd};
	hide_panel(stsd->pan_info);
    wbkgd(stsd->win_info, COLOR_PAIR(CP_WINDOW));
	keypad(stsd->win_info, true);
	leaveok(stsd->win_info, true);

	/* History screen */
	HistScrData* hsd  = ecalloc(1, sizeof(HistScrData));
	hsd->win_hist     = newwin(0,0,0,0);
	hsd->pan_hist     = new_panel(hsd->win_hist);
	screens[SCR_HIST] = (Screen){screen_hist, hsd};
	hide_panel(hsd->pan_hist);
    wbkgd(hsd->win_hist, COLOR_PAIR(CP_WINDOW));
	keypad(hsd->win_hist, true);
	leaveok(hsd->win_hist, true);

	bkgd(COLOR_PAIR(CP_SCREEN));

	return 0;
}

int init_stdin_dict(void) {
	Dictionary dict;

	if(load_dictionary(stdin, &dict) < 0)
		return -1;

	if(strstr_add(&dict_files, STDIN_NAME) < 0) {
		free_words(dict.words, dict.sz);
		return -1;
	}

	stdin_dict = dict;
	return 0;
}

int init_stdin_quote(void) {	
	Quotes quotes;

	if(load_quotes(stdin, &quotes) < 0)
		return -1;

	if(strstr_add(&quotes_files, STDIN_NAME) < 0) {
		free_quotes(&quotes);
		return -1;
	}

	stdin_quotes = quotes;
	return 0;
}

void init_stat(Stat* st) { memset(st, 0, sizeof(Stat)); }

void init_text(TypeText* tt) {
	const size_t initial_line_capacity = 12;

	tt->nwords         = 0;
	tt->curr_word      = 0;
	tt->curr_line      = 0;
	tt->lines_cap      = initial_line_capacity;
	tt->nlines         = 1;
	tt->lines          = ecalloc(tt->lines_cap, sizeof(Line));
	tt->lines[0].fword = 0;
	tt->lines[0].len   = 0;
	tt->text           = NULL;
	tt->matches        = NULL;
    tt->author         = NULL;
    tt->source         = NULL;
}


/* input loop for the start screen. */
ScreenNum loop_start(void) {
	StartScrData* const data = screens[SCR_START].data;

	while(true) {
		int key = wgetch(data->win_status);
		switch(key) {
		case KEY_RESIZE:
			fix_bkgd();
			redraw_start();
			continue;
		case ESC: return SCR_EXIT;
		}

		return SCR_MAIN;
	}
}


bool is_filtered_word(const char* word) {
	const ConfRange* const wl = &config.word_length;
	ConfRegex* const re = &config.wfilter;
	const int len = strlen(word);

	if(!is_range_off(wl) && (len < wl->min || len > wl->max))
		return true;
			
	if(re->valid && regexec(&re->re, word, 0, NULL, 0) == REG_NOMATCH)
		return true;

	return false;
}

bool is_filtered_quote(const Quote* quote) {
	int qmin=config.quote_length.min, qmax=config.quote_length.max;

	if(!is_range_off(&config.quote_length) 
	&& ((int)quote->sz < qmin || (int)quote->sz > qmax))
		return true;

	return false;
}

ScreenNum loop_hist(void) {
	HistScrData* const data = screens[SCR_HIST].data;
	
	while(true) {
		int key = wgetch(data->win_hist);
		switch(key) {
		case KEY_RESIZE:
			fix_bkgd();
			redraw_hist();
			continue;
		case KEY_DOWN: case 'j':
			data->selected = min(data->nhist-1, data->selected+1);
			break;
		case KEY_UP:   case 'k':
			data->selected = max(0, data->selected-1);
			break;
		case 'g':
			data->selected = 0;
			break;
		case 'G':
			data->selected = data->nhist-1;
			break;
		case KEY_ENTER: case 'l': case ' ':
			if(!data->hist_files) break;
			data->is_selected = true;
			loop_hist_stat();
			data->is_selected = false;
			break;
		case ESC: case 'h': return SCR_MAIN;
		default:
			if(keyname_cmp(key, "^J"))
				data->selected = min(data->nhist-1, data->selected+5);

			else if(keyname_cmp(key, "^K"))
				data->selected = max(0, data->selected-5);
		}
		
		redraw_hist();
	}

	return SCR_MAIN;
}

void loop_hist_stat(void) {
	HistScrData* const data = screens[SCR_HIST].data;
	const size_t bsz = 1024;
	char buf[bsz];
	FILE* fd;
			
	const char* fname = data->hist_files[data->selected];
	snprintf(buf, bsz, "%s/%s", user_hist_dir, fname);
	if(!(fd = fopen(buf, "r"))) { 
		data->errmsg = "Failed To Open File";
		redraw_hist();
		wgetch(data->win_hist);
		data->errmsg = NULL;
		return;
	}

	if(load_hist(fd, &data->hist) < 0) {
		data->errmsg = "Failed To Parse File";
		redraw_hist();
		wgetch(data->win_hist); data->errmsg = NULL; fclose(fd);
		return;
	}

	data->first_line = 0;
	data->nlines = nlines_text(data->hist.text, data->hist.matches, 
			data->hist.nwords, data->hist.nmatches,
			HIST_WIN_WIDTH-(config.border ? 2 : 0));
	redraw_hist();
	while(true) {
		int key = wgetch(data->win_hist);
		switch(key) {
		case KEY_RESIZE:
			fix_bkgd();
			redraw_hist();
			continue;
		case KEY_DOWN: case 'j':
			data->first_line = min(data->nlines-1, data->first_line+1);
			break;
		case KEY_UP: case 'k':
			data->first_line = max(0, data->first_line-1);
			break;
		case 'g':
			data->first_line = 0;
			break;
		case 'G':
			data->first_line = data->nlines-1;
			break;
		case ESC: case 'h':
			fclose(fd);
			free_hist(&data->hist);
			return;
		}
			
		redraw_hist();
	}

	fclose(fd);
	free_hist(&data->hist);
}

/* input loop for the main screen. */
ScreenNum loop_main(void) {
	MainScrData* const data = screens[SCR_MAIN].data;
	ScreenNum scrnum;
	const int nudge = (config.border ? 1 : 0);
	const int width_win = getmaxx(data->win_text)-nudge*2;

	while(true) {
		int key; 

		if(data->tt.text) curs_set(1);
		else              curs_set(0);
		key = wgetch(data->win_text);

		if(key == ESC) return SCR_EXIT;
		else if(keyname_cmp(key, "^U")) {
        	scrnum = SCR_OPT;
			break;
		}

		else if(keyname_cmp(key, "^P")) {
			scrnum = SCR_HIST;
			break;
		}
        
		else if(keyname_cmp(key, "^I"))
        	config.ideath = !config.ideath;
        
		else if(keyname_cmp(key, "^M"))
        	cycle_mode();
        
		else if(keyname_cmp(key, "^R")) {
        	free_text(&data->tt);
        	gen_text(&data->tt, width_win);
        }
        
		else if(isprint(key)) {
			if(!data->tt.text) continue;
        	ungetch(key);
        	scrnum = begin_test();
			break;
        }

		else if(key == KEY_RESIZE)
			fix_bkgd();
		
        redraw_main();
	}

	curs_set(0);
	return scrnum;
}

/* input loop for the type test screen. */
ScreenNum loop_test(void) {
	MainScrData* const mdata = screens[SCR_MAIN].data;
	const int nudge = (config.border ? 1 : 0);
	const int width = getmaxx(mdata->win_text)-nudge*2;
	const bool timed = config.main == M_TIMED && config.timer; 

	if(timed) {
		sigset_t sigset;
		struct sigaction sigact = {
			.sa_handler = sig_nothing,
			.sa_flags   = 0,
		};

		sigemptyset(&sigset);
		sigact.sa_mask = sigset;
		sigaction(SIGALRM, &sigact, NULL);
	}

	while(true) {
		int key;

		if(timed) { 
			struct timespec now;

			clock_gettime(CLOCK_MONOTONIC, &now);
			if(elapsed_ms(&mdata->time_start, &now) > config.timer*1e3) 
				break;

			alarm(1);
		}

		if((key = wgetch(mdata->win_text)) == ERR) {
			if(errno == EINTR) redraw_main();
			continue;
		}

		if(timed) alarm(0);
		
		if(isprint(key)) {
			if(tt_addch(mdata->win_text, &mdata->tt, key) == 1) break;
		}

		else if(key == KEY_BACKSPACE || keyname_cmp(key, "^?") || keyname_cmp(key, "^H"))
			tt_delch(mdata->win_text, &mdata->tt);

		else if(key == ESC) break;

		else if(key == KEY_RESIZE)
			fix_bkgd();

		/* generate words until text window is full */
		if(config.main == M_TIMED)
			gen_timed(&mdata->tt, width);

        redraw_main();
	}
	
	if(timed) signal(SIGALRM, SIG_DFL);

	return SCR_STAT;
}

ScreenNum loop_opt(void) {
	OptScrData* const data = screens[SCR_OPT].data;

	while(true) {
		int key = wgetch(data->win_opt);

		switch(key) {
			case ESC: return SCR_MAIN;
			case KEY_UP:   case 'k': opt_prev();  break;
			case KEY_DOWN: case 'j': opt_next();  break;
			case 'g':                opt_first(); break;
			case 'G':				 opt_last();  break;
			case ' ':
				data->selected = true;
				opt_select();
		    	data->selected = false;
				break;
			case KEY_RESIZE:
				fix_bkgd();
		}

        redraw_opt();
	}

	return SCR_MAIN;
}

void num_chars_typed(const TypeText* tt, int* typed, int* correct) {
    *typed = 0;
    *correct = 0;

    /* $tt->curr_word may be 1 greater than the last word in the case
     * every word was typed. */
    for(int i=0; i<=min(tt->curr_word, tt->nwords-1); i++) {
        const Word* text = &tt->text[i];
        const Word* match = &tt->matches[i];

        *typed += match->len;
        for(int j=0; j<text->len; j++)
            if(text->str[j] == match->str[j])
                *correct+=1;
    }

    /* account for spaces. */
    *typed += tt->curr_word;
    *correct += tt->curr_word;
}

int optfunc_circumfix(void) {
	return optfunc_confpunct(&config.circumfix, PUNCT_CIRCUMFIX);
}

int optfunc_confpunct(ConfPunct* punct, int type) {
	if(punct->valid) {
		free_puncts(punct->punct, punct->sz);
		free(punct->punct);
	}
	
	return parse_punct(punct, type);
}

int optfunc_dict(void) {
	Dictionary dict;
	int ret;

	if(!streq(dict_files[config.idict], STDIN_NAME)) {
		if((ret = get_dict(dict_files[config.idict], &dict)) != 0)
			return -1;
	} else dict = stdin_dict;

	if(loaded_dict.words != stdin_dict.words)
		free_words(loaded_dict.words, loaded_dict.sz);

	loaded_dict = dict;
	filtered_dict_valid = false;
	return 0;
}

int optfunc_digit_strs(void) {
	filtered_dict_valid = false;
	return optfunc_range(&config.digit_strs, 1, MAX_WORD);
}

int optfunc_range(ConfRange* r, int min, int max) {
	int result;

	if(strcmp(r->str, "-") == 0 || !r->str[0]) {
		r->min = -1;
		r->max = -1;
		strcpy(r->str, "-");
		return 0;
	}
	
	if((result = parse_range(r)) != (int)strlen(r->str))
		return -1;

	normalize_confrange(r, min, max);
	return 0;
}

int optfunc_postfix(void) {
	return optfunc_confpunct(&config.postfix, PUNCT_POSTFIX);
}

int optfunc_quote_length(void) {
	filtered_quotes_valid = false;
	return optfunc_range(&config.quote_length, 1, MAX_QUOTE_LENGTH);
}

int optfunc_quotes(void) {
	Quotes q;

	if(!streq(quotes_files[config.iquote], STDIN_NAME)) {
		if(get_quotes(quotes_files[config.iquote], &q) < 0)
			return -1;
	} else q = stdin_quotes;

	if(loaded_quotes.quotes != stdin_quotes.quotes)
		free_quotes(&loaded_quotes);

	loaded_quotes = q;
	filtered_quotes_valid = false;
	return 0;
}

int optfunc_word_filter(void) {
	ConfRegex* const regex = &config.wfilter;

	if(regex->valid) {
		regfree(&regex->re);
		filtered_dict_valid = false;
	}

	if(!regex->str[0]) return -1;
	if(regcomp(&regex->re, regex->str, REG_EXTENDED | REG_NOSUB) != 0)
		return -1;

	filtered_dict_valid = false;
	return 0;
}

int optfunc_word_length(void) {
	filtered_dict_valid = false;
	return optfunc_range(&config.word_length, 0, MAX_WORD);
}

void opt_first(void) {
	OptScrData* const data = screens[SCR_OPT].data;

	data->opt_idx = 0;
	if(data->options[0].type == OPT_SECTION)
		opt_next();
}

void opt_last(void) {
	OptScrData* const data = screens[SCR_OPT].data;

	data->opt_idx = data->nopts-1;
	if(data->options[data->opt_idx].type == OPT_SECTION)
		opt_prev();
}

void opt_next(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	int i = data->opt_idx;

	while(++i != data->nopts)
		if(data->options[i].type != OPT_SECTION) {
			data->opt_idx = i;
			break;
		}
}

void opt_prev(void) {
	OptScrData* const data = screens[SCR_OPT].data;
	int i = data->opt_idx;

	while(--i >= 0)
		if(data->options[i].type != OPT_SECTION) {
			data->opt_idx = i;
			break;
		}
	
}

void opt_select(void) {
	OptScrData* const data = screens[SCR_OPT].data;

	switch(data->options[data->opt_idx].type) {
		case OPT_TOGGLE: driver_toggle(); break;
		case OPT_RANGE:  driver_range();  break;
		case OPT_SELECT: driver_select(); break;
		case OPT_STRING: driver_string(); break;
		case OPT_SECTION:                 break;
	}
	
}

void print_log(void) {
	for(size_t i=0; i<sz_log; i++) {
		fprintf(stderr, "%s\n", log_messages[i]);
	}
}

/* Move the first word on the next line to $line */
void pull_word_next(TypeText* tt, size_t line) {
	const size_t vlen = word_visual_len(tt, tt->lines[line+1].fword++);

	tt->lines[line].len += vlen + 1;
	tt->lines[line+1].len -= vlen + 1;
    if(tt->curr_word == tt->lines[line+1].fword-1)
        tt->curr_line--;
}

/* Move the last word on $line to the next line */
void push_word_next(TypeText* tt, size_t line) {
	const size_t vlen = word_visual_len(tt, --tt->lines[line+1].fword);

	tt->lines[line].len -= vlen + 1;
	tt->lines[line+1].len += vlen + 1;
    if(tt->curr_word == tt->lines[line+1].fword)
        tt->curr_line++;
}

/* $buf is expected to be at least of size MAX_WORD+1 */
int punctuate(char* buf) {
	struct PunctRarity {
		Punctuation punct;
		int rarity;
	};

	int p;
	/* setting $punct to NULL avoids an unecessary compiler warning */
	const Punctuation* punct = NULL;
	static const struct PunctRarity puncts[] = {
		{ .punct = {.str = ",",    .type = PUNCT_POSTFIX},   .rarity = 20},
		{ .punct = {.str = ".",    .type = PUNCT_POSTFIX},   .rarity = 40},
		{ .punct = {.str = "!",    .type = PUNCT_POSTFIX},   .rarity = 50},
		{ .punct = {.str = ";",    .type = PUNCT_POSTFIX},   .rarity = 60},
		{ .punct = {.str = "?",    .type = PUNCT_POSTFIX},   .rarity = 70},
		{ .punct = {.str = ":",    .type = PUNCT_POSTFIX},   .rarity = 80},
		{ .punct = {.str = "...",  .type = PUNCT_POSTFIX},   .rarity = 85},
		{ .punct = {.str = "()",   .type = PUNCT_CIRCUMFIX}, .rarity = 90},
		{ .punct = {.str = "''",   .type = PUNCT_CIRCUMFIX}, .rarity = 95},
		{ .punct = {.str = "\"\"", .type = PUNCT_CIRCUMFIX}, .rarity = 100},
	};

	int pmax = 100;
	if(config.postfix.valid && config.postfix.sz)
		pmax += 10;

	if(config.circumfix.valid && config.circumfix.sz) 
		pmax +=  5;

	p = (rand() % (pmax+20))+1;

	/* only capitalize */
	if(p > pmax) {
		buf[0] = toupper(buf[0]);
		return 0;
	}

	/* set $punct punctuation from $puncts */
	if(p <= 100) {
		for(size_t i=0; i < arr_size(puncts); i++)
			if(p <= puncts[i].rarity) {
				punct = &puncts[i].punct;
				break;
			}
	}

	/* set $punct from the extra user-configured punctuations */
	else {
		punct = (p <= 110 - (config.postfix.valid && config.postfix.sz ? 0 : 10))
			? &config.postfix.punct[rand() % config.postfix.sz]
			: &config.circumfix.punct[rand() % config.circumfix.sz];
	}

	switch(punct->type) {
		case PUNCT_POSTFIX:
			if(strlen(buf)+strlen(punct->str) > MAX_WORD)
				return -1;

			strcat(buf, punct->str);
			break;

		case PUNCT_CIRCUMFIX:
			if(strlen(buf)+strlen(punct->str) > MAX_WORD) return -1;
			encase_word(buf, punct->str);
			break;
	}

	return 0;
}

void regen_filtered_dict(void) {
	free(filtered_dict.words);

	filtered_dict.words = ecalloc(loaded_dict.sz, sizeof(Word));
	filtered_dict.sz = 0;
	for(size_t i=0; i<loaded_dict.sz; i++)
		if(!is_filtered_word(loaded_dict.words[i].str))
			filtered_dict.words[filtered_dict.sz++] = loaded_dict.words[i];
}

void regen_filtered_quotes(void) {
	free(filtered_quotes.quotes);

	filtered_quotes.quotes = ecalloc(loaded_quotes.sz, sizeof(Quote));
	filtered_quotes.sz = 0;
	for(size_t i=0; i<loaded_quotes.sz; i++)
		if(!is_filtered_quote(&loaded_quotes.quotes[i]))
			filtered_quotes.quotes[filtered_quotes.sz++] = loaded_quotes.quotes[i];
}

int reset_attrs(void) {
    if(config.colors_enabled) {
        attributes.border   = COLOR_PAIR(CP_BORDER)   | config.attrs[CA_BORDER].attrs;
        attributes.text     = COLOR_PAIR(CP_TEXT)     | config.attrs[CA_TEXT].attrs;
        attributes.typed    = COLOR_PAIR(CP_TYPED)    | config.attrs[CA_TYPED].attrs;
        attributes.error    = COLOR_PAIR(CP_ERROR)    | config.attrs[CA_ERROR].attrs;
        attributes.selected = COLOR_PAIR(CP_SELECTED) | config.attrs[CA_SELECTED].attrs;
        init_pair_ca(CP_SCREEN, &config.attrs[CA_SCREEN]);
        init_pair_ca(CP_WINDOW, &config.attrs[CA_WINDOW]);
    }

    else {
        attributes.border   = config.attrs[CA_BORDER].attrs;
        attributes.text     = config.attrs[CA_TEXT].attrs;
        attributes.typed    = config.attrs[CA_TYPED].attrs;
        attributes.error    = config.attrs[CA_ERROR].attrs;
        attributes.selected = config.attrs[CA_SELECTED].attrs;
        init_pair(CP_SCREEN, default_color, default_color);
        init_pair(CP_WINDOW, default_color, default_color);
    }
    
    return 0;
}

void revert_rgb_colors(void) {
	RGB dummy;

	for(size_t i=0; i<NUM_CONF_ATTRS; i++) {
		ConfigAttr* const attr = &config.attrs[i];

		if(to_rgb(attr->fg, &dummy) == 0)
			attr->fg = default_attrs[i].fg;

		if(to_rgb(attr->bg, &dummy) == 0)
			attr->bg = default_attrs[i].bg;
	}
}

ScreenNum screen_hist(void) {
	const char* const re_str = 
		"^[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2},[0-9]{3}$";
	HistScrData* const data = screens[SCR_HIST].data;
	ScreenNum scrnum;
	regex_t reg;
	const size_t bsz = 1024;
	char buf[bsz];
	int ret;

	data->is_selected = false;
	data->selected = 0;
	data->errmsg = NULL;
	if(!user_hist_dir) {
		data->errmsg = "No History Directory";
		redraw_hist();
		wgetch(data->win_hist);
		return SCR_MAIN;
	}

	if((ret = dir_contents(user_hist_dir, &data->hist_files)) < 0) {
		data->hist_files = NULL;
		data->nhist = 0;
	}
	else { 
		data->nhist = ret;
		qsort(data->hist_files, data->nhist, 
				sizeof(char*), cmp_str_descend);
	}

	if(data->hist_files) {
		if(regcomp(&reg, re_str, REG_EXTENDED | REG_NOSUB) != 0) {
			free_strs(data->hist_files, data->nhist);
			free(data->hist_files);
			data->hist_files = NULL;
		}

		else {
			for(int i=0; i<data->nhist;) {
				if(regexec(&reg, data->hist_files[i], 0, NULL, 0) != 0) {
					strstr_remove(data->hist_files, i);
					data->nhist--;
				} else i++;
			}

			regfree(&reg);
		}

		for(int i=config.hist_limit; i<data->nhist;) {
			snprintf(buf, bsz, "%s/%s", user_hist_dir, data->hist_files[i]);
			if(remove(buf) == 0) {
				strstr_remove(data->hist_files, i);
				data->nhist--;
			}

			else {
				errlog("Warning: Failed to remove file: %s", buf);
				i++;
			}
		}
	}

	show_panel(data->pan_hist);
	redraw_hist();
	scrnum = loop_hist();
	hide_panel(data->pan_hist);

	if(data->hist_files) {
		free_strs(data->hist_files, data->nhist);
		free(data->hist_files);
	}

	data->hist_files = NULL;
	return scrnum;
}

ScreenNum screen_main(void) {
	MainScrData* const data = screens[SCR_MAIN].data;
    ScreenNum scrnum;
	const int nudge = (config.border ? 1 : 0);

	data->test_started = false;
	gen_text(&data->tt, getmaxx(data->win_text)-nudge*2);
	show_panel(data->pan_text);
	redraw_main();
	scrnum = loop_main();
	free_text(&data->tt);
	hide_panel(data->pan_text);
	return scrnum;
}

ScreenNum screen_stat(void) {
	StatScrData* const data = screens[SCR_STAT].data;

	show_panel(data->pan_info);
	redraw_stat();
	while(true) {
		int key = wgetch(data->win_info);

		if(key == ESC) break;
		if(key == KEY_RESIZE) {
			fix_bkgd();
			redraw_stat();
		}
	}

	hide_panel(data->pan_info);
	return ptype_args.oneshot ? SCR_EXIT : SCR_MAIN;
}

ScreenNum screen_start(void) {
    StartScrData* const data = screens[SCR_START].data;
    ScreenNum scrnum;

    show_panel(data->pan_logo);
    show_panel(data->pan_status);
	redraw_start();
	scrnum = loop_start();	
    hide_panel(data->pan_logo);
    hide_panel(data->pan_status);
	return scrnum;
}

ScreenNum screen_opt(void) {
    OptScrData* const data = screens[SCR_OPT].data;
    ScreenNum scrnum;

    show_panel(data->pan_opt);
	redraw_opt();
	scrnum = loop_opt();
    hide_panel(data->pan_opt);
	return scrnum;
}

int get_dict(const char* name, Dictionary* dict) {
	int ret;
	struct stat st;
	FILE* file;

	file = path_fopen(path_dicts, name);
	if(!file) return -1;
	fstat(fileno(file), &st);
	if(!(st.st_mode & S_IFREG)) { 
		fclose(file);
		return -1;
	}

	if((ret = load_dictionary(file, dict)) < 0) {
		fclose(file);
		return -1;
	}

	fclose(file);
	return 0;
}

int get_quotes(const char* name, Quotes* quotes) {
	int ret;
	struct stat st;
	FILE* file = path_fopen(path_quotes, name);

	if(!file) return -1;
	fstat(fileno(file), &st);
	if(!(st.st_mode & S_IFREG)) { 
		fclose(file);
		return -1;
	}

	if((ret = load_quotes(file, quotes)) < 0) {
		fclose(file);
		return -1;
	}

	fclose(file);
	return 0;
}

void sig_nothing(int sig) { /* do nothing */ }

/* return 1 if test is completed */
int tt_addch(WINDOW* win, TypeText* tt, int ch) {
    Stat* const st = &((StatScrData*)screens[SCR_STAT].data)->st;
	const Word* word = &tt->text[tt->curr_word];
	Word* match = &tt->matches[tt->curr_word];
	const int nudge = (config.border ? 1 : 0);

	/* move to next word only if the current match length is
	 * atleast equal to the current word length. */
	if(ch == ' ' && match->len >= word->len) {
		if(++tt->curr_word == tt->nwords) return 1;
        st->raw_typed++;
        st->raw_correct++;
		if(tt->curr_line+1 < tt->nlines
		&& tt->curr_word == tt->lines[tt->curr_line+1].fword)
		    tt->curr_line++;

		return 0;
	}

	/* the length of the current match should never exceed the length of the
	 * current word + MAX_ERR. */
	if(match->len >= word->len + MAX_ERR) return 0;
    st->raw_typed++;
	match->str[match->len++] = ch;
	match->str[match->len] = '\0';
    if(match->len <= word->len && ch == word->str[match->len-1])
        st->raw_correct++;

    else if(config.ideath) return 1;

	/* if the length of the current match exceeds the length of 
	 * the current word, the visible-word's size must have increased. */
    if(match->len > word->len) {
	    tt->lines[tt->curr_line].len++;
		tt_fix_line(tt, tt->curr_line, getmaxx(win)-nudge*2);
	}

	return 0;
}

void tt_delch(WINDOW* win, TypeText* tt) {
	const Word* word = &tt->text[tt->curr_word];
	Word* match = &tt->matches[tt->curr_word];
	const int nudge = (config.border ? 1 : 0);
	const int width = getmaxx(win)-nudge*2;

	if(match->len == 0) {
		if(tt->curr_word != 0 
		&& --tt->curr_word < tt->lines[tt->curr_line].fword)
			tt->curr_line--;

		return;
	} 

	match->len--;	
	match->str[match->len] = '\0';
	if(match->len >= word->len) {
		tt->lines[tt->curr_line].len--;

		/* check if the previous line can fit the first word on the current line. */
		if(tt->curr_line != 0)
			tt_fix_line(tt, tt->curr_line-1, width);

		tt_fix_line(tt, tt->curr_line, width);	
	}
}	

/* If $line's length exceeds the width of the window, move the
 * last word of $line to the next line (recursively);
 * &
 * If $line can fit the first word on the next line
 * move that word to $line (recursively);
 * &
 * Any line affected by the fix must also be fixed.
*/
void tt_fix_line(TypeText* tt, int num, int width) {
	if(!tt->text) return;

	/* check if words need to be moved to the next line. */
	if(tt->lines[num].len > width) {
		if(num == tt->nlines-1) {
			if(tt->nlines++ == tt->lines_cap)
				tt->lines = ereallocarray(tt->lines, tt->lines_cap*=2, sizeof(Line));

			tt->lines[num+1] = (Line){.fword = tt->nwords, .len = 0};
			push_word_next(tt, num);

			/* no need to update last line since we know it contains only 1 word. */
		}

		else {
            push_word_next(tt, num);
			tt_fix_line(tt, num+1, width); 	
		}
		
		/* update the same line again in case multiple words need to move to the next line. */
		tt_fix_line(tt, num, width); 
	}
	
	/* if no words were moved to the next line,
	 * check if words can be moved from the next line. */
	else if(num != tt->nlines-1
    && tt->lines[num].len + word_visual_len(tt, tt->lines[num+1].fword) < width) {
		pull_word_next(tt, num);
		tt_fix_line(tt, num+1, width); 
        tt_fix_line(tt, num, width); 
	}
}

void tt_fix_all_lines(TypeText* tt, int width) {
    Line* const first = &tt->lines[0];

	/* move all words to the first line */
    for(int i=1; i<tt->nlines; i++)
        first->len += tt->lines[i].len;

    tt->curr_line = 0;
    tt->nlines = 1;
    tt_fix_line(tt, 0, width);
}

int write_hist(FILE* fd, const TypeText* tt, const Stat* st) {
	const double ms = st->elapsed_ms / 1000.0;
	const int last_typed_word = min(tt->nwords-1, tt->curr_word);

	fprintf(fd, "main-mode: %s\n",          mode_strs[config.main]);
	fprintf(fd, "instant-death: %s\n",      config.ideath ? "on" : "off");
	fprintf(fd, "chars-correct: %u/%u\n",   st->ncorrect, st->ntyped);
	fprintf(fd, "time-elapsed: %.2fs\n",    ms);
	fprintf(fd, "wpm: %.2f\n",              st->wpm);
	fprintf(fd, "accuracy: %.2f%%\n",       st->acc);
	fprintf(fd, "awl: %.2f\n",              st->awl);
	if(st->author) fprintf(fd, "author: %s\n", st->author);
	if(st->source) fprintf(fd, "source: %s\n", st->source);

	fputs("\002", fd); /* ascii code STX (start of text) */
	const int end = (config.main == M_TIMED) ? last_typed_word+1 : tt->nwords;
	for(int i=0; i<end; i++)
		fprintf(fd, "%s%c", tt->text[i].str, 0);

	fputs("\003\n", fd); /* ascii code ETX (end of text) */

	fputs("\002", fd);

	for(int i=0; i<=last_typed_word; i++)
		fprintf(fd, "%s%c", tt->matches[i].str, 0);

	fputs("\003\n", fd);

	return 0;
}

void tt_init_word(TypeText* tt, const char* buf, size_t word) {
	const int word_len = strlen(buf);
	
	tt->text[word].str = ecalloc(word_len+1, sizeof(char));
	strcpy(tt->text[word].str, buf);
	tt->text[word].len = word_len;
	tt->matches[word].str = ecalloc(word_len + MAX_ERR + 1, sizeof(char));
	tt->matches[word].len = '\0';
	return;
}

inline double wpm(size_t ncorrect, long elapsed_ms) { 
	return ((12000.0 * ncorrect) / elapsed_ms); }

int main(int argc, char* argv[]) {
    ScreenNum scrnum;
	int opt;

	progname = argv[0];

	while((opt = getopt(argc, argv, ":hvc:Qw1")) != -1) {
		switch(opt) {
			case 'h':
				puts(HELP_STR);
				exit(0);
			case 'v':
				puts(VERSION_STR);
				exit(0);
			case 'c':
				if(streq(optarg, "=on"))
					ptype_args.color = ARG_ON;
				else if(streq(optarg, "=off"))
					ptype_args.color = ARG_OFF;
				else {
					fprintf(stderr, "%s: option 'c' has invalid argument '%s'\n",
							progname, optarg);
					exit(2);
				}
				break;
			case 'Q':
				ptype_args.quote = true;
				break;
			case 'w':
				ptype_args.warnings = false;
				break;
			case '1':
				ptype_args.oneshot = true;
				break;
			case '?': 
				fprintf(stderr, "%s: option '%c' is unrecognized\n",
						progname, optopt);
				exit(2);
			case ':':
				fprintf(stderr, "%s: option '%c' requires an argument\n", 
						progname, optopt);
				exit(2);
		}
	}

	init();
	scrnum = (config.start_screen) ? SCR_START : SCR_MAIN;
	while((scrnum = screens[scrnum].run()) != SCR_EXIT);
	exit(0);
}
