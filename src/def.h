#ifndef DEF_H
#define DEF_H

#include <config.h>

#include <ncurses.h>
#include <panel.h>
#include <stddef.h>
#include <stdbool.h>
#include <dirent.h>
#include <regex.h>
#include <time.h>

#define STX 2
#define ETX 3
#define ESC 27

#define FILE_ERROR 	-1
#define MEM_ERROR 	-2
#define PARSE_ERROR -3

#define MAX_WORD        30
#define MAX_ERR         4
#define MIN_NWORDS      1
#define MAX_NWORDS      3000
#define MAX_QUOTE_LENGTH MAX_NWORDS
#define MIN_TIMER       0
#define MAX_TIMER       600
#define MIN_HIST_LIMIT  0
#define MAX_HIST_LIMIT  1000
#define MIN_MAIN_WIDTH  MAX_WORD+MAX_ERR+3
#define MAX_MAIN_WIDTH  80
#define MIN_MAIN_HEIGHT 1
#define MAX_MAIN_HEIGHT 21
#define NUM_ANSI_COLORS 8
#define HIST_WIN_WIDTH  MAX_WORD+MAX_ERR+3
#define MAX_STRING_OPT  256
#define RANGE_OFF_STR "<none>"

#define SYSTEM_CONFIG   SYSCONFDIR"/ptype"
#define DIR_DICTS       "dictionaries"
#define DIR_QUOTES      "quotes"
#define DIR_USER_CONFIG "ptype"
#define FILE_CONFIG     "ptype.conf"
#define DIR_STATE       "ptype"  
#define DIR_HISTORY     "history"

enum { CP_TEXT=1, CP_TYPED, CP_ERROR, CP_BORDER, 
	   CP_BKMAIN, CP_SELECTED, CP_WINDOW, CP_SCREEN
};

enum {CA_BORDER, CA_TEXT, CA_TYPED, CA_ERROR, CA_SELECTED, 
      CA_SCREEN, CA_WINDOW, NUM_CONF_ATTRS
};

typedef enum { 
	M_NORMAL, M_TIMED, M_QUOTE, NUM_MODES
} ModeType;

typedef enum { 
	OPT_TOGGLE, OPT_RANGE, OPT_SELECT, OPT_STRING, OPT_SECTION
} OptType;

typedef enum {
	SCR_EXIT=-1,
	SCR_START, SCR_MAIN, SCR_STAT, SCR_OPT, SCR_HIST, 
	NUM_SCREENS
} ScreenNum;

typedef struct {
    int r;
    int g;
    int b;
} RGB;

typedef struct {
    char** colors;
    size_t sz;
} ColorMap;

typedef struct {
	char* str;
	int len;
} Word;

typedef struct {
	char* name;
	char* val;
} NameVal;

typedef struct {
	Word* words;
	size_t sz;
} Dictionary;

typedef struct {
	char* author;
	char* source;
	Word* text;   /* contents of the quote. */
	size_t sz;
} Quote;

typedef struct {
	Quote* quotes; /* list of quotes. */
	size_t sz;
} Quotes;

typedef struct {
	int fword; /* first word on line. */
	int len;   /* accumulative length of words on the line */
} Line;

typedef struct {
	Word* text;         /* text generated for the current test. */
	Word* matches;      /* matches[i] is the users attempt at text[i]. */
	Line* lines;        /* holds information necessary for formatting lines. */
	int nwords;         /* size of text and matches. */
	int curr_word;      /* the word the user is currently attempting to type. */
	int lines_cap;      /* number of elements currently allocated in lines. */
	int nlines;         /* number of lines. */
	int curr_line;      /* the line that curr_word appears on. */
    const char* author;
    const char* source;
} TypeText;

typedef struct {
	char* str;
	enum {PUNCT_POSTFIX, PUNCT_CIRCUMFIX} type;
} Punctuation;

typedef struct {
    attr_t border;
    attr_t text;
    attr_t typed;
    attr_t error;
    attr_t selected;
} GlobalAttrs;

typedef struct {
    char* fg;     /* foreground color. */
    char* bg;     /* background color. */
    attr_t attrs; /* attributes. */
} ConfigAttr;

typedef struct {
	char str[MAX_STRING_OPT];
	bool valid;               /* is $re a valid compiled regular-expression. */
	regex_t re;               /* compiled regular-expression. */
} ConfRegex;

typedef struct {
	char str[MAX_STRING_OPT];
	bool valid;
	int min;
	int max;
} ConfRange;

typedef struct {
	char str[MAX_STRING_OPT];
	bool valid;               /* is $str a valid list of whitespace separated words. */
	Punctuation* punct;
	size_t sz;
} ConfPunct;

