#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "drw.h"
#include "def.h"
#include "utils.h"
#include "aart.h"

extern Screen* screens;
extern Config config;
extern GlobalAttrs attributes;
extern char* mode_strs[];
extern void tt_fix_all_lines(TypeText*, int width);

static inline bool win_changed(WINDOW* win, int ny, int nx, int nh, int nw) {
    return ny != getbegy(win) || nx != getbegx(win)
        || nh != getmaxy(win) || nw != getmaxx(win);
}

static inline void add_error(WINDOW* win, int ch) {
    wattron(win, attributes.error);
    waddch(win, ch);
    wattroff(win, attributes.error);
}

static inline void add_text(WINDOW* win, int ch) {
    wattron(win, attributes.text);
    waddch(win, ch);
    wattroff(win, attributes.text);
}

static inline void add_typed(WINDOW* win, int ch) {
    wattron(win, attributes.typed);
    waddch(win, ch);
    wattroff(win, attributes.typed);
}

/* $lines is the size of the window; $sz is the size of the list.
 * return the position of the first list-option in the window where the
 * following applies:
 * the first list-option is always on the first line when visible;
 * the last list-option is always on the last line when visible, unless 
 * there is less list-options than can fill the window;
 * when neither the first or last list-options are visible the $pos-th option
 * is displayed in the center of the window. */
size_t scroll_list(int lines, int pos, int sz) {
	return max(0, min(sz - lines, pos - lines/2));
}

/* copy the portion of $str into $buf where: $str[cpos] is the last
 * byte in $buf unless $str's length is less than width, in which case
 * all of $str is copied to $buf. If $cpos is equal to $str's length
 * the null-byte is replaced with a space in $buf and moved one index
 * forward. 
 * return the index of $cpos mapped to $buf. */
int get_input_string(char* buf, const char* str, int cpos, int width) {
	const char* s = str+max(0, cpos-(width-1));
	int len = strnlen(str, width);

	memcpy(buf, s, len);
	if(!str[cpos]) {
		if(len!=width)
			buf[len++] = ' ';
		else
			buf[len-1] = ' ';
	}

	buf[len] = '\0';
	return (cpos>=len) ? len-1 : cpos;
}

/* draw rectangular 2 dimensional array to window */
static int waddimg(WINDOW* win, const char* img, size_t height, size_t width) {
	int row = getcury(win);
	int col = getcurx(win);

	for(size_t i=0; i<height; i++) {
		for(size_t j=0; j<width; j++)
			waddch(win, img[i*width + j]);

		wmove(win, ++row, col);
	}

	return 0;
}

/* print label value pair. */
static int print_label_val(WINDOW* win, int line, const char* label, const char* fmt, ...) {
	const size_t bsz = 128;
	char buf[bsz];
	const size_t len_label = strlen(label);
	int nudge = (config.border ? 1 : 0);
	va_list ap;
	int x;

	mvwaddstr(win, line, nudge, label);

	va_start(ap, fmt);
	vsnprintf_fit(buf, bsz, (getmaxx(win)-1-nudge)-(len_label+2), fmt, ap);
	va_end(ap);

	x = walignstr_right(win, buf, nudge);
	mvwaddstr(win, line, x, buf);

	return 0;
}

