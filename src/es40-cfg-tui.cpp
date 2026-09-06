/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
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

/**
 * \file
 * Configuration file editor, TUI variant.
 * This still is work in progress!
 *
 * $Id$
 *
 * X-0.5        Martin Vorländer                                05-SEP-2026
 *      Fully functional version.
 * X-0.4        Martin Vorländer                                28-AUG-2026
 *      Implemented form presets.
 * X-0.3        Martin Vorländer                                26-AUG-2026
 *      Functional version. Still some TODOs to be completed.
 * X-0.2        Martin Vorländer                                17-AUG-2026
 *      Changed header comment and license.
 * X-0.1        Martin Vorländer                                01-JUL-2026
 *      File created.
 **/

// TODO: For forms that the user does not enter, insert the default values (e.g. system settings) before saving,
// TODO: For PCI cards, implement adding multiple cards of the same type.
// TODO: Add the option to remove things.
// TODO: Add the option to quit forms without checks or storing the results.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>

#include <sstream>
#include <string>
#include <vector>

using namespace std;

#include <pcap/pcap.h>

#ifdef __MINGW32__
// compile with -DNCURSES_STATIC to be able to link
#include <ncurses/ncurses.h>
#include <ncurses/panel.h>
#include << ncurses / menu.h>
#include << ncurses / form.h>
#else
#include <ncurses.h>
#include <panel.h>
#include <menu.h>
#include <form.h>
#endif

#include "StdAfx.h"
#include "Configurator.h"

#define ARRAY_SIZE(a) (int)(sizeof(a) / sizeof(a[0]))

// Maximum number of CPUs
#define MAX_CPUS 4

// Low and high values for memory.bits parameter
#define LOW_MEM_BITS 25 // 32MB
#define HI_MEM_BITS 35  // 32GB

// Internal PCI device slot names, plus a placeholder one
#define PCI_SLOT_ALI "pci0.7"
#define PCI_SLOT_IDE "pci0.15"
#define PCI_SLOT_PMU "pci0.17"
#define PCI_SLOT_USB "pci0.19"
#define PCI_SLOT_TEMP "pci99.99"

// Forms: Strings for negative and affirmative answer
#define STR_NO "no"   // "no" , "false", "0"
#define STR_YES "yes" // "yes", "true" , "1"

// es40_banner: Time to wait for a keypress
#define WAITSEC 5

// curses: y,x for menus
#define MENU_1ST_LEVEL 1, 4
#define MENU_2ND_LEVEL 4, 7
#define MENU_3RD_LEVEL 7, 10
#define MENU_4TH_LEVEL 10, 13

// curses: "normal" Keys
#define KEY_TAB 9
#define KEY_RETURN 10
#define KEY_DEL 127

/**
 * Menu item type
 **/

// Type of routine to be called when menu item is selected
typedef void (*SelectFuncPtr)(const char *_title);

// Menu item type
typedef struct MenuEntry_t
{
    const char *text;
    const char *description;
    SelectFuncPtr select_callback;
} MenuEntry_t;

/**
 * Form field type
 **/

// Type of routine to be called from form to setup type and validation for field
typedef void (*ValidationFuncPtr)(FIELD *_field);

// Form field type
typedef struct FormEntry_t
{
    const char *label;
    const char *preset;
    const char *name; // configuration file attribute name, NULL for special treatment
    const char *description;
    ValidationFuncPtr validation_callback;
} FormEntry_t;

// Type for array of values entered into form
typedef char **FormValues_t;

// Type of routine to be called from form to check validity of values
typedef bool (*FormCheckFuncPtr)(FormEntry_t entry[], int num_entries, FormValues_t values);

/**
 * Global variables
 **/

// The Configuration, plus The System
CConfigurator *theConfig, *sys0;

// (Re-)calculated height and width of screen
int scrh, scrw;

/**
 * Convert an integer to a string.
 **/
inline string i2s(int x)
{
    // From src/NumberQuestion.h
    ostringstream o;
    o << x;
    return o.str();
}

/**
 * Convert a string to an integer.
 *
 * Throws a CLogicException when the input is not numeric.
 **/
inline int s2i(const string x)
{
    // From src/NumberQuestion.h
    istringstream i(x);
    int x1;
    char c;
    if (!(i >> x1) || i.get(c))
        FAILURE_1(Logic, "invalid integer conversion of '%s'", x.c_str());
    return x1;
}

/**
 * Convert memory size to memory bits
 * (assumes mem_size is a power of 2)
 *
 * Throws a CLogicException when 'mem_size' does not end in 'M', 'm', 'G', or 'g'.
 **/
int memory_size2bits(char *mem_size)
{
    char unit = mem_size[strlen(mem_size) - 1];
    u64 amount = atoi(mem_size);
    int mem_bits = 0;
    switch (unit)
    {
    case 'm':
    case 'M':
        amount *= 1024 * 1024;
        break;
    case 'g':
    case 'G':
        amount *= 1024 * 1024 * 1024;
        break;
    default:
        FAILURE_1(Logic, "Unrecognized memory size: '%s'", mem_size);
        break;
    }
    while ((amount & 1) == 0 && amount != 0)
    {
        ++mem_bits;
        amount >>= 1;
    }
    return mem_bits;
}

/**
 * Convert memory bits to memory size.
 *
 * Throws a CLogicException when 'mem_bits' is not in the range LOW_MEM_BITS .. HI_MEM_BITS.
 **/
string memory_bits2size(int mem_bits)
{
    if (mem_bits < LOW_MEM_BITS || mem_bits > HI_MEM_BITS)
        FAILURE_1(Logic, "Memory bits out of range: '%d'", mem_bits);

    // from es40-cfg.cpp
    string unit = "";
    int j = mem_bits;
    if (mem_bits < 30)
    {
        // Megabyte-range.
        j -= 20;
        unit = "M";
    }
    else
    {
        // Gigabyte range.
        j -= 30;
        unit = "G";
    }
    return (i2s(1 << j) + unit);
}

#if 0
/**
 * Signal handler for SIGWINCH
 **/
RETSIGTYPE resizeHandler(int sig)
{
    getmaxyx(stdscr, scrh, scrw);
    /* TODO: recalculate the dimensions of all active windows using scrh, scrw
     *       and redisplay them.
     *       This Does not work with the current structure of the program!
     */
    wclear(stdscr);
    wrefresh(stdscr);
}
#endif

/**
 * Read theConfig from a file named 'filename'.
 */
void read_configuration(const char *filename)
{
    // From src/AlphaSim.cpp
    FILE *f;

    // Open chosen file (print the path on failure)
    f = fopen(filename, "rb");
    if (!f)
        FAILURE_2(Configuration, "failed to open configuration file: %s (%s)",
                  filename, strerror(errno));

    // Determine size (Windows: 64-bit length; POSIX: fseek/ftell)
    size_t ll1 = 0;
#if defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        FAILURE_1(Configuration, "failed to seek end of configuration file: %s", filename);
    }
    __int64 pos64 = _ftelli64(f);
    if (pos64 < 0)
    {
        fclose(f);
        FAILURE_1(Configuration, "failed to tell size of configuration file: %s", filename);
    }
    ll1 = (size_t)pos64;
    if (_fseeki64(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        FAILURE_1(Configuration, "failed to rewind configuration file: %s", filename);
    }
#else
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        FAILURE_1(Configuration, "failed to seek end of configuration file: %s", filename);
    }
    long pos = ftell(f);
    if (pos < 0)
    {
        fclose(f);
        FAILURE_1(Configuration, "failed to tell size of configuration file: %s", filename);
    }
    ll1 = (size_t)pos;
    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        FAILURE_1(Configuration, "failed to rewind configuration file: %s", filename);
    }
#endif

    // Allocate buffer (+1 for NUL), read, and NUL-terminate for safety
    char *ch1 = (char *)calloc(ll1 + 1, 1);
    if (!ch1)
    {
        fclose(f);
        FAILURE_1(Configuration, "out of memory reading configuration file: %s", filename);
    }

    size_t read_bytes = fread(ch1, 1, ll1, f);
    if (read_bytes != ll1)
    {
        fclose(f);
        free(ch1);
        FAILURE_1(Configuration, "failed to read entire configuration file: %s", filename);
    }
    ch1[ll1] = '\0';

    theConfig = new CConfigurator(nullptr, NULL, NULL, ch1, ll1);
    fclose(f);
    free(ch1);
}

/**
 * Write theConfig to a file. If filename is '-', write to stdout.
 */
void write_configuration(const char *filename)
{
    FILE *f;

    if (!strcmp(filename, "-"))
    {
        f = stdout;
    }
    else
    {
        f = fopen(filename, "wb");
        if (!f)
            FAILURE_2(Configuration, "failed to open configuration file: %s (%s)",
                      filename, strerror(errno));
    }

    // theConfig->write_configuration(f);
    /* The above does'nt work - The emulator does not recognize the gui section if it appears after the sys0 section */
    CConfigurator *c = theConfig->find_child("gui");
    if (c != nullptr)
        c->write_configuration(f);
    sys0->write_configuration(f);

    fclose(f);
}

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

    mvwprintw(win, y, left_x, text);
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
        mvwprintw(my_win, nLines + 1, nCols - strlen(helptext), helptext);

    return my_win;
}

/**
 * curses: Display a centered window with a 'title' containing the 'text'.
 * Wait for a key, then close the window.
 *
 * 'text' may be multiple lines, separated by '\n'.
 **/
void show_text(const char *title, const char *text)
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
        mvwprintw(my_win, i, 1, p);
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
 * <UP>, <DOWN>: go to previous/next item
 * <ENTER>: if item's callback is NULL: exit menu,
 *          else call its callback
 * <F1>: Display item's description
 * <F2>: exit menu
 *
 * Returns the index of the last item selected,
 * or -1 if leaving via <F2>.
 **/
