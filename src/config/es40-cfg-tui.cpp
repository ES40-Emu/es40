/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by Martin Vorländer
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

#if defined(_WIN32) && !defined(HAVE_CONFIG_H)
// Set the API baseline before PCap or curses can include Windows headers.
#include "config_win32.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>

#include <string>
#include <vector>

using namespace std;

#if defined(_WIN32) && !defined(__MINGW32__)
// Visual Studio: PDCursesMod provides curses and panel, NetBSD libform/libmenu provide form and menu
#include <stdarg.h>
#include <curses.h>
#include <panel.h>
#include <menu.h>
// NetBSD form.h declares the TYPE_* field types outside its own extern "C" block.
extern "C" {
#include <form.h>
}

// NetBSD libform keeps the caller's TYPE_ENUM list pointer
// Keep the shared choice storage alive and translate the flags.
template <typename... Args>
inline int es40_set_field_type(FIELD *field, FIELDTYPE *type, Args... args)
{
    return set_field_type(field, type, args...);
}

template <typename C, typename U>
inline int es40_set_field_type(FIELD *field, FIELDTYPE *type, const char **list, C checkcase, U checkunique)
{
    if (type != TYPE_ENUM)
        return set_field_type(field, type, list, checkcase, checkunique);

    // Choices are owned by the shared field descriptors for the entire form lifetime.
    return set_field_type(field, TYPE_ENUM, list, checkcase ? FALSE : TRUE, TRUE);
}

template <typename C, typename U>
inline int es40_set_field_type(FIELD *field, FIELDTYPE *type, char **list, C checkcase, U checkunique)
{
    return es40_set_field_type(field, type, (const char **)list, checkcase, checkunique);
}

#define set_field_type es40_set_field_type

// NetBSD libform's REQ_END_LINE is bad, puts cursor one beyond. Fix here. 
inline int es40_form_driver(FORM *form, int c)
{
    if (c != REQ_END_LINE && c != REQ_END_FIELD)
        return form_driver(form, c);

    int status = form_driver(form, REQ_BEG_LINE);
    for (int i = 0; i < 1024 && form_driver(form, REQ_NEXT_CHAR) == E_OK; ++i)
        ;
    return status;
}

#define form_driver es40_form_driver
#elif defined(__MINGW32__)
// compile with -DNCURSES_STATIC to be able to link
#include <ncurses/ncurses.h>
#include <ncurses/panel.h>
#include <ncurses/menu.h>
#include <ncurses/form.h>
#else
#include <ncurses.h>
#include <panel.h>
#include <menu.h>
#include <form.h>
#endif

#include "StdAfx.h"
#include "ConfigEditor.h"

// es40_banner: Time to wait for a keypress
#define WAITSEC 5

// curses: y,x for menus
#define MENU_1ST_LEVEL 1, 4
#define MENU_2ND_LEVEL 4, 7
#define MENU_3RD_LEVEL 7, 10
#define MENU_4TH_LEVEL 10, 13

// curses: action keys
#define MYKEY_HELP KEY_F(1)
#define MYKEY_EXIT KEY_F(2)
#define MYKEY_QUIT KEY_F(3)
#define MYKEY_HELP_STR "F1"
#define MYKEY_EXIT_STR "F2"
#define MYKEY_QUIT_STR "F3"

// curses: "normal" Keys
#define MYKEY_TAB 9
#define MYKEY_RETURN 10
#define MYKEY_DEL 127


int scrh, scrw;
void show_text(const char *, const char *);

/**
 * curses: Update the columns setting 'c' to be big enough to hold string 's'
 **/
inline void set_min_width(int *c, const char *s)
{
    if (s == NULL)
        return;
    if ((int)strlen(s) + 2 > *c)
        *c = strlen(s) + 2;
}

/**
 * curses: Display 'text' in row 'y' of window 'win'
 * centered to maximum width 'maxw'.
 **/
void mvwprintw_center(WINDOW *win, int y, int maxw, const char *text)
{
    int len = (int)strlen(text);
    if (len > maxw - 2)
        len = maxw - 2; // truncate rather than break the border

    int left_x = (maxw - len + 1) / 2;

    mvwprintw(win, y, left_x, "%s", text);
}