static void print_option(WINDOW* win, int y, int x, const Option* opt, bool hover, bool selected) {
	const char* const str_on  = "on";
	const char* const str_off = "off";
	const int nudge = (config.border ? 1 : 0);
	const size_t bsz = 1024;
	char buf[bsz];
	/* space available for printing the option's value. */
	const int rwidth = (getmaxx(win)-nudge*2) - (strlen(opt->label)+2);

	if(opt->type != OPT_SECTION) {
		if(hover)
            wattron(win, attributes.selected);

		mvwprintw(win, y, x, "%s", opt->label);
		wattroff(win, attributes.selected);
	}
    

	switch(opt->type) {
		case OPT_TOGGLE: {
			if(*opt->toggle.toggle) {
				wattron(win, attributes.typed);
				mvwaddstr(win, y, walignstr_right(win, str_on, nudge), str_on);
				wattroff(win, attributes.typed);
			} else {
				wattron(win, attributes.error);
				mvwaddstr(win, y, walignstr_right(win, str_off, nudge), str_off);
				wattroff(win, attributes.error);
			}

			break;
		}

		case OPT_RANGE: {
			sprintf(buf, "%d", *opt->range.val);
			if(hover && selected)
				wattron(win, A_UNDERLINE);

			mvwprintw(win, y, walignstr_right(win, buf, nudge), "%s", buf);
			wattroff(win, A_UNDERLINE);
			break;
		}

		case OPT_SELECT: {
			const char* opt_str = (*opt->select.list)
				? opt->select.list[*opt->select.selected]
			    : "Empty";

            snprintf_fit(buf, bsz, rwidth, "%s", opt_str);
			if(hover && selected)
				wattron(win, A_UNDERLINE);

            mvwaddstr(win, y, walignstr_right(win, buf, nudge), buf);
			wattroff(win, A_UNDERLINE);
			break;
		}

		case OPT_STRING: {
			const OptString* const opt_str = &opt->string;
			if(hover && selected) {
				int cpos = get_input_string(buf, opt_str->val, opt_str->pos, rwidth);
			
				int x = walignstr_right(win, buf, nudge);
				mvwaddstr(win, y, x, buf);
				/* draw pseudo-cursor */
				wattron(win, A_UNDERLINE);
				mvwaddch(win, y, x+cpos, buf[cpos]);
				wattroff(win, A_UNDERLINE);
			}

			else {
				if(!*opt_str->valid) wattron(win, attributes.error);
				snprintf_fit(buf, bsz, rwidth, "%s", opt_str->val);	
				mvwaddstr(win, y, walignstr_right(win, buf, nudge), buf);
				wattroff(win, attributes.error);
			}

			break;
		}

		case OPT_SECTION: {
			mvwprintw(win, y, whalignstr_center(win, opt->label)+1,
					"%s", opt->label);
			break;
		}
	}
}


static void update_test_timer(const TypeText* tt) {
	MainScrData* const data = screens[SCR_MAIN].data;
	static const char* infinite_str = "Infinite";
	char time_str[32];

	if(!config.timer) {
		mvwaddstr(data->win_text, 0, 
			whalignstr_center(data->win_text, infinite_str), infinite_str);
		return;
	}

	else if(data->test_started) {
		struct timespec now;
		long elapsed;

		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = elapsed_ms(&data->time_start, &now)/1e3;
		sprintf(time_str, "%lu", config.timer - elapsed);
	}

	else
		sprintf(time_str, "%d", config.timer);

	mvwprintw(data->win_text, 0, 
			whalignstr_center(data->win_text, time_str), "%ss", time_str);
}

static void update_win_curs(WINDOW* win, const TypeText* tt) {
	const int nudge = (config.border ? 1 : 0);
    const int fword = tt->lines[tt->curr_line].fword;
    Word* const match = &tt->matches[tt->curr_word];
	int x=0, y=0;

    if(tt->curr_line < (getmaxy(win)-1-nudge)/2)
		y = tt->curr_line;

    else y = max(0, (getmaxy(win)-1-nudge)/2);

	for(int i = fword; i < tt->curr_word; i++)
		x += tt->matches[i].len + 1;

	x += match->len;
	wmove(win, y+1, x+nudge);
}

/* update the border and mode information embedded within it. */
static void update_win_info(WINDOW* win, const TypeText* tt) {
	static const char* ideath_str   = "Instant Death";
	const int nudge = (config.border ? 1 : 0);

    if(config.border) {
	    wattron(win, attributes.border);
	    box(win, 0, 0);
	    wattroff(win, attributes.border);
    }
	
	mvwprintw(win, 0, nudge, "Mode: %s", mode_strs[config.main]);
	if(config.main == M_TIMED)
		update_test_timer(tt);

	if(config.ideath) {
		wattron(win, attributes.error);
		mvwprintw(win, 0, walignstr_right(win, ideath_str, nudge), "%s", ideath_str);
		wattroff(win, attributes.error);
	}
}

static void update_win_word(WINDOW* win, const TypeText* tt, int line_num, int word_num, int y, int x) {
	const Word* word = &tt->text[word_num];
	const Word* match = &tt->matches[word_num];

	wmove(win, y, x);

	int i = 0;
	for(; i<word->len; i++) {
		if(match->len <= i) {
			add_text(win, word->str[i]);
			continue;
		}

		(match->str[i] == word->str[i])
			? add_typed(win, word->str[i])
			: add_error(win, word->str[i]);
	}

	for(; i<match->len; i++)
		add_error(win, match->str[i]);

    (word_num < tt->curr_word)
	    ? add_typed(win, ' ')
		: add_text(win, ' ');
}