int show_menu(
    const char *title,
    MenuEntry_t entry[], int num_entries,
    int begin_y, int begin_x)
{
    const char *helptext = "F1 for help, F2 to Exit";

    int nLines = num_entries;
    int nCols = 0;

    for (int i = 0; i < num_entries; ++i)
        set_min_width(&nCols, entry[i].text);

    WINDOW *my_win = create_window(nLines, nCols, begin_x, begin_y, title, helptext);
    PANEL *my_panel = new_panel(my_win);
    update_panels();

    ITEM **my_item = (ITEM **)calloc(num_entries + 1, sizeof(ITEM *));
    for (int i = 0; i < num_entries; ++i)
        my_item[i] = new_item(entry[i].text, NULL);
    my_item[num_entries] = (ITEM *)NULL;

    MENU *my_menu = new_menu(my_item);
    set_menu_win(my_menu, my_win);
    set_menu_sub(my_menu, derwin(my_win, nLines, nCols, 1, 1));
    post_menu(my_menu);
    update_panels();
    doupdate();

    bool stay_in_loop = TRUE;
    int cur_idx = 0;
    while (stay_in_loop)
    {
        ITEM *cur = current_item(my_menu);
        cur_idx = item_index(cur);
        switch (wgetch(my_win))
        {
        case KEY_F(1):
            show_text(entry[cur_idx].text, entry[cur_idx].description);
            break;
        case KEY_F(2):
            cur_idx = -1;
            stay_in_loop = FALSE;
            break;
        case KEY_DOWN:
            menu_driver(my_menu, REQ_DOWN_ITEM);
            break;
        case KEY_UP:
            menu_driver(my_menu, REQ_UP_ITEM);
            break;
        case KEY_RETURN:
            SelectFuncPtr callback;

            callback = entry[cur_idx].select_callback;
            if (callback == NULL)
                stay_in_loop = FALSE;
            else
            {
                callback(item_name(cur));
                pos_menu_cursor(my_menu);
            }
            break;
        default:
            break;
        }
    }

    // Clean up
    unpost_menu(my_menu);
    free_menu(my_menu);
    del_panel(my_panel);
    wclear(my_win);
    wrefresh(my_win);
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
 * <UP> / <DOWN>, <ENTER>: go to previous/next field
 * <TAB>, <RIGHT> / Shift-<TAB>, <LEFT>: in enum fields, go to next/previous choice
 * <F1>: Display field description
 * <F2>: exit form
 * any other printable character is entered in the current field
 *
 * When the form is exited, call 'check_callback' (if not NULL).
 * If this returns FALSE, stay in the form.
 *
 * The entered values are returned in a 'num_entries' wide array.
 * The calling routine is responsible for freeing values' storage.
 *
 **/
FormValues_t show_form(
    const char *title,
    FormEntry_t entry[],
    FormValues_t preset,
    int num_entries,
    FormCheckFuncPtr check_callback)
{
    const char *helptext = "F1 for help, F2 to Exit";

    int nLines = num_entries * 2; // label + field
    int nCols = scrw - 20;
    FormValues_t values;

    for (int i = 0; i < num_entries; ++i)
    {
        set_min_width(&nCols, entry[i].label);
        set_min_width(&nCols, entry[i].preset);
    }

    if (nLines + 2 > scrh)
        FAILURE_1(Runtime, "Screen is too small for form, need %d lines", nLines + 2);

    WINDOW *my_win = create_window(nLines, nCols + 1, -1, -1, title, helptext);
    PANEL *my_panel = new_panel(my_win);
    update_panels();

    FIELD **my_fld = (FIELD **)calloc(num_entries + 1, sizeof(FIELD *));
    for (int i = 0; i < num_entries; ++i)
    {
        // Coordinates relative to the form's derwin() window!
        my_fld[i] = new_field(1, nCols - 1, 2 * i + 1, 1, FALSE, 0);
        if (entry[i].preset != NULL)
            set_field_buffer(my_fld[i], 0, preset[i]);
        set_field_back(my_fld[i], A_UNDERLINE); // Make the field visible.
        field_opts_off(my_fld[i], O_AUTOSKIP);  // Don't skip to next field when running off the end.
        field_opts_off(my_fld[i], O_STATIC);    // Use a dynamic length field ...
        set_max_field(my_fld[i], 256);          // ... of at most 256.
        set_field_pad(my_fld[i], ' ');          // Pad field with blanks.

        ValidationFuncPtr set_field_validation = entry[i].validation_callback;
        if (set_field_validation != NULL)
        {
            set_field_validation(my_fld[i]);
            field_opts_off(my_fld[i], O_PASSOK); // Valdidate on every exit
            if (field_type(my_fld[i]) == TYPE_INTEGER || field_type(my_fld[i]) == TYPE_NUMERIC)
                field_opts_off(my_fld[i], O_NULLOK); // Don't allow empty field
        }
    }
    my_fld[num_entries] = NULL;

    FORM *my_form = new_form(my_fld);
    set_form_win(my_form, my_win);
    set_form_sub(my_form, derwin(my_win, nLines, nCols, 1, 1));
    post_form(my_form);

    for (int i = 0; i < num_entries; ++i)
        mvwprintw(my_win, 2 * i + 1, 1, entry[i].label);

    form_driver(my_form, REQ_FIRST_FIELD);
    form_driver(my_form, REQ_END_FIELD);
    update_panels();
    doupdate();

    values = (FormValues_t)calloc(num_entries, sizeof(char *));
    bool checked;
    do
    {
        bool stay_in_loop = TRUE;
        while (stay_in_loop)
        {
            FIELD *cur_field = current_field(my_form);
            int cur_idx = field_index(cur_field);
            switch (int c = wgetch(my_win))
            {
            case KEY_F(1):
                show_text(entry[cur_idx].label, entry[cur_idx].description);
                break;
            case KEY_F(2):
                if (form_driver(my_form, REQ_VALIDATION) == E_OK)
                    stay_in_loop = FALSE;
                else
                    show_text("Error", "Field validation failed");
                break;
            case KEY_TAB:
                if (field_type(cur_field) == TYPE_ENUM)
                {
                    form_driver(my_form, REQ_NEXT_CHOICE);
                    form_driver(my_form, REQ_END_LINE);
                }
                break;
            case KEY_STAB: // Shift-<TAB> ?
                if (field_type(cur_field) == TYPE_ENUM)
                {
                    form_driver(my_form, REQ_PREV_CHOICE);
                    form_driver(my_form, REQ_END_LINE);
                }
                break;
            case KEY_DOWN:
            case KEY_RETURN:
                form_driver(my_form, REQ_NEXT_FIELD);
                form_driver(my_form, REQ_END_LINE);
                break;
            case KEY_UP:
                form_driver(my_form, REQ_PREV_FIELD);
                form_driver(my_form, REQ_END_LINE);
                break;
            case KEY_LEFT:
                if (field_type(cur_field) == TYPE_ENUM)
                {
                    form_driver(my_form, REQ_PREV_CHOICE);
                    form_driver(my_form, REQ_END_LINE);
                }
                else
                    form_driver(my_form, REQ_PREV_CHAR);
                break;
            case KEY_RIGHT:
                if (field_type(cur_field) == TYPE_ENUM)
                {
                    form_driver(my_form, REQ_NEXT_CHOICE);
                    form_driver(my_form, REQ_END_LINE);
                }
                else
                    form_driver(my_form, REQ_NEXT_CHAR);
                break;
            case KEY_BACKSPACE: // Ctrl-H
            case KEY_DEL:
                if (field_type(cur_field) != TYPE_ENUM)
                    form_driver(my_form, REQ_DEL_PREV);
                break;
            case KEY_UNDO: // AFAIK, this key is not configured in vt100's terminfo
                if (entry[cur_idx].preset != NULL)
                {
                    set_field_buffer(cur_field, 0, entry[cur_idx].preset);
                    form_driver(my_form, REQ_END_LINE);
                }
                break;
            default:
                if (field_type(cur_field) != TYPE_ENUM && isprint(c))
                    form_driver(my_form, c);
                break;
            }
        }

        for (int i = 0; i < num_entries; ++i)
        {
            char *buf = field_buffer(my_fld[i], 0);
            size_t len = strlen(buf);
            while (len > 0 && buf[len - 1] == field_pad(my_fld[i]))
                --len;
            values[i] = (char *)malloc(len + 1);
            strncpy(values[i], buf, len);
            values[i][len] = '\0';
        }

        checked = TRUE;
        if (check_callback != NULL)
        {
            checked = check_callback(entry, num_entries, values);

            if (!checked)
                for (int i = 0; i < num_entries; ++i)
                    free(values[i]);
        }
    } while (!checked);

    // Clean up
    unpost_form(my_form);
    free_form(my_form);
    del_panel(my_panel);
    wclear(my_win);
    wrefresh(my_win);
    delwin(my_win);
    update_panels();
    doupdate();

    for (int i = 0; i < num_entries; ++i)
        free_field(my_fld[i]);
    free(my_fld);

    return values;
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
    int num_entries = ARRAY_SIZE(text);
    const char *helptext = "Any key to continue";

    int nLines = num_entries;
    int nCols = 0;

    for (int i = 0; i < num_entries; ++i)
        set_min_width(&nCols, text[i]);

    string helptext_dur = string(helptext) + " (" + i2s(WAITSEC) + ")";
    WINDOW *my_win = create_window(nLines, nCols, -1, -1, NULL, helptext_dur.c_str());
    PANEL *my_panel = new_panel(my_win);

    for (int i = 0; i < num_entries; ++i)
        mvwprintw_center(my_win, i + 1, nCols, text[i]);

    update_panels();
    doupdate();

    const int factor = 10;
    const timespec req = {0, 1000L * 1000 * 1000 / factor}; // = 100,000,000 ns = 0.1 seconds
    int count = WAITSEC * factor;
    int maxx, maxy;
    getmaxyx(my_win, maxy, maxx);
    nodelay(my_win, TRUE);
    while (--count > 0 && wgetch(my_win) == ERR)
    {
        nanosleep(&req, NULL);
        if (count % factor == 0)
        {
            string dur = string("(") + i2s(count / factor) + ")";
            // No idea why the coordinates need offsets...
            mvwprintw(my_win, maxy - 1, maxx - 2 - strlen(dur.c_str()), dur.c_str());
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

/**
 * Return the config object in PCI slot "<bus>.<slot>",
 * or nullptr if there's no object in that slot.
 **/
CConfigurator *get_pcislot(const char *bus_slot)
{
    return sys0->find_child((string("pci") + bus_slot).c_str());
}

/**
 * Return the config object in PCI slot "<bus>.<slot>",
 * or nullptr if there's no object in that slot.
 **/
CConfigurator *get_pcislot(int bus, int slot)
{
    return get_pcislot((i2s(bus) + "." + i2s(slot)).c_str());
}

/**
 * Test whether there's a config object in PCI slot "<bus>.<slot>".
 */
bool is_pcislot_free(int bus, int slot)
{
    return (get_pcislot(bus, slot) == nullptr);
}

/**
 * Return the first PCI slot "<bus>.<slot>" which does not have a config object.
 *
 * The calling procedure is responsible for freeing up the storage of the return value.
 */
char *find_first_free_pcislot(void)
{
    for (int bus = 0; bus <= 1; ++bus)
        for (int slot = 1; slot < ((bus == 0) ? 4 : 6); ++slot)
            if (is_pcislot_free(bus, slot))
            {
                string bus_slot = i2s(bus) + "." + i2s(slot);
                return strdup(bus_slot.c_str());
            }
    return NULL;
}

/**
 * Return the name of the config object in PCI slot "<bus>.<slot>",
 * or "-" if there's no object in that slot.
 */
char *device_in_pcislot(int bus, int slot)
{
    string pcislot = string("pci") + i2s(bus) + "." + i2s(slot);
    CConfigurator *c = sys0->find_child(pcislot.c_str());
    if (c == nullptr)
        return (char *)"-";
    else
        return c->get_myValue();
}

/**
 * General validation routines
 **/

void validation_yes(FIELD *field)
{
    const char *choices[] = {
        STR_YES,
        NULL};
    set_field_type(field, TYPE_ENUM, choices, FALSE, TRUE);
}

void validation_yes_no(FIELD *field)
{
    const char *choices[] = {
        STR_NO,
        STR_YES,
        NULL};
    set_field_type(field, TYPE_ENUM, choices, FALSE, TRUE);
}

void validation_optional_datetime(FIELD *field)
{
    // Preceeding and trailing optional blanks added for trimming the field,
    // see form_field_validation(3X) manpage
    set_field_type(field, TYPE_REGEXP,
                   "^ *"
                   "([0-8]{4}-[0-8]{2}-[0-8]{2})?"
                   "( [0-8]{2}:[0-8]{2}:[0-8]{2})?"
                   " *$");
}

void validation_file(FIELD *field)
{
    /* NYI */
}

void validation_mac(FIELD *field)
{
    set_field_type(field, TYPE_REGEXP,
                   "^ *"
                   "[0-9A-Fa-f]{2}(-[0-9A-Fa-f]{2}){5}"
                   " *$");
}

void validation_pcislot(FIELD *field)
{
    // Only include free PCI slots
    vector<char *> choices;
    for (int bus = 0; bus <= 1; ++bus)
        for (int slot = 1; slot <= ((bus == 0) ? 4 : 6); ++slot)
            if (is_pcislot_free(bus, slot))
            {
                string pcislot = i2s(bus) + "." + i2s(slot);
                choices.push_back(strdup(pcislot.c_str()));
            }
    choices.push_back(NULL);

    set_field_type(field, TYPE_ENUM, choices.data(), FALSE, TRUE);

    for (size_t i = 0; i < choices.size() - 1; ++i)
        free(choices[i]);
}

/**
 * General helper routines
 **/

int fentry_index(FormEntry_t *entry, int num_entries, const char *name)
{
    for (int i = 0; i < num_entries; ++i)
        if (!strcmp(entry[i].label, name))
            return i;

    FAILURE_1(Logic, "Form entry '%s' not found", name);
}

/**
 * Routine for adding disks
 **/

void validation_disk_type(FIELD *field)
{
    const char *choices[] = {
        "file",
        "device",
        "ramdisk",
        NULL};
    set_field_type(field, TYPE_ENUM, choices, FALSE, TRUE);
}

void validation_disk_file(FIELD *field)
{
    // can be filename, device name, or empty for ramdisk
    /* NYI */
}

void validation_disk_hd_cd(FIELD *field)
{
    const char *choices[] = {
        "disk",
        "cd-rom",
        NULL};
    set_field_type(field, TYPE_ENUM, choices, FALSE, TRUE);
}

void validation_disk_autocreate_size(FIELD *field)
{
    // Preceeding and trailing optional blanks added for trimming the field,
    // see form_field_validation(3X) manpage
    // (..)? to allow leaving field empty
    set_field_type(field, TYPE_REGEXP, "^ *([0-9]+[KkMmGg])? *$");
}

bool check_add_disks(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    int idx = fentry_index(entry, num_entries, "Type");
    char *disk_type = values[idx];
    idx = fentry_index(entry, num_entries, "File / Device name");
    if ((!strcmp(disk_type, "file") || !strcmp(disk_type, "device")) &&
        !strcmp(values[idx], ""))
    {
        string msg = string("No ") + disk_type + " name has been specified.";
        show_text("Error", msg.c_str());
        return FALSE;
    }
    idx = fentry_index(entry, num_entries, "Autocreate size");
    if (!strcmp(disk_type, "ramdisk") && !strcmp(values[idx], ""))
    {
        show_text("Error", "Autocreate size can't be empty for type 'ramdisk'.");
        return FALSE;
    }
    return TRUE;
}

/**
 * Add disk 'disk_name' to the controller 'parent' of the configuration.
 **/
void add_disks(const char *title, const char *disk_name, CConfigurator *parent)
{
    FormEntry_t entry[] = {
        {"Type", "file", NULL, // no name - config entry myValue
         "Disks can be emulated in several ways.",
         validation_disk_type},
        {"File / Device name", "", NULL, // "file" | "device",
         "Enter the path to the file or device to use for this disk.\n"
         "This parameter is ignored for type 'ramdisk'.",
         validation_disk_file},
        {"Harddisk or CD-ROM", "disk", "cdrom",
         "Do you want the OS to see this as a hard-disk, or as a cd-rom?\n"
         "This parameter is ignored for type 'ramdisk' and floppy drives, as they always are disks.",
         validation_disk_hd_cd},
        {"Autocreate size", "", NULL, // "autocreate_size" | "size"
         "For type 'file': Do you want the program to create it? And in what size?\n"
         "Leave blank to not autocreate the file.\n"
         "For type 'ramdisk': Required to be non-empty.\n"
         "For type 'device': Ignored."
         "The file/ramdisk will be created the first time the emulator runs.\n"
         "Format: number followed by K (kilo), M (mega), or G (giga)",
         validation_disk_autocreate_size},
        {"Read-only?", STR_NO, "read_only",
         "Should the disk be set to read-only?\n"
         "This parameter is ignored for CD-ROMs (always true),\n"
         "and for ramdisks (always false)",
         validation_yes_no},
        {"Disk model number", "", "model_number",
         "Would you like to set a disk model number?",
         NULL},
        {"Revision number", "", "rev_number",
         "Would you like to set a revision number?",
         NULL},
        {"Serial number", "", "serial_number",
         "Would you like to set a serial number?",
         NULL}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t values, preset;
    int idx;
    char *disk_type;

    c = parent->find_child(disk_name);

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "Type");
    preset[idx] = disk_type = (char *)((c != nullptr) ? c->get_myValue() : entry[idx].preset);

    for (int i = 0; i < num_entries; ++i)
    {
        if (i == idx)
            continue;

        if (!strcmp(entry[i].label, "Harddisk or CD-ROM") && c != nullptr)
        {
            bool is_cdrom = !strcmp(c->get_text_value(entry[i].name, STR_NO), STR_YES);
            preset[i] = (char *)(is_cdrom ? "cd-rom" : "disk");
            continue;
        }

        if (entry[i].name != NULL)
        {
            char *p;
            if (c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
                preset[i] = p;
            else
                preset[i] = (char *)entry[i].preset;
            continue;
        }

        if (c != nullptr)
        {
            if (!strcmp(entry[i].label, "File / Device name") &&
                (!strcmp(disk_type, "file") || !strcmp(disk_type, "device")))
            {
                preset[i] = c->get_text_value(disk_type, "?");
                continue;
            }
            if (!strcmp(entry[i].label, "Autocreate size"))
            {
                if (!strcmp(disk_type, "ramdisk"))
                    preset[i] = c->get_text_value("size", "?");
                else if (!strcmp(disk_type, "file"))
                    preset[i] = c->get_text_value("autocreate_size", "");
                else
                    preset[i] = (char *)entry[i].preset;
                continue;
            }

            FAILURE_1(Logic, "No handling of preset '%s'", entry[i].label);
        }

        preset[i] = (char *)entry[i].preset;
    }

    values = show_form(title, entry, preset, num_entries, check_add_disks);

    idx = fentry_index(entry, num_entries, "Type");
    disk_type = values[idx];
    bool is_ramdisk = !strcmp(disk_type, "ramdisk");

    idx = fentry_index(entry, num_entries, "Harddisk or CD-ROM");
    bool is_cdrom = !strcmp(values[idx], "cd-rom");
    if (is_ramdisk && is_cdrom) // also for floppy drives
    {
        free(values[idx]);
        values[idx] = strdup("disk");
    }

    idx = fentry_index(entry, num_entries, "Read-only?");
    if (is_ramdisk)
    {
        free(values[idx]);
        values[idx] = strdup(STR_NO);
    }
    else if (is_cdrom)
    {
        free(values[idx]);
        values[idx] = strdup(STR_YES);
    }

    if (c != nullptr)
        parent->remove_child(c->get_myName());
    c = new CConfigurator(parent, (char *)disk_name, disk_type);

    for (int i = 0; i < num_entries; ++i)
    {
        if (!strcmp(entry[i].label, "Harddisk or CD-ROM"))
        {
            if (!strcmp(values[i], "cd-rom"))
                c->set_value(strdup(entry[i].name), strdup(STR_YES));
            else
                c->set_value(strdup(entry[i].name), strdup(STR_NO));
            continue;
        }

        if (entry[i].name != NULL)
        {
            c->set_value(strdup(entry[i].name), strdup(values[i]));
            continue;
        }

        if (!strcmp(entry[i].label, "File / Device name") &&
            (!strcmp(disk_type, "file") || !strcmp(disk_type, "device")))
            c->set_value(strdup(disk_type), strdup(values[i]));

        if (!strcmp(entry[i].label, "Autocreate size"))
        {
            int last = strlen(values[i]) - 1;
            values[i][last] = toupper(values[i][last]);
            if (is_ramdisk)
                c->set_value(strdup("size"), strdup(values[i]));
            if (!strcmp(disk_type, "file") && strcmp(values[i], ""))
                c->set_value(strdup("autocreate_size"), strdup(values[i]));
        }
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
}

/**
 * Add SCSI disks (up to ID 'max_scsi_id') to the controller 'parent' of the configuration.
 **/
void add_scsi_disks(const char *title, const int max_scsi_id, CConfigurator *parent)
{
    vector<MenuEntry_t> entry;

    entry.push_back({"none", "Stop adding disks", NULL});

    for (int i = 0; i < max_scsi_id; ++i)
    {
        string desc = "Target " + i2s(i);
        string text = "disk0." + i2s(i);
        CConfigurator *c = parent->find_child(text.c_str());
        if (c != nullptr)
            text = text + "  " + c->get_myValue();
        else
            text = text + "  -";
        entry.push_back({strdup(text.c_str()), strdup(desc.c_str()), NULL});
    }
    int num_entries = entry.size();

    while (TRUE)
    {
        int sel = show_menu(title, entry.data(), num_entries, MENU_4TH_LEVEL);
        if (sel <= 0)
            break;

        char disk_name[sizeof("disk0.xx")];
        strncpy(disk_name, entry[sel].text, sizeof(disk_name));
        disk_name[(max_scsi_id < 10) ? 7 : 8] = '\0';

        string subtitle = string(title) + ": " + disk_name;
        add_disks(subtitle.c_str(), disk_name, parent);

        // Update display of disk entry
        CConfigurator *c = parent->find_child(disk_name);
        free((void *)entry[sel].text);
        string text = disk_name + string("  ") + c->get_myValue();
        entry[sel].text = strdup(text.c_str());
    }

    // Clean up
    for (int i = 1; i <= max_scsi_id; ++i)
    {
        free((void *)entry[i].text);
        free((void *)entry[i].description);
    }
}

/**
 * GUI configuration
 **/

#ifdef HAVE_SDL
void validation_gui_sdl_mousespeed(FIELD *field)
{
    set_field_type(field, TYPE_NUMERIC, 1, 0.1, 10.0);
}

void validation_gui_sdl_scaleratio(FIELD *field)
{
    set_field_type(field, TYPE_INTEGER, 1, 0, 10);
}

bool check_gui_sdl(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "keyboard use mapping?")], STR_YES) &&
        !strcmp(values[fentry_index(entry, num_entries, "keyboard map")], ""))
    {
        show_text("Error", "No keyboard map has been specified.");
        return FALSE;
    }
    return TRUE;
}

// Form for SDL GUI
void edit_gui_sdl(const char *title)
{
    FormEntry_t entry[] = {
        {"keyboard use mapping?", STR_NO, "keyboard.use_mapping",
         "Use a keymap file to translate host keys to guest scancodes.\n"
         "Enable this if you use a non-US keyboard layout.\n"
         "If set to 'yes', keyboard.map must be filled out.",
         validation_yes_no},
        {"keyboard map", "keys.map", "keyboard.map",
         "The keymap file to use when keyboard.use_mapping is enabled.",
         validation_file},
        {"mouse.speed favtor", "1.0", "mouse.speed",
         "Multiplier applied to host mouse motion before it is passed to the guest.\n"
         "Use a value below 1.0 to slow the guest pointer down (e.g. 0.5 for half speed),\n"
         "or above 1.0 (up to 10.0) to speed it up.",
         validation_gui_sdl_mousespeed},
        {"mouse invert x?", STR_NO, "mouse.invert_x",
         "Reverse the direction of host mouse motion on the horizontal axis.",
         validation_yes_no},
        {"mouse invert y?", STR_NO, "mouse.invert_y",
         "Reverse the direction of host mouse motion on the vertical axis.",
         validation_yes_no},
        {"video linear", STR_YES, "video.linear",
         "Filtering used when the guest display is scaled to the window size:\n"
         "true = linear (smooth), false = nearest neighbor (sharp pixels).",
         validation_yes_no},
        {"video scale ratio", "0", "video.scale_ratio",
         "Integer scale factor for the emulator window (1 - 8). For example,"
         "2 renders the guest's 640x480 display in a 1280x960 window. The default\n"
         "is 0, which sizes the window automatically from the OS display scale\n"
         "(DPI) setting.",
         validation_gui_sdl_scaleratio},
        {"video scale change enable?", STR_NO, "video.scale_change_enable",
         "If enabled, the display scale ratio can be adjusted on the fly\n"
         "while the emulator is running, without restarting.\n"
         "The change is not persisted back to the config file.\n"
         "The runtime defaults are:\n"
         "  Ctrl+PageUp   - increase scale by 1 (clamped at 8x)\n"
         "  Ctrl+PageDown - decrease scale by 1 (clamped at 1x)\n"
         "Optional overrides to the keys used can be configured directly in the config file.\n"
         "These hotkey bindings take effect only\n"
         "when runtime display scale changes are enabled.",
         validation_yes_no}
        // TODO: Implement editing of hotkey.*
    };
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;

    c = theConfig->find_child("gui");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_gui_sdl);

    if (c != nullptr)
        theConfig->remove_child(c->get_myName());
    c = new CConfigurator(theConfig, (char *)"gui", (char *)"sdl");

    for (int i = 0; i < num_entries; ++i)
        c->set_value(strdup(entry[i].name), strdup(values[i]));

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
}
#endif // HAVE_SDL

#ifdef HAVE_X11
bool check_gui_x11(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "keyboard use mapping?")], STR_YES) &&
        !strcmp(values[fentry_index(entry, num_entries, "keyboard map")], ""))
    {
        show_text("Error", "No keyboard map has been specified.");
        return FALSE;
    }
    return TRUE;
}