/**
 * curses: Create a new window of 'nLines' rows and at least 'nCols' columns
 * with the upper left at 'begin_y', 'begin_x'.
 * Draw a border around it with 'title' in the first row,
 * and 'helptext' in the last row.
 *
 * Widens the window by increasing 'nCols' to accomodate 'title' and 'helptext'.
 * If 'begin_x' and 'begin_y' are both -1, center the window on the screen.
 **/
WINDOW *create_window(
    int nLines, int nCols,
    int begin_y, int begin_x,
    const char *title,
    const char *helptext)
{
    if (title != NULL)
        set_min_width(&nCols, title);
    if (helptext != NULL)
        set_min_width(&nCols, helptext);

    if (nLines > scrh)
        nLines = scrh - 2;
    if (nCols > scrw)
        nCols = scrw - 2;

    if (begin_y == -1 && begin_x == -1)
    {
        begin_y = (scrh - nLines - 2) / 2;
        begin_x = (scrw - nCols - 2) / 2;
    }

    WINDOW *my_win = newwin(nLines + 2, nCols + 2, begin_y, begin_x);
    box(my_win, 0, 0);
    keypad(my_win, TRUE);

    if (title != NULL)
        mvwprintw_center(my_win, 0, nCols, title);
    if (helptext != NULL)
        mvwprintw(my_win, nLines + 1, nCols - strlen(helptext), "%s", helptext);

    return my_win;
}

/**
 * curses: Display a centered window with a 'title' containing the 'text'.
 * Wait for a key, then close the window.
 *
 * 'text' may be multiple lines, separated by '\n'.
 **/
void tui_text(const char *title, const char *text)
{
    if (text == NULL)
        return;

    const char *helptext = "Any key to close";

    int nLines = 1;
    int nCols = 1;
    char *linebuffer = strdup(text);
    char *p, *q;

    if (strchr(linebuffer, '\n') == NULL)
        set_min_width(&nCols, linebuffer);
    else
        for (p = linebuffer; (q = strchr(p, '\n')) != NULL; p = q + 1)
        {
            ++nLines;
            *q = '\0';
            set_min_width(&nCols, p);
        }

    WINDOW *my_win = create_window(nLines, nCols, -1, -1, title, helptext);
    PANEL *my_panel = new_panel(my_win);

    p = linebuffer;
    for (int i = 1; i <= nLines; ++i)
    {
        mvwprintw(my_win, i, 1, "%s", p);
        p += strlen(p) + 1; // Skip NUL terminating part
    }

    update_panels();
    doupdate();

    wgetch(my_win);

    // Clean up
    del_panel(my_panel);
    wclear(my_win);
    wrefresh(my_win);
    delwin(my_win);
    update_panels();
    doupdate();

    free(linebuffer);
}

/**
 * Display a menu of 'num_entries' menu 'entry's in a window
 * with upper left corner at row 'begin_y', column 'begin_x'.
 *
 * React to keys pressed:
 * <UP>, <DOWN>: go to previous/next item, wrapping around
 * <ENTER>: if item callback is NULL: exit menu,
 *          else call its callback
 * <F1>: Display item description
 * <F2>: exit menu
 * <F3>: quit menu
 *
 * Returns the index of the last item selected,
 * or -1, -2 if leaving via <F2> and <F3>, respectively.
 **/