static void update_win_text(WINDOW* win, TypeText* tt) {
	const char* const str_no_text = "No Text Generated";
	const int nudge = (config.border ? 1 : 0);
	int y = 1, x = 0;

	if(!tt->text) {
		x = whalignstr_center(win, str_no_text);
		y = getmaxy(win)/2;
		wattron(win, attributes.error);
		mvwaddstr(win, y, x, str_no_text);
		wattroff(win, attributes.error);
		return;
	}
	
	const int start = max(0, tt->curr_line - (getmaxy(win)-1-nudge)/2); 
	const int end   = min(tt->nlines, start + getmaxy(win)-1-nudge);
	for(int i=start; i<end; i++) { 
		const int end_word = (i == tt->nlines-1) 
            ? tt->nwords : tt->lines[i+1].fword;

		x = nudge;
		for(int j=tt->lines[i].fword; j<end_word; j++) { 
			update_win_word(win, tt, i, j, y, x);
			x += word_visual_len(tt, j) + 1;
		}

		y++;
	}

	update_win_curs(win, tt);
}

void redraw_main(void) {
	MainScrData* const data = screens[SCR_MAIN].data;
    WINDOW* const win = data->win_text;
	const int nudge = (config.border ? 1 : 0);
	const int wt_height = config.main_height+1 + nudge;
    const int wt_width = config.main_width + 2*nudge;
	const int wt_y = align_center(LINES, wt_height);
	const int wt_x = align_center(COLS, wt_width);

	/* don't change window dimensions if the new size is the same. */
    if(win_changed(win, wt_y, wt_x, wt_height, wt_width)) {
		del_panel(data->pan_text);
	    wresize(win, wt_height, wt_width); 
		data->pan_text = new_panel(win);
	    move_panel(data->pan_text, wt_y, wt_x);
        tt_fix_all_lines(&data->tt, getmaxx(win)-nudge*2);
    }

	/* fill the window with blanks */
	werase(win);

	update_win_info(win, &data->tt);
    update_win_text(win, &data->tt);

	update_panels();
	doupdate();
}

void redraw_start(void) {
	StartScrData* const data = screens[SCR_START].data;	
	static const char* prompt_str = "Press any key to start, or esc to quit";
	const int wl_height = LOGO_HEIGHT, wl_width = LOGO_WIDTH+1;
	const int wl_y = align_center(LINES, wl_height);
	const int wl_x = align_center(COLS, wl_width);
	const int ws_height = 1, ws_width = strlen(prompt_str);  
	const int ws_y = (wl_y + wl_height) + 1;
	const int ws_x = align_center(COLS, ws_width);

	/* don't change window dimensions if the new size is the same. 
       if $win_logo changed, so will have $win_status */
    if(win_changed(data->win_logo, wl_y, wl_x, wl_height, wl_width)) {
		del_panel(data->pan_logo);
		del_panel(data->pan_status);
	    wresize(data->win_logo, wl_height, wl_width);
	    wresize(data->win_status, ws_height, ws_width);	
		data->pan_logo = new_panel(data->win_logo);
		data->pan_status = new_panel(data->win_status);
	    move_panel(data->pan_logo, wl_y, wl_x);
	    move_panel(data->pan_status, ws_y, ws_x);
    }

	/* fill the window with blanks */
	werase(data->win_logo);  
	werase(data->win_status);
	wmove(data->win_logo, 0, 0);
	wattron(data->win_logo, attributes.border);
	waddimg(data->win_logo, art_logo, LOGO_HEIGHT, LOGO_WIDTH);
	wattroff(data->win_logo, attributes.border);
	wmove(data->win_status, 0,
			whalignstr_center(data->win_status, prompt_str));

	waddstr(data->win_status, prompt_str);

	update_panels();
	doupdate();
}

void redraw_opt(void) {
	const char* const win_label = "Settings";
	OptScrData* const data = screens[SCR_OPT].data;
    WINDOW* const win = data->win_opt;
	const int nudge = (config.border ? 1 : 0);
	const int wo_width=42, wo_height=min(24, data->nopts+1)+nudge;
	const int wo_y = align_center(LINES, wo_height-nudge);
	const int wo_x = align_center(COLS, wo_width);

	/* don't change window dimensions if the new size is the same. */
    if(win_changed(win, wo_y, wo_x, wo_height, wo_width)) {
		del_panel(data->pan_opt);
	    wresize(win, wo_height, wo_width);
		data->pan_opt = new_panel(win);
	    move_panel(data->pan_opt, wo_y, wo_x);
    }

	/* fill the window with blanks */
	werase(win);

	wattron(win, attributes.border);
    if(config.border) box(win, 0, 0);
	mvwprintw(win, 0, nudge, "%s", win_label);
	wattroff(win, attributes.border);

	const int lines = wo_height-1-nudge;
	const int start = scroll_list(lines, data->opt_idx, data->nopts);
	for(int i=start; (i-start)<lines; i++)
		print_option(win, (i-start)+1, nudge, &data->options[i],
				data->opt_idx==i, data->selected);

	update_panels();
	doupdate();
}