// Form for X11 GUI
void edit_gui_x11(const char *title)
{
    FormEntry_t entry[] = {
        {"keyboard use mapping?", STR_NO, "keyboard.use_mapping?",
         "Use a keymap file to translate host keys to guest scancodes.\n"
         "Enable this if you use a non-US keyboard layout.\n"
         "If set to 'yes', keyboard.map must be filled out.",
         validation_yes_no},
        {"keyboard map", "keys.map", "keyboard.map",
         "The keymap file to use when keyboard.use_mapping is enabled.",
         validation_file},
        {"private colormap?", STR_NO, "private_colormap",
         "Use a private colormap?",
         validation_yes_no}};
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;

    c = theConfig->find_child("gui");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_gui_x11);

    if (c != nullptr)
        theConfig->remove_child(c->get_myName());
    c = new CConfigurator(theConfig, (char *)"gui", (char *)"x11");

    for (int i = 0; i < num_entries; ++i)
    {
        c->set_value(strdup(entry[i].name), strdup(values[i]));
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
}
#endif // HAVE_X11

// Menu for GUI
void edit_gui(const char *title)
{
    MenuEntry_t entry[] = {
        {"none", "No GUI. Graphics cards will not be supported.", NULL}
#if defined(HAVE_SDL)
        ,
        {"sdl", "Simple Directmedia Layer. Preferred GUI mechanism.", edit_gui_sdl}
#endif
#if 0 && defined(HAVE_X11)
        ,
        {"X11", "Unix X-Windows GUI support.", edit_gui_x11}
#endif
#if 0 && defined(_WIN32)
        ,
        {"win32", "Windows 32 GUI support.", edit_gui_win32}
#endif
    };
    int num_entries = ARRAY_SIZE(entry);

    if (num_entries == 1)
    {
        show_text(title, "Sorry, the GUI is not available! (no SDL support found).");
        return;
    }

    int sel = show_menu(title, entry, num_entries, MENU_2ND_LEVEL);

    if (sel == 0)
    {
        CConfigurator *c = theConfig->find_child("gui");
        if (c != nullptr)
            theConfig->remove_child(c->get_myName());
    }
}