int tui_menu(
    const char *title,
    MenuEntry_t entry[], int num_entries,
    int begin_y, int begin_x)
{
    const char *helptext = MYKEY_HELP_STR ": help, " MYKEY_EXIT_STR ": exit";
    char dashes[80] = "";

    int nLines = num_entries;
    int nCols = 0;

    set_min_width(&nCols, helptext);
    for (int i = 0; i < num_entries; ++i)
        set_min_width(&nCols, entry[i].text);

    WINDOW *my_win = create_window(nLines, nCols, begin_x, begin_y, title, helptext);
    PANEL *my_panel = new_panel(my_win);
    update_panels();

    ITEM **my_item = (ITEM **)calloc(num_entries + 1, sizeof(ITEM *));
    for (int i = 0; i < num_entries; ++i)
        if (entry[i].text != NULL)
            my_item[i] = new_item((char *)entry[i].text, NULL); // NetBSD libmenu takes non-const char *
        else
        {
            if (dashes[0] == '\0')
            {
                size_t len = nCols;
                if (len > sizeof(dashes))
                    len = sizeof(dashes);
                memset(dashes, '-', len);
                dashes[len - 1] = '\0';
            }
            my_item[i] = new_item(dashes, NULL);
            item_opts_off(my_item[i], O_SELECTABLE);
        }
    my_item[num_entries] = (ITEM *)NULL;

    MENU *my_menu = new_menu(my_item);
    set_menu_grey(my_menu, A_NORMAL); // No multi-select, but unselectable items
    set_menu_win(my_menu, my_win);
    set_menu_sub(my_menu, derwin(my_win, nLines, nCols, 1, 1));
#if defined(_WIN32)
    // NetBSD libmenu defaults to 16 rows and blank-fills rows past the items; with a shorter
    // subwindow the failed wmove() makes those blanks overwrite the first items.
    set_menu_format(my_menu, nLines, 1);
#endif
    post_menu(my_menu);
    update_panels();
    doupdate();

    bool stay_in_loop = TRUE;
    int cur_idx = 0;
    while (stay_in_loop)
    {
        ITEM *cur = current_item(my_menu);
        cur_idx = item_index(cur);
#if defined(_WIN32)
        // NetBSD libmenu draws into the derwin() without syncing it; PDCurses only refreshes touched windows.
        touchwin(my_win);
        wcursyncup(menu_sub(my_menu));
#endif
        switch (wgetch(my_win))
        {
        case MYKEY_HELP:
            show_text(entry[cur_idx].text, entry[cur_idx].description);
            break;
        case MYKEY_QUIT:
            cur_idx = MENU_SEL_QUIT;
            stay_in_loop = FALSE;
            break;
        case MYKEY_EXIT:
            cur_idx = MENU_SEL_EXIT;
            stay_in_loop = FALSE;
            break;
        case KEY_DOWN:
            if (cur_idx == num_entries - 1)
                menu_driver(my_menu, REQ_FIRST_ITEM);
            else
                menu_driver(my_menu, REQ_DOWN_ITEM);
            break;
        case KEY_UP:
            if (cur_idx == 0)
                menu_driver(my_menu, REQ_LAST_ITEM);
            else
                menu_driver(my_menu, REQ_UP_ITEM);
            break;
        case MYKEY_RETURN:
            if ((item_opts(cur) & O_SELECTABLE) != 0)
            {
                SelectFuncPtr callback = entry[cur_idx].select_callback;
                if (callback == NULL)
                    stay_in_loop = FALSE;
                else
                {
                    callback(item_name(cur));
                    pos_menu_cursor(my_menu);
                }
            }
            break;
        default:
            break;
        }
    }

    // Clean up
#if defined(_WIN32)
    // PDCurses' delwin() refuses a window that still has subwindows (and asserts in Debug builds).
    WINDOW *my_sub = menu_sub(my_menu);
#endif
    unpost_menu(my_menu);
    free_menu(my_menu);
    del_panel(my_panel);
    wclear(my_win);
    wrefresh(my_win);
#if defined(_WIN32)
    delwin(my_sub);
#endif
    delwin(my_win);
    update_panels();
    doupdate();

    for (int i = 0; i < num_entries; ++i)
        free_item(my_item[i]);
    free(my_item);

    return cur_idx;
}

/**
 * Display a form of 'num_entries' form 'entry's
 * in a centered window with a 'title'.
 * Fill in the fields from 'preset'.
 *
 * React to keys pressed:
 * <UP> / <DOWN>, <ENTER>: go to previous/next field, wrapping around
 * <TAB>, <RIGHT> / Shift-<TAB>, <LEFT>: in enum fields, go to next/previous choice
 * <F1>: Display field description
 * <F2>: exit form
 * <F3>: quit form
 * any other printable character is entered in the current field
 *
 * When the form is exited, call 'check_callback' (if not NULL).
 * If this returns FALSE, stay in the form.
 *
 * The entered values are returned in a 'num_entries' wide array.
 * The calling routine is responsible for freeing values' storage,
 * except when leaving via <F3>. In that case, NULL is returned
 *
 **/