void redraw_stat(void) {
	StatScrData* const data = screens[SCR_STAT].data;
	static const char* const ideath_str = "Instant Death";
    WINDOW* const win = data->win_info;
	const int nudge = (config.border ? 1 : 0);
	int wi_height = 7 + nudge;
	const int wi_width = 31;
	const int wi_y = align_center(LINES, wi_height);
	const int wi_x = align_center(COLS, wi_width);
	int line;

	if(config.main == M_QUOTE) {
		if(data->st.author || data->st.source) wi_height++;
		if(data->st.author) wi_height++;
		if(data->st.source) wi_height++;
	}
	
	/* don't change window dimensions if the new size is the same. */
    if(win_changed(win, wi_height, wi_width, wi_y, wi_x)) {
		del_panel(data->pan_info);
	    wresize(win, wi_height, wi_width);
		data->pan_info = new_panel(win);
	    move_panel(data->pan_info, wi_y, wi_x);
    }

	/* fill the window with blanks */
	werase(win);
    wattron(win, attributes.border);
    if(config.border)
	    box(win, 0, 0);
	
    mvwaddstr(win, 0, nudge, mode_strs[config.main]);
	wattroff(win, attributes.border);
    if(config.ideath) {
        wattron(win, attributes.error);
        mvwaddstr(win, 0, walign_right(win, strlen(ideath_str), nudge), ideath_str);
        wattroff(win, attributes.error);
    }

	line = 1;
	const double secs = data->st.elapsed_ms/1000.0;
	print_label_val(win, line++, "Time", "%6.2fs", secs);
	print_label_val(win, line++, "Words typed", "%u", data->st.wtyped);
	print_label_val(win, line++, "Letters correct", "%u/%u", data->st.ncorrect, data->st.ntyped);
	print_label_val(win, line++, "Average word size", "%6.2f", data->st.awl);
	print_label_val(win, line++, "Accuracy", "%6.2f%%", data->st.acc);
	print_label_val(win, line++, "Wpm", "%6.2f", data->st.wpm);
	line++;
	if(data->st.author) print_label_val(win, line++, "Author", "%s", data->st.author);
	if(data->st.source) print_label_val(win, line++, "Source", "%s", data->st.source);

	update_panels();
	doupdate();
}

static void redraw_hist_files(void) {
	const char* const str_no_hist = "No History Files";
	const char* const str_hist_fail = "Failed To Open History";
	const char* const win_label = "History";
	const int nudge = (config.border) ? 1 : 0;
	HistScrData* const data = screens[SCR_HIST].data;
	WINDOW* const win = data->win_hist;
	PANEL* const pan = data->pan_hist;
	const size_t bsz = 32;
	char buf[bsz];

	if(data->hist_files) {
		if(data->nhist) {
			/* number of lines where a file can be displayed */
			const int lines = getmaxy(win)-nudge-1;

			int start = scroll_list(lines, data->selected, data->nhist);
			for(int i=start; (i-start) < lines && i<data->nhist; i++) {
				/* don't copy milliseconds or seconds from file name. */
				memcpy(buf, data->hist_files[i], 16);
				buf[16] = '\0';

				const int x = whalignstr_center(win, buf)+1;
				if(i == data->selected) wattron(win, attributes.selected);
				mvwaddstr(win, (i-start)+1, x, buf);
				wattroff(win, attributes.selected);
			}
		} 

		else {
			const int x = whalignstr_center(win, str_no_hist)+2;
			const int y = wvalign_center(win, 1);
			wattron(win, attributes.error);
			mvwaddstr(win, y, x, str_no_hist);
			wattroff(win, attributes.error);
		}
	} 

	else {
		const int x = whalignstr_center(win, str_hist_fail)+1;
		const int y = wvalign_center(win, 1);
		wattron(win, attributes.border);
		mvwaddstr(win, y, x, str_hist_fail);
		wattroff(win, attributes.border);
	}

	wattron(win, attributes.border);
	mvwaddstr(win, 0, 1, win_label);
	wattroff(win, attributes.border);
}