/**
 * Tsunami system settings configuration
 **/

void validation_tsunami_memory(FIELD *field)
{
    char *choices[HI_MEM_BITS + 1 - LOW_MEM_BITS + 1];

    for (int i = LOW_MEM_BITS; i <= HI_MEM_BITS; ++i)
        choices[i - LOW_MEM_BITS] = strdup(memory_bits2size(i).c_str());
    choices[HI_MEM_BITS + 1 - LOW_MEM_BITS] = NULL;

    set_field_type(field, TYPE_ENUM, choices, FALSE, TRUE);

    for (int i = LOW_MEM_BITS; i <= HI_MEM_BITS; ++i)
        free(choices[i - LOW_MEM_BITS]);
}

bool check_tsunami(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "rom.srm file")], ""))
    {
        show_text("Error", "No ROM filename has been specified.");
        return FALSE;
    }
    return TRUE;
}

// Form for Tsunami
void edit_tsunami(const char *title)
{
    FormEntry_t entry[] = {
        {"rom.srm file",
#if defined(_WIN32)
         "rom\\cl67srmrom.exe",
#elif defined(__VMS)
         "[.ROM]CL67SRMROM.EXE",
#else
         "rom/cl67srmrom.exe",
#endif
         "rom.srm",
         "Location of the SRM ROM image.\n"
         "This file is required.",
         validation_file},
        {"rom.flash file",
#if defined(_WIN32)
         "rom\\flash.rom",
#elif defined(__VMS)
         "[.ROM]FLASH.ROM",
#else
         "rom/flash.rom",
#endif
         "rom.flash",
         "Location of the Flash ROM image",
         validation_file},
        {"rom.dpr file",
#if defined(_WIN32)
         "rom\\dpr.rom",
#elif defined(__VMS)
         "[.ROM]DPR.ROM",
#else
         "rom/dpr.rom",
#endif
         "rom.dpr",
         "Location of the DPR EEPROM image.",
         validation_file},
        {"rom.toy file",
#if defined(_WIN32)
         "rom\\toy.rom",
#elif defined(__VMS)
         "[.ROM]TOY.ROM",
#else
         "rom/toy.rom",
#endif
         "rom.toy",
         "Location of the CMOS/TOY NVRAM image.\n"
         "Preserves SRM CMOS settings (such as heap_expand)\n"
         "across emulator restarts, like the battery-backed\n"
         "CMOS on real hardware.",
         validation_file},
        {"memory size", "256M", NULL, // "memory.bits"
         "Amount of RAM memory.\n"
         "Your system should have enough free memory\n"
         "to emulate the amount you choose here.",
         validation_tsunami_memory},
        {"fixed date", "", "time",
         "Set a fixed date and time when the VM starts.\n"
         "By default (empty value), the VM's date and time is initialized\n"
         "to the current host date and time at startup.\n"
         "You can set a fixed date and time instead,\n"
         "Format: 'YYYY-MM-DD' or 'YYYY-MM-DD HH:MM:SS'",
         validation_optional_datetime},
        {"arc_year_compat?", STR_NO, "arc_year_compat",
         "Should the reported year be compatible with Windows?\n"
         "This only affects the year reported to the guest.\n"
         "Select 'yes' if you are planning to run Windows OSes.",
         validation_yes_no},
        {"Exit on PAL_halt?", STR_NO, "exit_on_pal_halt",
         "Should the VM power off on the guest's request?",
         validation_yes_no}};
    const int num_entries = ARRAY_SIZE(entry);
    FormValues_t preset;
    int idx;

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "memory size");
    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (i == idx && (p = sys0->get_text_value("memory.bits", NULL)))
            preset[i] = (char *)memory_bits2size(atoi(p)).c_str();
        else if (entry[i].name != NULL && (p = sys0->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_tsunami);

    idx = fentry_index(entry, num_entries, "memory size");
    sys0->set_value(strdup("memory.bits"), strdup(i2s(memory_size2bits(values[idx])).c_str()));

    for (int i = 0; i < num_entries; ++i)
        if (entry[i].name != NULL && (strcmp(entry[i].name, "time") || strcmp(values[i], "")))
            sys0->set_value(strdup(entry[i].name), strdup(values[i]));

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
}

/**
 * EV68 CPU configuration
 **/

void validation_ev68cb_cpuspeed(FIELD *field)
{
    set_field_type(field, TYPE_INTEGER, 2, 10, 1250);
}

void validation_ev68cb_max_ticks(FIELD *field)
{
    set_field_type(field, TYPE_INTEGER, 1, 1, INT_MAX);
}