bool tui_form(const char *title, FormEntry_t entry[], vector<EditorField>& fields)
{
    const int count = (int)fields.size();
    int nLines = count * 2;
    int nCols = scrw - 20;
    if (nLines + 2 > scrh)
        FAILURE_1(Runtime, "Screen is too small for form, need %d lines", nLines + 2);
    WINDOW *win = create_window(nLines, nCols + 1, -1, -1, title,
                               "F1: help, F2: apply, F3: cancel");
    PANEL *panel = new_panel(win);
    vector<FIELD *> native(count + 1, nullptr);
    // NetBSD libform retains TYPE_ENUM pointers. Keep them alive through free_field().
    vector<vector<char *>> choices(count);
    for (int i = 0; i < count; ++i)
    {
        auto& rule = fields[i];
        FIELD *f = native[i] = new_field(1, nCols - 1, 2 * i + 1, 1, 0, 0);
        set_field_buffer(f, 0, rule.value.c_str());
        field_opts_off(f, O_AUTOSKIP | O_STATIC);
        set_max_field(f, 256);
        set_field_pad(f, ' ');
        switch (rule.type)
        {
        case EditorField::Type::Choice:
            for (auto& choice : rule.choices)
                choices[i].push_back(const_cast<char *>(choice.c_str()));
            choices[i].push_back(nullptr);
            set_field_type(f, TYPE_ENUM, choices[i].data(), FALSE, TRUE);
            break;
        case EditorField::Type::Integer:
            set_field_type(f, TYPE_INTEGER, rule.precision, (long)rule.minimum, (long)rule.maximum);
            field_opts_off(f, O_NULLOK);
            break;
        case EditorField::Type::Number:
            set_field_type(f, TYPE_NUMERIC, rule.precision, rule.minimum, rule.maximum);
            field_opts_off(f, O_NULLOK);
            break;
        case EditorField::Type::Pattern:
            set_field_type(f, TYPE_REGEXP, rule.pattern.c_str());
            break;
        default: break;
        }
        set_field_back(f, rule.type == EditorField::Type::Choice ? A_NORMAL : A_UNDERLINE);
        set_field_status(f, TRUE);
    }
    FORM *form = new_form(native.data());
    WINDOW *sub = derwin(win, nLines, nCols, 1, 1);
    set_form_win(form, win);
    set_form_sub(form, sub);
    post_form(form);
    for (int i = 0; i < count; ++i)
        mvwprintw(win, 2 * i + 1, 1, "%s", entry[i].label);
    form_driver(form, REQ_FIRST_FIELD);
    form_driver(form, REQ_END_FIELD);
    bool apply = false, done = false;
    while (!done)
    {
        update_panels();
        doupdate();
#ifdef _WIN32
        touchwin(win);
        wcursyncup(sub);
#endif
        FIELD *f = current_field(form);
        int idx = field_index(f);
        const bool choice = fields[idx].type == EditorField::Type::Choice;
        int key = wgetch(win);
        switch (key)
        {
        case MYKEY_HELP: show_text(entry[idx].label, entry[idx].description); break;
        case MYKEY_QUIT: done = true; break;
        case MYKEY_EXIT:
            if (form_driver(form, REQ_VALIDATION) == E_OK) { apply = true; done = true; }
            else show_text("Error", "Field validation failed");
            break;
        case KEY_DOWN: case MYKEY_RETURN:
            form_driver(form, idx + 1 == count ? REQ_FIRST_FIELD : REQ_NEXT_FIELD);
            form_driver(form, REQ_END_FIELD); break;
        case KEY_UP:
            form_driver(form, idx == 0 ? REQ_LAST_FIELD : REQ_PREV_FIELD);
            form_driver(form, REQ_END_FIELD); break;
        case MYKEY_TAB: case KEY_RIGHT:
            form_driver(form, choice ? REQ_NEXT_CHOICE : (key == KEY_RIGHT ? REQ_NEXT_CHAR : REQ_END_FIELD)); break;
        case KEY_STAB: case KEY_LEFT:
            form_driver(form, choice ? REQ_PREV_CHOICE : (key == KEY_LEFT ? REQ_PREV_CHAR : REQ_BEG_FIELD)); break;
        case KEY_BACKSPACE: case MYKEY_DEL:
#ifdef _WIN32
        case 0x08:
#endif
            if (!choice) form_driver(form, REQ_DEL_PREV);
            break;
        default:
            if (!choice && key >= 0 && key <= UCHAR_MAX && isprint(key)) form_driver(form, key);
            break;
        }
    }
    if (apply)
        for (int i = 0; i < count; ++i)
        {
            fields[i].value = field_buffer(native[i], 0);
            while (!fields[i].value.empty() && fields[i].value.back() == ' ')
                fields[i].value.pop_back();
        }
    unpost_form(form);
    free_form(form);
    del_panel(panel);
    delwin(sub);
    delwin(win);
    for (int i = 0; i < count; ++i) free_field(native[i]);
    update_panels();
    doupdate();
    return apply;
}