typedef struct {
	ConfigAttr attrs[NUM_CONF_ATTRS];
	ConfRange word_length;            /* word-length range. */
	ConfRange quote_length;           /* quote-length range. */
	ConfRange digit_strs;             /* interpolate digit-strings, of a size in this range, into text. */
	ConfRegex wfilter;                /* regex to filter words generated in dictionary. */
	ConfPunct circumfix;              /* list of extra circumfix punctuations */
	ConfPunct postfix;                /* list of extra postfix punctuations */
	int main;                         /* M_* modes. */
	int timer;                        /* timer duration in seconds for M_TIMER mode. */
	int nwords;                       /* number of words to generate for M_NORMAL mode. */
	int punctuation;                  /* number between 0-10 (0 = off). */
	int hist_limit;                   /* overwrite old history files at limit (0 = off). */
	int main_width;                   /* window width. */
	int main_height;                  /* window height. */
	int idict;                        /* currently selected dictionary. */
	int iquote;                       /* currently selected quote file. */
	int insert_freq;                  /* frequency that insertions, such as digit-strings, are made */
	bool border;                      /* is the border on or off? */
	bool start_screen;                /* is the start-screen displayed? */
	bool colors_enabled;              /* are colors on or off? */
	bool ideath;                      /* instant death. */
} Config;

typedef struct {
	NameVal* stats;
	Word* text;
	Word* matches;
	int nwords;
	int nmatches;
	int sz_stats;
} History;

typedef struct {
	bool* toggle;     /* pointer to togglable variable in global config. */
    int(*func)(void); /* function to call after updating option. */
} OptToggle;

typedef struct {
	int* val;         /* pointer to value in global config. */
	int rmin;         /* range min. */
	int rmax;         /* range max. */
    int(*func)(void); /* function to call after updating option. */
} OptRange;

typedef struct {
	int* selected;    /* currently selected from list (pointer to index), */
	char** list;      /* list to select from. */
	int len_list;     /* length of the list. */
	int(*func)(void); /* function to call after updating option. */
} OptSelect;

typedef struct {
	char* val;        /* pointer to value in global config */
	bool* valid;      /* pointer to value in global config */
	int pos;          /* position of the cursor into the string. */
	int(*func)(void); /* function to call after updating option. */
} OptString;

typedef struct {
	OptType type;      /* OPT_* types. */
	const char* label; /* option name. */
	union {
		OptToggle toggle;
		OptRange range;
		OptSelect select;
		OptString string;
	};
} Option;

typedef struct {
	const char* author;
	const char* source; 
	int raw_typed;      /* number of inputs. */
    int raw_correct;    /* number of correct inputs. */
	int ntyped;         /* number of characters typed by the end of the test. */
	int ncorrect;       /* number of characters correctly typed by the end of the test. */
	int wtyped;         /* number of complete words typed. */
	int64_t elapsed_ms; /* time taken in milliseconds. */
	double wpm;         /* words per minute. */
	double acc;         /* accuracy as percentage. */
	double awl;         /* average word len. */
} Stat;

typedef struct {
	WINDOW* win_text;           /* window the test text appears in. */
	PANEL* pan_text;            /* win_text panel. */
	TypeText tt;                /* all data related to the currently generated test. */
	bool test_started;
	struct timespec time_start;
} MainScrData;

typedef struct {
	WINDOW* win_logo;   /* logo appears here. */
	WINDOW* win_status; /* status_str appears here. */
	PANEL* pan_logo;    /* win_logo panel. */
	PANEL* pan_status;	/* win_status panel. */
} StartScrData;

typedef struct {
	WINDOW* win_opt; /* options displayed here. */
	PANEL* pan_opt;  /* win_opt panel */
	Option* options; /* options list. */
	int nopts;       /* number of options. */
	int opt_idx;     /* current option in group hovered. */
	bool selected;   /* is the option selected. */
} OptScrData;

typedef struct {
	WINDOW* win_info;
	PANEL* pan_info; /* win_info panel; */
	Stat st;
} StatScrData;

typedef struct {
	WINDOW* win_hist;  /* history & stats displayed here. */
	PANEL* pan_hist;   /* win_hist panel; */
	char** hist_files; /* history list. */
	char* errmsg; 
	int nhist;         /* number of history files. */
	bool is_selected;  /* is $st loaded. */
	int selected;      /* index into $hist_files. */
	int nlines;        /* number of lines in the history text. */
	int first_line;    /* first line to display on screen in history text. */
	History hist;
} HistScrData;

typedef ScreenNum(*FRun)(void);
typedef struct {
	FRun run;   /* the screens main logic. */
	void* data; /* each screen has a unique data structure associated with it. */
} Screen;

#endif /* DEF_H */