// Form for EV68CB
void edit_ev68cb(const char *title)
{
    vector<FormEntry_t> entry; // (enabled, speed, max_instr_per_tick [,nohle]) * MAX_CPUS

    for (int i = 0; i < MAX_CPUS; ++i)
    {
        const string cpu = "cpu" + i2s(i);
        const string enabled = cpu + " enabled?";
        if (i == 0)
            entry.push_back({strdup(enabled.c_str()), STR_YES, NULL,
                             "CPU 0 is always enabled.",
                             validation_yes});
        else
            entry.push_back({strdup(enabled.c_str()), STR_NO, NULL,
                             "Enable CPU?\n"
                             "If not enabled, all parameters refering to this CPU will be ignored.",
                             validation_yes_no});

        const string speed = cpu + ".speed";
        entry.push_back({strdup(speed.c_str()), "500", "speed",
                         "The CPU speed reported to the guest platform (in MHz, ranging from 10 to 1250).\n"
                         "This does not affect the speed of the emulation.",
                         validation_ev68cb_cpuspeed});
#ifndef ES40_JIT
        const string nohle = cpu + ".palcode.vms.nohle?";
        entry.push_back({strdup(nohle.c_str()), STR_NO, "palcode.vms.nohle",
                         "Disable the high-level emulation (HLE) of the OpenVMS PALcode\n"
                         "and run the real SRM PALcode instead.",
                         validation_bool});
#endif
        const string max_ticks = cpu + ".timer.max_instr_per_tick";
        entry.push_back({strdup(max_ticks.c_str()), "1250000", "timer.max_instr_per_tick",
                         "Upper bound on guest instructions retired between 1024 Hz interval-timer\n"
                        "ticks. Real Alpha silicon topped out at 1.25 GHz (EV68).\n"
                        "A CPU that reaches this bound before the next wall-clock tick is due is\n"
                        "held (slept) until the tick lands, so the guest never sees more than this\n"
                        "many instructions per tick and its tick-counted clock stays wall-clock accurate.",
                         validation_ev68cb_max_ticks});
    }
    const int num_entries = entry.size();
    const int entries_per_cpu = num_entries / MAX_CPUS;
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < MAX_CPUS; ++i)
    {
        const string cpu = "cpu" + i2s(i);
        const string enabled = cpu + " enabled?";
        idx = fentry_index(entry.data(), num_entries, enabled.c_str());
        c = sys0->find_child(cpu.c_str());
        for (int k = i * entries_per_cpu; k < (i + 1) * entries_per_cpu; ++k)
            if (entry[k].name != NULL)
            {
                char *p;
                if (c != nullptr && (p = c->get_text_value(entry[k].name)) != NULL)
                    preset[k] = p;
                else
                    preset[k] = (char *)entry[k].preset;
            }
            else
            {
                // Just a safeguard...
                if (k != idx)
                    FAILURE_1(Logic, "Field is not '%s'", enabled.c_str());
                if (c != nullptr)
                    preset[k] = (char *)STR_YES;
                else
                    /* Always use entry presets for CPUs not enabled.
                     * We can't decide by looking at sys0's children
                     * whether the missing of their entries is intentional or not yet configured.
                     */
                    preset[k] = (char *)entry[k].preset;
            }
    }

    FormValues_t values = show_form(title, entry.data(), preset, num_entries, NULL);

    for (int i = 0, j = 0; i < MAX_CPUS; ++i)
    {
        const string enabled = "cpu" + i2s(i) + " enabled?";
        idx = fentry_index(entry.data(), num_entries, enabled.c_str());
        if (!strcmp(values[idx], STR_YES))
        {
            const string cpu = "cpu" + i2s(j++);
            c = sys0->find_child(cpu.c_str());
            if (c != nullptr)
                sys0->remove_child(c->get_myName());
            c = new CConfigurator(sys0, (char *)cpu.c_str(), (char *)"ev68cb");
            for (int k = i * entries_per_cpu; k < (i + 1) * entries_per_cpu; ++k)
                if (entry[k].name != NULL)
                    c->set_value(strdup(entry[k].name), strdup(values[k]));
        }
        else
        {
            const string cpu = "cpu" + i2s(i);
            c = sys0->find_child(cpu.c_str());
            if (c != nullptr)
                sys0->remove_child(c->get_myName());
        }
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
    {
        free((void *)entry[i].label);
        free(values[i]);
    }
    free(values);
    free(preset);
}

/**
 * ALI 1543 configuration
 **/

void validation_ali_console(FIELD *field)
{
    const char *choices[] = {
        "serial",
        "graphics",
        NULL};
    set_field_type(field, TYPE_ENUM, choices, FALSE, TRUE);
}

// Form for ALI 1543
void edit_ali(const char *title)
{
    FormEntry_t entry[] = {
        {"Console Output", "serial", "vga_console",
         "Where would you like console output to go?\n"
         "This is the SRM 'console' variable.\n"
         "WARNING: for the 'graphics' option to work,\n"
         "you need to configure a VGA card and a GUI.",
         validation_ali_console},
        {"LPT Output", "", "lpt.outfile",
         "Where would you like printer output to go?\n"
         "Output from the printer port will be saved to this file.\n"
         "Leave blank if not wanted.",
         validation_file},
        {"M7101 PMU enabled?", STR_YES, NULL,
         "Enable the M7101 power-management / ACPI device at PCI 0:17?",
         validation_yes_no},
        {"USB controller enabled?", STR_YES, NULL,
         "Enable the USB OHCI controller at PCI 0:19?",
         validation_yes_no}};
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child(PCI_SLOT_ALI);
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    char *p;
    idx = fentry_index(entry, num_entries, "Console Output"); // name == "vga_console"
    if (c != nullptr && (p = c->get_text_value(entry[idx].name)) != NULL)
        preset[idx] = (char *)(!strcmp(p, STR_YES) ? "graphics" : "serial");
    else
        preset[idx] = (char *)entry[idx].preset;

    idx = fentry_index(entry, num_entries, "LPT Output");
    if (c != nullptr && (p = c->get_text_value(entry[idx].name)) != NULL)
        preset[idx] = p;
    else
        preset[idx] = (char *)entry[idx].preset;

    /* Always use entry presets for PMU and USB controller.
     * We can't decide by looking at sys0's children
     * whether the missing of their entries is intentional or not yet configured.
     */
    idx = fentry_index(entry, num_entries, "M7101 PMU enabled?");
    preset[idx] = (char *)entry[idx].preset;
    idx = fentry_index(entry, num_entries, "USB controller enabled?");
    preset[idx] = (char *)entry[idx].preset;

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    c = sys0->find_child(PCI_SLOT_ALI);
    if (c != nullptr)
        sys0->remove_child(c->get_myName());
    c = new CConfigurator(sys0, (char *)PCI_SLOT_ALI, (char *)"ali");

    idx = fentry_index(entry, num_entries, "Console Output");
    c->set_value(strdup(entry[idx].name), strdup(!strcmp(values[idx], "graphics") ? STR_YES : STR_NO));

    idx = fentry_index(entry, num_entries, "LPT Output");
    if (strcmp(values[idx], ""))
        c->set_value(strdup(entry[idx].name), strdup(values[idx]));

    idx = fentry_index(entry, num_entries, "M7101 PMU enabled?");
    if (!strcmp(values[idx], STR_YES))
    {
        c = sys0->find_child(PCI_SLOT_PMU);
        if (c == nullptr)
            c = new CConfigurator(sys0, (char *)PCI_SLOT_PMU, (char *)"ali_pmu");
    }
    else
    {
        sys0->remove_child(PCI_SLOT_PMU);
    }

    idx = fentry_index(entry, num_entries, "USB controller enabled?");
    if (!strcmp(values[idx], STR_YES))
    {
        c = sys0->find_child(PCI_SLOT_USB);
        if (c == nullptr)
            c = new CConfigurator(sys0, (char *)PCI_SLOT_USB, (char *)"ali_usb");
    }
    else
    {
        sys0->remove_child(PCI_SLOT_USB);
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
}

/**
 * PCI bus configuration
 **/

void show_pcislots(const char *title)
{
    string lines;
    for (int bus = 0; bus <= 1; ++bus)
        for (int slot = 1; slot <= ((bus == 0) ? 4 : 6); ++slot)
        {
            string o = string("pci") + i2s(bus) + "." + i2s(slot) + ": " + device_in_pcislot(bus, slot);
            lines.append(o);
            if (bus == 0 || slot < 6)
                lines.append("\n");
        }

    show_text(title, lines.c_str());
}

bool check_pci_vga_s3(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    return strcmp(values[fentry_index(entry, num_entries, "rom file")], "");
}

// Form for S3 VGA PCI card
void edit_pci_vga_s3(const char *title)
{
    char *first_free_pcislot = find_first_free_pcislot();
    if (first_free_pcislot == NULL)
    {
        show_text(title, "No free PCI slots");
        return;
    }

    FormEntry_t entry[] = {
        {"PCI slot", first_free_pcislot, NULL,
         "Which PCI slot should the VGA card be on?\n"
         "Only free PCI slots are listed.\n"
         "AFAIK, VGA should always be on pci0.x",
         validation_pcislot},
        {"rom file",
#if defined(_WIN32)
         "rom\\VGABIOSFILE.bin",
#elif defined(__VMS)
         "[.ROM]VGABIOSFILE.bin",
#else
         "rom/VGABIOSFILE.bin",
#endif
         "rom",
         "Where can the VGA BIOS ROM image be found?\n"
         "Real S3 Trio64 VGA BIOS images work, GNU vgabios project also works,\n"
         "but may not enable or support all S3 features.\n"
         "This file is required.",
         validation_file}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child_myValue("s3");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "PCI slot");
    if (c != nullptr)
        preset[idx] = c->get_myName() + strlen("pci");
    else
        preset[idx] = (char *)entry[idx].preset;
    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_pci_vga_s3);

    if (c != nullptr)
        sys0->remove_child(c->get_myName());

    idx = fentry_index(entry, num_entries, "PCI slot");
    c = get_pcislot(values[idx]);
    if (c != nullptr)
        sys0->remove_child(c->get_myName());
    c = new CConfigurator(sys0, (char *)(string("pci") + values[idx]).c_str(), (char *)"s3");

    for (int i = 0; i < num_entries; ++i)
        if (i != idx && entry[i].name != NULL)
            c->set_value(strdup(entry[i].name), strdup(values[i]));

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
    free(first_free_pcislot);
}

// Form for VGA PCI card
void edit_pci_vga(const char *title)
{
    MenuEntry_t entry[] = {
        {"none", "No graphics card", NULL},
        {"s3", "S3 Trio 64", edit_pci_vga_s3}
#if 0 && defined(HAVE_CIRRUS)
        ,
        {"cirrus", "Cirrus CL-GD542x", edit_pci_vga_cirrus}
#endif
#if 0 && defined(HAVE_RADEON)
        ,
        {"radeon", "ATI Radeon 7500", edit_pci_vga_radeon}
#endif
    };
    int num_entries = ARRAY_SIZE(entry);

    int sel = show_menu(title, entry, num_entries, MENU_3RD_LEVEL);

    if (sel == 0)
    {
        // Remove any VGA cards that may have been added
        CConfigurator *c;
        for (int i = 1; i < num_entries; ++i)
            while ((c = sys0->find_child_myValue(entry[i].text)) != nullptr)
                sys0->remove_child(c->get_myName());
    }
}