static void mvwadd_diff(WINDOW* win, int y, int x, const Word* word, const Word* match) {
	const Word* w1 = word;
	const Word* w2 = match;
	int attr;

	if(!match) {
		wattron(win, attributes.text);
		mvwaddstr(win, y, x, word->str);
		wattroff(win, attributes.text);
		return;
	}

	int i=0;
	for(; i < w1->len && i < w2->len; i++) {
		attr = (w1->str[i] == w2->str[i])
			? attributes.typed
			: attributes.error;

		wattron(win, attr);
		mvwaddch(win, y, x+i, w1->str[i]);
		wattroff(win, attr);
	}

	attr = attributes.text;
	if(w1->len < w2->len) {
		w1 = match;
		w2 = word;
		attr = attributes.error;
	}

	wattron(win, attr);
	for(; i < w1->len; i++)
		mvwaddch(win, y, x+i, w1->str[i]);
	wattroff(win, attr);
}

static void redraw_hist_stat(void) {
	HistScrData* const data = screens[SCR_HIST].data;
	WINDOW* const win = data->win_hist;
	const int nudge = config.border ? 1 : 0;
	const size_t bsz = 128;
	char buf[bsz];
	const int w = getmaxx(win)-(2*nudge);

	int i=0;
	/* the arbitrary '-5' is to save 5 lines for the following history text. */
	for(; data->hist.stats[i].name && i+1 < getmaxy(win)-1-nudge-5; i++) {
		const char* const name = data->hist.stats[i].name;
		const char* const val = data->hist.stats[i].val;
		const int val_w = getmaxx(win)-(nudge*2) - (strlen(name)+2);

		mvwaddstr(win, i+1, nudge, name);
		snprintf_fit(buf, bsz, val_w, "%s", val);	
		const int val_x = walignstr_right(win, buf, nudge);
		mvwaddstr(win, i+1, val_x, buf);
	}

	wattron(win, attributes.border);
	mvwhline(win, i+1, 0, ACS_HLINE, getmaxx(win));
	if(config.border) {
		mvwaddch(win, i+1, 0, ACS_LTEE);
		mvwaddch(win, i+1, getmaxx(win)-1, ACS_RTEE);
	}

	wattroff(win, attributes.border);

	const int y_text = i+2;
	const int h_text = getmaxy(win)-y_text-nudge;
	int x = nudge;
	int line = 0;
	for(int j=0; j<data->hist.nwords; j++) {
		const Word* const word  = &data->hist.text[j];
		const Word* match = NULL;
		int len = word->len;
		if(j < data->hist.nmatches) {
			match = &data->hist.matches[j];
			len = max(word->len, match->len);
		}

		if(x + len+1 > getmaxx(win)-nudge) {
			x = nudge;
			line++;
			if(line-data->first_line>=h_text)
				break;
		}

		if(line >= data->first_line) {
			const int y = line-data->first_line;
			mvwadd_diff(win, y_text+y, x, word, match);
		}

		x += len+1;
	}

	memcpy(buf, data->hist_files[data->selected], 16);
	buf[16] = '\0';
	wattron(win, attributes.border);
	mvwaddstr(win, 0, nudge, buf);
	wattroff(win, attributes.border);
}

void redraw_hist(void) {
	HistScrData* const data = screens[SCR_HIST].data;
	WINDOW* const win = data->win_hist;
	PANEL* const pan = data->pan_hist;
	const int wh_height = 25;
	const int wh_width = HIST_WIN_WIDTH;
	const int wh_y = align_center(LINES, wh_height);
	const int wh_x = align_center(COLS, wh_width);
	const size_t bsz = 128;
	char buf[bsz];

	/* don't change window dimensions if the new size is the same. */
    if(win_changed(win, wh_y, wh_x, wh_height, wh_width)) {
		del_panel(data->pan_hist);
	    wresize(win, wh_height, wh_width);
		data->pan_hist = new_panel(win);
	    move_panel(data->pan_hist, wh_y, wh_x);
    }
	
	const size_t w = getmaxx(win)-(config.border ? 2 : 0);
	werase(win);

	wattron(win, attributes.border);
	if(config.border) box(win, 0, 0);
	wattroff(win, attributes.border);

	if(data->errmsg) {
		const int y = (getmaxy(win)/2)-1;
		const int x = whalignstr_center(win, data->errmsg)+1;
		wattron(win, attributes.error);
		mvwaddstr(win, 0, 1, "Error");
		snprintf_fit(buf, bsz, w, "%s", data->errmsg);
		mvwaddstr(win, y, x, buf);
		wattroff(win, attributes.error);
	} else {
		(data->is_selected)
		    ? redraw_hist_stat()
			: redraw_hist_files();
	}

	update_panels();
	doupdate();
}