/**
 * Display the ES40 banner in a centered window,
 * then wait at most WAITSEC seconds for a keypress.
 **/
void es40_banner(const char *title)
{
    const char *text[] = {
        title,
        "Version " VERSION,
        "",
        "Copyright (C) 2007-2025 by the ES40 Emulator Project & Others",
        "Website: https://github.com/ES40-Emu/es40/",
        "",
        "This program is free software; you can redistribute it and/or",
        "modify it under the terms of the GNU General Public License",
        "as published by the Free Software Foundation; either version 2",
        "of the License, or (at your option) any later version."};
    int num_entries = (int)(sizeof(text) / sizeof(text[0]));
    const char *helptext = "Any key to continue";

    int nLines = num_entries;
    int nCols = 0;

    for (int i = 0; i < num_entries; ++i)
        set_min_width(&nCols, text[i]);

    string helptext_dur = string(helptext) + " (" + to_string(WAITSEC) + ")";
    WINDOW *my_win = create_window(nLines, nCols, -1, -1, NULL, helptext_dur.c_str());
    PANEL *my_panel = new_panel(my_win);

    for (int i = 0; i < num_entries; ++i)
        mvwprintw_center(my_win, i + 1, nCols, text[i]);

    update_panels();
    doupdate();

    const int factor = 10;
#if !defined(_WIN32)
    const timespec req = {0, 1000L * 1000 * 1000 / factor}; // = 100,000,000 ns = 0.1 seconds
#endif
    int count = WAITSEC * factor;
    int maxx, maxy;
    getmaxyx(my_win, maxy, maxx);
    nodelay(my_win, TRUE);
    while (--count > 0 && wgetch(my_win) == ERR)
    {
#if defined(_WIN32)
        Sleep(1000 / factor);
#else
        nanosleep(&req, NULL);
#endif
        if (count % factor == 0)
        {
            // No idea why the coordinates need offsets...
            mvwprintw(my_win, maxy - 1, maxx - 2 - 3, "(%1d)", count / factor);
            update_panels();
            doupdate();
        }
    }
    nodelay(my_win, FALSE);

    // Clean up
    del_panel(my_panel);
    wclear(my_win);
    wrefresh(my_win);
    delwin(my_win);
    update_panels();
    doupdate();
}

class TuiUI final : public EditorUI
{
public:
    void initialize() override
    {
        setlocale(LC_ALL, "");
#ifndef _WIN32
        setenv("NCURSES_NO_PADDING", "1", FALSE);
#endif
        initscr(); cbreak(); noecho(); noqiflush(); keypad(stdscr, TRUE);
        getmaxyx(stdscr, scrh, scrw);
    }
    void shutdown() override { endwin(); }
    void banner() override { es40_banner("AlphaServer ES40 emulator configuration utility"); }
    void text(const char *title, const char *body) override { tui_text(title, body); }
    int menu(const char *title, MenuEntry_t entry[], int count, int y, int x) override
    { return tui_menu(title, entry, count, y, x); }
    bool form(const char *title, FormEntry_t entry[], vector<EditorField>& fields) override
    { return tui_form(title, entry, fields); }
};

int main(int argc, char **argv)
{
    TuiUI ui;
    return run_config_editor(argc, argv, ui);
}