void validation_pci_dec21143_type(FIELD *field)
{
    vector<const char *> choices;

#ifdef HAVE_PCAP
    choices.push_back("pcap");
#endif
#ifdef HAVE_TAP_NET
    choices.push_back("tap");
#endif
#ifdef HAVE_VMNET
    choices.push_back("vmnet");
#endif
    choices.push_back(NULL);

    set_field_type(field, TYPE_ENUM, choices.data(), FALSE, TRUE);
}

void validation_pci_dec21143_adapter(FIELD *field)
{
    vector<const char *> choices;

    choices.push_back("list");

#ifdef HAVE_PCAP
    /* Get a list of network interfaces and
     * add them to the list.
     */
    pcap_if_t *alldevs;
    pcap_if_t *d;
    char errbuf[PCAP_ERRBUF_SIZE];

    if (pcap_findalldevs(&alldevs, errbuf) == -1)
    {
        /* No devices to add. */
        string msg = "Error in pcap_findalldevs:\n" + string(errbuf);
        show_text("Error", msg.c_str());
    }
    else
    {
        for (d = alldevs; d; d = d->next)
            choices.push_back(d->name);
    }
#endif
    choices.push_back(NULL);

    set_field_type(field, TYPE_ENUM, choices.data(), FALSE, TRUE);
}

void validation_pci_dec21143_queue(FIELD *field)
{
    set_field_type(field, TYPE_INTEGER, 1, 1, INT_MAX);
}

// Form for DEC21143 PCI NIC
void edit_pci_dec21143(const char *title)
{
#if !defined(HAVE_PCAP) && !defined(HAVE_TAP_NET) && !defined(HAVE_VMNET)
    show_text("Error", "This program has been compiled with no network support.");
    return;
#else
    char *first_free_pcislot = find_first_free_pcislot();
    if (first_free_pcislot == NULL)
    {
        show_text(title, "No free PCI slots");
        return;
    }

    FormEntry_t entry[] = {
        {"PCI slot", first_free_pcislot, NULL,
         "Which PCI slot should the NIC be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot},
        {"type",
#if defined(HAVE_PCAP)
         "pcap",
#elif defined(HAVE_TAP_NET)
         "tap0",
#elif defined(HAVE_VMNET)
         "vmnet",
#else
         "none",
#endif
         "type",
         "How should this NIC connect to the host network?"
#ifdef HAVE_PCAP
         "\n"
         "'pcap' captures/injects packets on an existing host adapter\n"
         "(works everywhere, but usually requires a bridge/promiscuous\n"
         "setup on the host to reach a wired LAN)."
#endif
#ifdef HAVE_TAP_NET
         "\n"
         "'tap' uses a TAP virtual network device (Linux/FreeBSD/NetBSD only),\n"
         "which plays more nicely with host firewalling and bridging."
#endif
#ifdef HAVE_VMNET
         "\n"
         "'vmnet' (MacOS only) uses the MacOS vmnet framework which uses\n"
         "a host's network interface as a bridge to effectively place\n"
         "the Alpha system on the same subnet as the host."
#endif
         ,
         validation_pci_dec21143_type},
#if defined(HAVE_PCAP)
        {"adapter", "list", "adapter",
         "What host network interface should we connect to?\n"
         "Choose 'list' to get a list at run-time.",
         validation_pci_dec21143_adapter},
#endif
        {"mac", "08-00-2B-E5-40-00", "mac",
         "What should the NIC's MAC address be?\n"
         "This should be unique on your network.",
         validation_mac},
        {"queue", "1024", "queue",
         "The number of frames the receive queue can hold between the host\n"
         "capture interface and the emulated NIC. Frames that arrive while the\n"
         "queue is full are dropped (and reported on the console).",
         validation_pci_dec21143_queue},
        {"crc?", STR_NO, "crc",
         "Calculate a real ethernet CRC (FCS) for frames delivered to the\n"
         "guest. When false, a zeroed placeholder CRC is appended instead,\n"
         "which saves host CPU; guests normally never check it",
         validation_yes_no},
        {"trace packets?", STR_NO, "trace_packets",
         "Dump every transmitted and received packet to the console,\n"
         "for network debugging. Very noisy.",
         validation_yes_no}
    /*
    ,
    {"autonegotiate_delay?", STR_NO, "autonegotiate_delay",
     "Hidden option: defer SIA autonegotiation completion ~50ms.",
     validation_yes_no}
    */
#if 0 && defined(HAVE_VMNET)
        ,
        {"drop_privileges?", STR_YES, "drop_privileges",
         "For vmnet interfaces, controls whether privileges are dropped after the\n"
         "interface is initialized. (ES40 starts with privileges enabled but\n"
         "doesn't need privileges once the network interfaces are initialized.)\n"
         "If you configure multiple network interfaces with vmnet, set this\n"
         "variable to false on all but the interface in the highest PCI slot.",
         validation_yes_no}
#endif
    };
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child_myValue("dec21143");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "PCI slot");
    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (i == idx && c != nullptr)
            preset[i] = c->get_myName() + strlen("pci");
        else if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (c != nullptr)
        sys0->remove_child(c->get_myName());

    idx = fentry_index(entry, num_entries, "PCI slot");
    c = get_pcislot(values[idx]);
    if (c == nullptr)
        c = new CConfigurator(sys0, (char *)(string("pci") + values[idx]).c_str(), (char *)"dec21143");

    for (int i = 0; i < num_entries; ++i)
        if (entry[i].name != NULL)
            c->set_value(strdup(entry[i].name), strdup(values[i]));

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
    free(first_free_pcislot);
#endif // defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_VMNET)
}

// Form for Symbios 53c819 SCSI PCI card settings
void edit_pci_sym53c810_settings(const char *title)
{
    char *first_free_pcislot = find_first_free_pcislot();
    if (first_free_pcislot == NULL)
    {
        show_text(title, "No free PCI slots");
        return;
    }

    FormEntry_t entry[] = {
        {"PCI slot", first_free_pcislot, NULL,
         "Which PCI slot should the Symbios 53C810 narrow SCSI controller be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot}};
    int num_entries = ARRAY_SIZE(entry);
    FormValues_t preset;
    int idx;

    CConfigurator *c = sys0->find_child_myValue("sym53c810");

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "PCI slot");
    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (i == idx && strcmp(c->get_myName(), PCI_SLOT_TEMP))
            preset[i] = c->get_myName() + strlen("pci");
        else if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (!strcmp(c->get_myName(), PCI_SLOT_TEMP))
    {
        idx = fentry_index(entry, num_entries, "PCI slot");
        c->set_value(strdup("pci_slot"), strdup(values[idx]));
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
    free(first_free_pcislot);
}

// Menu for Symbios 53c819 SCSI PCI card disks
void edit_pci_sym53c810_disks(const char *title)
{
    // Created in edit_pci_sym53c810()
    CConfigurator *c = sys0->find_child_myValue("sym53c810");

    add_scsi_disks(title, 7, c);
}

// Menu for Symbios 53c819 SCSI PCI card
void edit_pci_sym53c810(const char *title)
{
    MenuEntry_t entry[] = {
        {"Settings", "Edit the Symbios 53C810 narrow SCSI controller settings.", edit_pci_sym53c810_settings},
        {"Add disks", "Add disks to the Symbios 53C810 narrow SCSI controller.", edit_pci_sym53c810_disks}};
    int num_entries = ARRAY_SIZE(entry);
    char *pcislot;

    CConfigurator *c = sys0->find_child_myValue("sym53c810");
    if (c == nullptr)
        c = new CConfigurator(sys0, (char *)PCI_SLOT_TEMP, (char *)"sym53c810");

    bool check_failed;
    do
    {
        show_menu(title, entry, num_entries, MENU_3RD_LEVEL);

        check_failed = FALSE;
        if (!strcmp(c->get_myName(), PCI_SLOT_TEMP))
        {
            pcislot = c->get_text_value("pci_slot", NULL);
            if (pcislot == NULL)
            {
                show_text("Error", "No PCI slot has been specified.");
                check_failed = TRUE;
            }
            else
            {
                c->set_myName((char *)(string("pci") + pcislot).c_str());
                c->remove_value((char *)"pci_slot");
            }
        }
    } while (check_failed);
}

bool check_pci_lsi53c1020_settings(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "persistant flash?")], STR_YES) &&
        !strcmp(values[fentry_index(entry, num_entries, "flash file")], ""))
    {
        show_text("Error", "No flash filename has been specified.");
        return FALSE;
    }

    if (!strcmp(values[fentry_index(entry, num_entries, "initial LSI BIOS and IOC firmware images?")], STR_YES))
    {
        if (!strcmp(values[fentry_index(entry, num_entries, "rom file")], ""))
        {
            show_text("Error", "No ROM filename has been specified.");
            return FALSE;
        }

        if (!strcmp(values[fentry_index(entry, num_entries, "firmware file")], ""))
        {
            show_text("Error", "No firmware filename has been specified.");
            return FALSE;
        }
    }
    return TRUE;
}

// Form for LSI 53c1020 SCSI PCI card settings
void edit_pci_lsi53c1020_settings(const char *title)
{
    char *first_free_pcislot = find_first_free_pcislot();
    if (first_free_pcislot == NULL)
    {
        show_text(title, "No free PCI slots");
        return;
    }

    FormEntry_t entry[] = {
        {"PCI slot", first_free_pcislot, NULL,
         "Which PCI slot should the LSI 53C1020 Fusion-MPT Ultra320 SCSI controller be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot},
        {"persistant flash?", STR_NO, NULL,
         "Do you want the LSI card flash to persist between emulator runs?\n"
         "The optional backing file is a raw 512 KiB image of the card's flash.\n"
         "If it does not exist, ES40 creates it when possible. Without a flash backing\n"
         "file, BIOS and IOC firmware changes remain volatile.",
         validation_yes_no},
        {"flash file",
#if defined(_WIN32)
         "rom\\lsi53c1020.flash",
#elif defined(__VMS)
         "[.ROM]LSI53C1020.FLASH",
#else
         "rom/lsi53c1020.flash",
#endif
         "flash",
         "Where should the LSI card flash image be stored?\n"
         "Use a different 512 KiB backing file for each emulated controller.\n"
         "An existing image always takes precedence over seed files.",
         validation_file},
        {"initial LSI BIOS and IOC firmware images?", STR_NO, NULL,
         "These files seed the card only when no persistent flash imagehas been loaded.\n"
         "They are not reapplied on later starts, so changes made by firmware or\n"
         "an operating system remain intact. The behavioral controller starts without either seed.",
         validation_yes_no},
        {"rom file",
#if defined(_WIN32)
         "rom\\mptps.rom",
#elif defined(__VMS)
         "[.ROM]MPTPS.ROM",
#else
         "rom/mptps.rom",
#endif
         "rom",
         "Where can the LSI PCI BIOS image be found?\n"
         "Use the unmodified mptps.rom file from a compatible LSI firmware package.\n"
         "It is used only when initializing card flash. AlphaBIOS can execute this\n"
         "x86 ROM and expose attached disks through SCSIBIOS; SRM does not use it.",
         validation_file},
        {"firmware file",
#if defined(_WIN32)
         "rom\\it_1030.fw",
#elif defined(__VMS)
         "[.ROM]IT_1030.FW",
#else
         "rom/it_1030.fw",
#endif
         "firmware",
         "Where can the LSI IOC firmware image be found?\n"
         "Use the supplied it_1030.fw compatibility image for the plain 53C1020;\n"
         "the T image is for 53C1020A/1030T controllers. The image is used only\n"
         "when initializing flash, and does not change the emulated card's 53C1020 identity.",
         validation_file}};
    int num_entries = ARRAY_SIZE(entry);
    FormValues_t preset;
    int idx;

    CConfigurator *c = sys0->find_child_myValue("lsi53c1020");

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "PCI slot");
    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (i == idx && strcmp(c->get_myName(), PCI_SLOT_TEMP))
            preset[i] = c->get_myName() + strlen("pci");
        else if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_pci_lsi53c1020_settings);

    if (!strcmp(c->get_myName(), PCI_SLOT_TEMP))
    {
        idx = fentry_index(entry, num_entries, "PCI slot");
        c->set_value(strdup("pci_slot"), strdup(values[idx]));
    }

    idx = fentry_index(entry, num_entries, "persistant flash?");
    if (!strcmp(values[idx], STR_YES))
    {
        idx = fentry_index(entry, num_entries, "flash file");
        c->set_value(strdup(entry[idx].name), strdup(values[idx]));
    }

    idx = fentry_index(entry, num_entries, "initial LSI BIOS and IOC firmware images?");
    if (!strcmp(values[idx], STR_YES))
    {
        idx = fentry_index(entry, num_entries, "rom file");
        c->set_value(strdup(entry[idx].name), strdup(values[idx]));

        idx = fentry_index(entry, num_entries, "firmware file");
        c->set_value(strdup(entry[idx].name), strdup(values[idx]));
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
    free(first_free_pcislot);
}

// Menu for LSI 53c1020 SCSI PCI card disks
void edit_pci_lsi53c1020_disks(const char *title)
{
    CConfigurator *c = sys0->find_child_myValue("lsi53c1020");

    add_scsi_disks(title, 16, c);
}

// Menu for LSI 53c1020 SCSI PCI card
void edit_pci_lsi53c1020(const char *title)
{
    MenuEntry_t entry[] = {
        {"Settings", "Edit the LSI 53C1020 Fusion-MPT Ultra320 SCSI controller settings.", edit_pci_lsi53c1020_settings},
        {"Add disks", "Add disks to the LSI 53C1020 Fusion-MPT Ultra320 SCSI controller.", edit_pci_lsi53c1020_disks}};
    int num_entries = ARRAY_SIZE(entry);
    char *pcislot;

    CConfigurator *c = sys0->find_child_myValue("lsi53c1020");
    if (c == nullptr)
        c = new CConfigurator(sys0, (char *)PCI_SLOT_TEMP, (char *)"lsi53c1020");

    bool check_failed;
    do
    {
        show_menu(title, entry, num_entries, MENU_3RD_LEVEL);

        check_failed = FALSE;
        if (!strcmp(c->get_myName(), PCI_SLOT_TEMP))
        {
            pcislot = c->get_text_value("pci_slot", NULL);
            if (pcislot == NULL)
            {
                show_text("Error", "No PCI slot has been specified.");
                check_failed = TRUE;
            }
            else
            {
                c->set_myName((char *)(string("pci") + pcislot).c_str());
                c->remove_value((char *)"pci_slot");
            }
        }
    } while (check_failed);
}

// Form for Ensoniq ES1370 PCI card
void edit_pci_es1370(const char *title)
{
    char *first_free_pcislot = find_first_free_pcislot();
    if (first_free_pcislot == NULL)
    {
        show_text(title, "No free PCI slots");
        return;
    }

    FormEntry_t entry[] = {
        {"PCI slot", first_free_pcislot, NULL,
         "Which PCI slot should the Ensoniq AudioPCI ES1370 sound card be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child_myValue("es1370");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "PCI slot");
    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (i == idx && c != nullptr)
            preset[i] = c->get_myName() + strlen("pci");
        else if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (c != nullptr)
        sys0->remove_child(c->get_myName());

    idx = fentry_index(entry, num_entries, "PCI slot");
    c = get_pcislot(values[idx]);
    if (c != nullptr)
        sys0->remove_child(c->get_myName());
    c = new CConfigurator(sys0, (char *)(string("pci") + values[idx]).c_str(), (char *)"es1370");

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
    free(first_free_pcislot);
}

// Menu for PCI bus
void edit_pci(const char *title)
{
    MenuEntry_t entry[] = {
        {"List PCI bus", "Show a list of devices on the PCI bus.", show_pcislots},
        {"Add VGA", "Add a VGA card", edit_pci_vga},
        {"Add NIC", "Add a DEC 21143 Network Interface Card", edit_pci_dec21143},
        {"Add Symbios SCSI", "Add a Symbios 53C810 narrow SCSI controller (most tested/proven for OS boot/install)", edit_pci_sym53c810},
        {"Add LSI SCSI", "Add a LSI 53C1020 Fusion-MPT Ultra320 SCSI controller (NOT SRM BOOT CAPABLE!)", edit_pci_lsi53c1020}
#ifdef HAVE_SDL
        ,
        {"Add ES1370 Audio", "Add an Ensoniq AudioPCI ES1370 sound card (works only with Windows NT 4.0 guest)", edit_pci_es1370}};
#endif
    int num_entries = ARRAY_SIZE(entry);

    show_menu(title, entry, num_entries, MENU_2ND_LEVEL);
}

/**
 * Serial lines configuration
 **/

void validation_serial_port(FIELD *field)
{
    set_field_type(field, TYPE_INTEGER, 1, 1, USHRT_MAX);
}

bool check_serial(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    for (int i = 0; i < 2; ++i)
    {
        string serial_program = "serial" + i2s(i) + "." + "program";
        int idx = fentry_index(entry, num_entries, serial_program.c_str());
        if (strchr(values[idx], '"'))
        {
            show_text("Error", "No double quotes are allowed in the program paths.");
            return FALSE;
        }
    }
    return TRUE;
}

// Form for serial lines
void edit_serial(const char *title)
{
    const int num_attr = 6;
    FormEntry_t entry[num_attr * 2];

    for (int i = 0; i < 2; ++i)
    {
        string serial_name = "serial" + i2s(i);
        string serial_prefix = serial_name + ".";
        entry[num_attr * i] = {
            strdup((serial_prefix + "disabled").c_str()), STR_NO, "disabled",
            "Make the guest see no UART at this address at all, so drivers skip\n"
            "the port completely. No Telnet port is opened. Unlike null_attach,\n"
            "the UART appears absent rather than present-but-idle.\n"
            "If enabled, all other parameters refering to this serial port will be ignored.",
            validation_yes_no};

        entry[num_attr * i + 1] = {
            strdup((serial_prefix + "null_attach").c_str()), STR_NO, "null_attach",
            "If 'yes' is selected, the UART exists on the bus and presents itself\n"
            "to the guest as a healthy idle 16550 (THRE/TSRE, CTS/DSR), but no telnet listener\n"
            "is opened and any bytes the guest transmits are silently discarded.\n"
            "Useful since two are required by the platform firmwares in case you don't need them.\n"
            "If enabled, all other parameters refering to this serial port will be ignored.",
            validation_yes_no};

        const string port_value = i2s(21264 + i);
        entry[num_attr * i + 2] = {
            strdup((serial_prefix + "port").c_str()), strdup(port_value.c_str()), "port",
            "The telnet port the serial device will listen on (in the range 1..65535).",
            validation_serial_port};

        entry[num_attr * i + 3] = {
            strdup((serial_prefix + "raw_mode").c_str()), STR_NO, "raw_mode",
            "Pass the byte stream through unmodified: no Telnet protocol (IAC)\n"
            "processing and no throttling to emulated line speed. Required when\n"
            "the port carries a binary protocol such as windbg (KD) or kgdb\n"
            "instead of a terminal session.",
            validation_yes_no};

        entry[num_attr * i + 4] = {
            strdup((serial_prefix + "program").c_str()),
#if defined(_WIN32)
            "C:\\Program Files\\Putty\\Putty.exe",
#else
            "putty",
#endif
            NULL,
            "The program that should be started automatically to connect to the serial device listener.\n"
            "Enter the path to a program to start this to create an automatic connection with the serial port.\n"
            "No double quotes are allowed in the path.\n"
            "Set to an empty value to establish the connection manually.\n"
            "In that case, the arguments parameter is ignored.",
            validation_file};

        const string arguments_value = "telnet://localhost:" + port_value;
        entry[num_attr * i + 5] = {
            strdup((serial_prefix + "arguments").c_str()), strdup(arguments_value.c_str()), NULL,
            "Arguments the program should use to connect to the serial port.",
            NULL};
    }
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    const string dquotes = "\"\"";
    for (int i = 0; i < 2; ++i)
    {
        string serial_name = "serial" + i2s(i);
        c = sys0->find_child(serial_name.c_str());
        for (int k = i * num_attr; k < (i + 1) * num_attr; ++k)
        {
            if (entry[k].name != NULL)
            {
                char *p;
                if (c != nullptr && (p = c->get_text_value(entry[k].name)) != NULL)
                    preset[k] = p;
                else
                    preset[k] = (char *)entry[k].preset;
                continue;
            }

            if (c == nullptr)
            {
                preset[k] = (char *)entry[k].preset;
                continue;
            }

            char *action = c->get_text_value("action", NULL);
            if (action == NULL)
            {
                preset[k] = (char *)"";
                continue;
            }

            // Split action into program & arguments
            static char program[2][256], arguments[2][256];
            string action_str = action;
            string::size_type p1, p2, p3; // p1=start of program, p2=end of program, p3=after end of program
            if ((p1 = action_str.find(dquotes)) != string::npos)
            {
                p1 += 2;
                p2 = action_str.find(dquotes, p1);
                if (p2 == string::npos)
                    FAILURE_1(Configuration, "No closing double quotes in %s", entry[k].label);
                p3 = p2 + 2;
            }
            else
            {
                p1 = 0;
                p2 = action_str.find(" ");
                if (p2 == string::npos)
                    p2 = action_str.length();
                p3 = p2;
            }
            if (k % num_attr == 4)
            {
                memset(program[i], '\0', sizeof(program[i]));
                strncpy(program[i], action_str.substr(p1, p2 - p1).c_str(), sizeof(program[i]) - 1);
                preset[k] = program[i];
            }
            else if (k % num_attr == 5)
            {
                memset(arguments[i], '\0', sizeof(arguments[i]));
                if (p3 < action_str.length())
                    strncpy(arguments[i], action_str.substr(p3 + 1).c_str(), sizeof(arguments[i]) - 1);
                preset[k] = arguments[i];
            }
            else
                FAILURE_1(Logic, "No preset for %s", entry[k].label);
        }
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_serial);

    for (int i = 0; i < 2; ++i)
    {
        string serial_name = "serial" + i2s(i);
        string serial_prefix = serial_name + ".";
        c = sys0->find_child(serial_name.c_str());
        if (c != nullptr)
            sys0->remove_child(c->get_myName());
        c = new CConfigurator(sys0, (char *)serial_name.c_str(), (char *)"serial");

        idx = fentry_index(entry, num_entries, (serial_prefix + "disabled").c_str());
        if (!strcmp(values[idx], STR_YES))
        {
            c->set_value(strdup(entry[idx].name), strdup(values[idx]));
            continue;
        }

        idx = fentry_index(entry, num_entries, (serial_prefix + "null_attach").c_str());
        if (!strcmp(values[idx], STR_YES))
        {
            c->set_value(strdup(entry[idx].name), strdup(values[idx]));
            continue;
        }

        idx = fentry_index(entry, num_entries, (serial_prefix + "port").c_str());
        c->set_value(strdup(entry[idx].name), strdup(values[idx]));

        idx = fentry_index(entry, num_entries, (serial_prefix + "raw_mode").c_str());
        c->set_value(strdup(entry[idx].name), strdup(values[idx]));

        idx = fentry_index(entry, num_entries, (serial_prefix + "program").c_str());
        if (strcmp(values[idx], ""))
        {
            int idx2 = fentry_index(entry, num_entries, (serial_prefix + "arguments").c_str());
            // TODO: Check program path to determine whether dquotes are necessary
            string action = dquotes + values[idx] + dquotes + " " + values[idx2];
            c->set_value(strdup("action"), strdup(action.c_str()));
        }
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
    {
        free(values[i]);

        free((void *)entry[i].label);
        if (i % num_attr == 2 || i % num_attr == 5)
            free((void *)entry[i].preset);
    }
    free(values);
    free(preset);
}

/**
 * Floppy configuration
 **/

// Menu for floppy disks
void edit_floppy(const char *title)
{
    MenuEntry_t entry[] = {
        {"empty", "Don't add drives to the floppy controller", NULL},
        {"disk0.0", "Drive A: / DVA0:", NULL},
        {"disk0.1", "Drive B: / DVA1:", NULL}};
    int num_entries = ARRAY_SIZE(entry);

    CConfigurator *c = sys0->find_child("fdc0");
    if (c == nullptr)
        c = new CConfigurator(sys0, (char *)"fdc0", (char *)"floppy");

    while (TRUE)
    {
        int sel = show_menu(title, entry, num_entries, MENU_2ND_LEVEL);
        if (sel == -1)
            break;
        if (sel == 0)
        {
            c->remove_all_children();
            break;
        }

        string subtitle = string(title) + ": " + entry[sel].text;
        add_disks(subtitle.c_str(), entry[sel].text, c);
    }
}

/**
 * IDE bus configuration
 **/

// Form for IDE bus settings
void edit_ide_settings(const char *title)
{
    FormEntry_t entry[] = {
        {"dma?", STR_YES, "dma",
         "Allow the guest to use (busmaster) DMA transfers on this\n"
         "IDE controller. Set to false to force PIO-only operation.",
         validation_yes_no}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child(PCI_SLOT_IDE);
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (c != nullptr)
        sys0->remove_child(c->get_myName());
    c = new CConfigurator(sys0, (char *)PCI_SLOT_IDE, (char *)"ali_ide");

    idx = fentry_index(entry, num_entries, "dma?");
    c->set_value(strdup(entry[idx].name), strdup(values[idx]));

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
}

// Menu for IDE bus disks
void edit_ide_disks(const char *title)
{
    MenuEntry_t entry[] = {
        {"none", "Stop adding disks", NULL},
        {"disk0.0", "primary master", NULL},
        {"disk0.1", "primary slave", NULL},
        {"disk1.0", "secondary master", NULL},
        {"disk1.1", "secondary slave", NULL}};
    int num_entries = ARRAY_SIZE(entry);

    CConfigurator *c = sys0->find_child(PCI_SLOT_IDE);
    if (c == nullptr)
        c = new CConfigurator(sys0, (char *)PCI_SLOT_IDE, (char *)"ali_ide");

    while (TRUE)
    {
        int sel = show_menu(title, entry, num_entries, MENU_3RD_LEVEL);
        if (sel <= 0)
            break;

        string subtitle = string(title) + ": " + entry[sel].text;
        add_disks(subtitle.c_str(), entry[sel].text, c);
    }
}

// Menu for IDE bus
void edit_ide(const char *title)
{
    MenuEntry_t entry[] = {
        {"empty", "Don't add disks to the IDE controller (recommended).", NULL},
        {"Settings", "Edit the IDE controller settings.", edit_ide_settings},
        {"Add disks", "Add disks to the IDE controller.", edit_ide_disks}};
    int num_entries = ARRAY_SIZE(entry);

    int sel = show_menu(title, entry, num_entries, MENU_2ND_LEVEL);

    if (sel == 0)
    {
        CConfigurator *c = sys0->find_child(PCI_SLOT_IDE);
        if (c != nullptr)
            c->remove_all_children();
    }
}

/**
 * MPU-401 configuration (Windows only)
 **/

#ifdef _WIN32
void validation_mpu401_midiout(FIELD *field)
{
    set_field_type(field, TYPE_INTEGER, 1, 0, SHRT_MAX); // What is the maximum MIDI channel number?
}

// Form for MPU-401
void edit_mpu401(const char *title)
{
    FormEntry_t entry[] = {
        {"midi_out", "0", "midi_out",
         "The host MIDI output device number to play on.\n"
         "The default is 0 (the Windows default MIDI device).",
         validation_mpu401_midiout}};
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child("mpu0");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (c != nullptr)
        sys0->remove_child(c->get_myName());
    c = new CConfigurator(sys0, (char *)"mpu0", (char *)"mpu401");

    idx = fentry_index(entry, num_entries, "midi_out");
    c->set_value(strdup(entry[idx].name), strdup(values[idx]));

    // Clean up
    for (int i = 0; i < num_entries; ++i)
        free(values[i]);
    free(values);
    free(preset);
}
#endif

/**
 * Main menu
 *
 * Returns TRUE is results are to be saved.
 **/
bool main_menu(void)
{
    MenuEntry_t entry[] = {
        {"GUI settings",
         "You need a GUI if you want to use an emulated graphics card.\n"
         "You don't need this for most OS'es. If you don't need this,\n"
         "we recommend that you select 'none'.",
         edit_gui},
        {"System settings", "", edit_tsunami},
        {"CPU settings", "", edit_ev68cb},
        {"ALI 1543 settings", "", edit_ali},
        {"PCI devices", "", edit_pci},
        {"Serial devices", "", edit_serial},
        {"Floppy devices", "", edit_floppy},
        {"IDE devices", "", edit_ide}
#ifdef _WIN32
        ,
        {"MPU-401 device", "", edit_mpu401}
#endif
        ,
        {"Quit without saving", "Quit the program without writing to output file", NULL}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    bool save_results;

    bool check_failed;
    do
    {
        int sel = show_menu("Main menu", entry, num_entries, MENU_1ST_LEVEL);

        check_failed = FALSE;
        save_results = (sel != num_entries - 1);
        if (save_results)
        {
            // Find any VGA card
            CConfigurator *vgacard;
            vgacard = sys0->find_child_myValue("s3");
#if defined(HAVE_CIRRUS)
            if (vgacard == nullptr)
                vgacard = sys0->find_child_myValue("cirrus");
#endif
#if defined(HAVE_RADEON)
            if (vgacard == nullptr)
                vgacard = sys0->find_child_myValue("radeon");
#endif

            c = sys0->find_child(PCI_SLOT_ALI);
            if (c != nullptr)
            {
                char *vga_console = c->get_text_value("vga_console", STR_NO);
                if (!strcmp(vga_console, STR_YES) && vgacard == nullptr)
                {
                    show_text("Error",
                              "You set console=graphics (in the ALI settings)\n"
                              "without configuring a VGA card (on the PCI bus).");
                    check_failed = TRUE;
                }
            }

            if (vgacard != nullptr)
            {
                c = theConfig->find_child("gui");
                if (c == nullptr)
                {
                    show_text("Error", "You configured a VGA card without configuring a GUI.");
                    check_failed = TRUE;
                }
            }
        }
    } while (check_failed);

    return save_results;
}

int main(int argc, char **argv)
{
    const char *out_filename = NULL;

    if (argc >= 2 && argv[1][0] != '\0')
    {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "-?"))
        {
            printf("Usage: %s [input-filename] [output-filename]\n", argv[0]);
            return 0;
        }
        read_configuration(argv[1]); // initializes theConfig
    }
    else
    {
        static char minimal_config[] = "sys0 = tsunami { cpu0 = ev68cb {} }";
        theConfig = new CConfigurator(nullptr, NULL, NULL, minimal_config, strlen(minimal_config));
    }
    sys0 = theConfig->find_child("sys0");
    if (sys0 == nullptr)
        FAILURE(Configuration, "No 'sys0' in the configuration.");

    if (argc >= 3)
    {
        if (argv[2][0] == '\0')
            FAILURE(Configuration, "Output filename is empty.");
        out_filename = argv[2];
    }
    else
        out_filename = "es40.cfg";

    // see ncurses(3X) manpage
    setlocale(LC_ALL, "");

    // see https://invisible-island.net/ncurses/ncurses.faq.html#no_padding
    setenv("NCURSES_NO_PADDING", "1", FALSE);

    atexit(reinterpret_cast<void (*)()>(endwin));
    initscr();
    cbreak();
    noecho();
    noqiflush();
    keypad(stdscr, TRUE);
    // start_color();  // Neither used (yet) nor needed.

    getmaxyx(stdscr, scrh, scrw);
#if 0
    signal(SIGWINCH, resizeHandler);
#endif

    try
    {
        es40_banner("AlphaServer ES40 emulator configuration utility");

        bool save_results = main_menu();

        if (save_results)
            write_configuration(out_filename);
    }
	catch (CException& e)
	{
		printf("Failure: %s\n", e.displayText().c_str());
    }

    return 0;
}