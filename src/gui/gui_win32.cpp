/* ES40 emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2025-2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 *  This file is based upon Bochs.
 *
 *  Copyright (C) 2002  MandrakeSoft S.A.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  Much of this file was written by:
 *  David Ross
 *  dross@pobox.com
 */

 /**
  * \file
  * Win32 GUI implementation. Allows use of gfx without SDL on WIndows
  *
  * $Id$
  *
  * X-1.5        Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.4        Camiel Vanderhoeven                             12-FEB-2008
  *      Moved keyboard code into it's own class (CKeyboard)
  *
  * X-1.3        Camiel Vanderhoeven                             22-JAN-2008
  *      Commented out mousewheel-code, doesn't work with older win32
  *      versions.
  *
  * X-1.2        Camiel Vanderhoeven                             19-JAN-2008
  *      Changed window title.
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2008
  *      Initial version for ES40 emulator.
  *
  **/
#include "../StdAfx.h"

#if defined(_WIN32)
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include "config/Configurator.h"
#include "io/Keyboard.h"
#include "system/System.h"
#include "keymap.h"
#include "gui.h"
#include "win32_media.h"
#include "win32_manager.h"

class bx_win32_gui_c;
class win32_application;
static LRESULT CALLBACK win32_window_proc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK win32_dispatch_proc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK win32_keyboard_proc(int, WPARAM, LPARAM);
static win32_application* win32_keyboard_application = nullptr;
static const char win32_window_class[] = "ES40 Display";
static const char win32_dispatch_class[] = "ES40 Display Dispatcher";
static constexpr DWORD win32_window_style = WS_OVERLAPPEDWINDOW;
static constexpr UINT win32_invoke_message = WM_APP + 1;
static constexpr UINT win32_wake_message = WM_APP + 2;
static constexpr UINT win32_reconcile_message = WM_APP + 3;
static constexpr UINT win32_keyboard_message = WM_APP + 4;
static constexpr unsigned win32_print_screen_scan = 0x137;
static constexpr unsigned win32_pause_scan = 0x146;

// Embedded displays use the manager's foreground window.
static bool win32_display_active(HWND window)
{
	return window && IsWindowVisible(window) && GetFocus() == window &&
		GetForegroundWindow() == GetAncestor(window, GA_ROOT);
}

static unsigned win32_key_scan(unsigned key, unsigned scan, bool extended)
{
	// Some virtual keyboards send break codes or omit the scan entirely.
	extended = extended || (scan & 0xff00) == 0xe000;
	if (!(scan & 0x7f)) scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
	const unsigned prefix = scan & 0xff00;
	const unsigned make = scan & 0x7f;
	extended = extended || prefix == 0xe000;
	// Windows appears to make right shift extended even if the native is
	// plain 0x36 during capture, both must identify the same to the guest
	if (key == VK_RSHIFT || (key == VK_SHIFT && make == 0x36)) return 0x36;
	// Keep Pause/Break's E1 sequence distinct from Control and NumLock.
	if (key == VK_SNAPSHOT) return win32_print_screen_scan;
	if (key == VK_NUMLOCK) return 0x145;
	if (key == VK_PAUSE || key == VK_CANCEL ||
		(prefix == 0xe100 && make == 0x1d) || (!extended && make == 0x45))
		return win32_pause_scan;
	return make | (extended ? 0x100 : 0);
}

static LPARAM win32_key_flags(WPARAM key, LPARAM flags)
{
	const unsigned scan = win32_key_scan((unsigned)key, HIWORD(flags) & 0xff,
		(HIWORD(flags) & KF_EXTENDED) != 0);
	return (flags & ~(LPARAM(0x1ff) << 16)) | (LPARAM(scan) << 16);
}

static bool win32_grabbed_key(unsigned key)
{
	return key == VK_LWIN || key == VK_RWIN || key == VK_LCONTROL ||
		key == VK_RCONTROL || key == VK_LMENU || key == VK_RMENU ||
		key == VK_TAB || key == VK_ESCAPE || key == VK_SNAPSHOT;
}

struct win32_captured_key
{
	HWND window;
	unsigned key, scan;
	bool released, repeat;
};

struct win32_native_key
{
	DWORD time;
	unsigned key;
	bool released, pending;
};

struct win32_raw_mouse_position
{
	long long x = 0, y = 0;
	int screen_width = 0, screen_height = 0, screen_left = 0, screen_top = 0;
	USHORT mode = 0;
	bool valid = false;
};

struct win32_call
{
	std::function<void()> fn;
	std::exception_ptr error;
};

struct win32_linear_sample
{
	unsigned first, second, fraction;
};

static win32_linear_sample win32_linear_axis(unsigned position,
	unsigned source_size, unsigned target_size)
{
	// Sample pixel centers and clamp at the image edge, as SDL textures do.
	const double source = (std::max)(0.0, (std::min)((double)source_size - 1,
		(position + 0.5) * source_size / target_size - 0.5));
	const unsigned first = (unsigned)source;
	return { first, (std::min)(first + 1, source_size - 1),
		(unsigned)((source - first) * 256 + 0.5) };
}

static u32 win32_linear_mix(u32 first, u32 second, unsigned fraction)
{
	// Interpolate red/blue together and green separately, with 8-bit weights.
	const unsigned inverse = 256 - fraction;
	const u32 rb = (((first & 0x00ff00ff) * inverse +
		(second & 0x00ff00ff) * fraction + 0x00800080) >> 8) & 0x00ff00ff;
	const u32 g = (((first & 0x0000ff00) * inverse +
		(second & 0x0000ff00) * fraction + 0x00008000) >> 8) & 0x0000ff00;
	return rb | g;
}

enum win32_hotkey_modifier : unsigned
{
	WIN32_HOTKEY_CTRL = 1, WIN32_HOTKEY_ALT = 2,
	WIN32_HOTKEY_SHIFT = 4, WIN32_HOTKEY_GUI = 8
};

// Keep character, named and keypad keys distinct, without an SDL dependency.
static constexpr unsigned win32_named_key = 0x10000;
static constexpr unsigned win32_keypad_key = 0x20000;
static const struct { unsigned key; const char* name; } win32_hotkey_names[] = {
	{ win32_named_key | VK_RETURN, "Return" },
	{ win32_named_key | VK_ESCAPE, "Escape" },
	{ win32_named_key | VK_BACK, "Backspace" },
	{ win32_named_key | VK_TAB, "Tab" }, { ' ', "Space" },
	{ win32_named_key | VK_CAPITAL, "CapsLock" },
	{ win32_named_key | VK_SNAPSHOT, "PrintScreen" },
	{ win32_named_key | VK_SCROLL, "ScrollLock" },
	{ win32_named_key | VK_PAUSE, "Pause" },
	{ win32_named_key | VK_INSERT, "Insert" },
	{ win32_named_key | VK_HOME, "Home" },
	{ win32_named_key | VK_PRIOR, "PageUp" },
	{ win32_named_key | VK_DELETE, "Delete" },
	{ win32_named_key | VK_END, "End" },
	{ win32_named_key | VK_NEXT, "PageDown" },
	{ win32_named_key | VK_RIGHT, "Right" },
	{ win32_named_key | VK_LEFT, "Left" },
	{ win32_named_key | VK_DOWN, "Down" },
	{ win32_named_key | VK_UP, "Up" },
	{ win32_named_key | VK_NUMLOCK, "Numlock" },
	{ win32_keypad_key | VK_DIVIDE, "Keypad /" },
	{ win32_keypad_key | VK_MULTIPLY, "Keypad *" },
	{ win32_keypad_key | VK_SUBTRACT, "Keypad -" },
	{ win32_keypad_key | VK_ADD, "Keypad +" },
	{ win32_keypad_key | VK_RETURN, "Keypad Enter" },
	{ win32_keypad_key | VK_DECIMAL, "Keypad ." },
	{ win32_keypad_key | VK_NUMPAD0, "Keypad 0" },
	{ win32_keypad_key | VK_NUMPAD1, "Keypad 1" },
	{ win32_keypad_key | VK_NUMPAD2, "Keypad 2" },
	{ win32_keypad_key | VK_NUMPAD3, "Keypad 3" },
	{ win32_keypad_key | VK_NUMPAD4, "Keypad 4" },
	{ win32_keypad_key | VK_NUMPAD5, "Keypad 5" },
	{ win32_keypad_key | VK_NUMPAD6, "Keypad 6" },
	{ win32_keypad_key | VK_NUMPAD7, "Keypad 7" },
	{ win32_keypad_key | VK_NUMPAD8, "Keypad 8" },
	{ win32_keypad_key | VK_NUMPAD9, "Keypad 9" }
};

struct win32_hotkey_binding
{
	const char* config_name = nullptr;
	unsigned key = 0, modifiers = 0;
	bool enabled = false;
	std::string display;
	bool matches(unsigned host_key, unsigned host_modifiers) const
		{ return enabled && key == host_key && modifiers == host_modifiers; }
};

static std::string trim_win32_hotkey(const std::string& text)
{
	size_t first = 0, last = text.size();
	while (first < last && std::isspace((unsigned char)text[first])) ++first;
	while (last > first && std::isspace((unsigned char)text[last - 1])) --last;
	return text.substr(first, last - first);
}

static bool win32_hotkey_text_equals(const std::string& text, const char* name)
{
	if (text.size() != strlen(name)) return false;
	for (size_t i = 0; i < text.size(); ++i)
		if (std::tolower((unsigned char)text[i]) !=
			std::tolower((unsigned char)name[i])) return false;
	return true;
}

static unsigned parse_win32_hotkey_modifier(const std::string& text)
{
	if (win32_hotkey_text_equals(text, "Ctrl") ||
		win32_hotkey_text_equals(text, "Control")) return WIN32_HOTKEY_CTRL;
	if (win32_hotkey_text_equals(text, "Alt") ||
		win32_hotkey_text_equals(text, "Option")) return WIN32_HOTKEY_ALT;
	if (win32_hotkey_text_equals(text, "Shift")) return WIN32_HOTKEY_SHIFT;
	for (const char* name : { "GUI", "Super", "Win", "Windows", "Cmd", "Command", "Meta" })
		if (win32_hotkey_text_equals(text, name)) return WIN32_HOTKEY_GUI;
	return 0;
}

static win32_hotkey_binding parse_win32_hotkey(CConfigurator* cfg,
	const char* config_name, const char* default_value)
{
	win32_hotkey_binding binding;
	binding.config_name = config_name;
	const char* configured = cfg->get_text_value(config_name, default_value);
	const std::string value = trim_win32_hotkey(configured ? configured : "");
	if (win32_hotkey_text_equals(value, "none")) return binding;
	std::string key_name = value;
	bool invalid = value.empty();
	for (;;)
	{
		const size_t separator = key_name.find('+');
		if (separator == std::string::npos) break;
		const unsigned modifier = parse_win32_hotkey_modifier(
			trim_win32_hotkey(key_name.substr(0, separator)));
		if (!modifier) break;
		if (binding.modifiers & modifier) { invalid = true; break; }
		binding.modifiers |= modifier;
		key_name = trim_win32_hotkey(key_name.substr(separator + 1));
	}
	const struct { const char* alias; const char* name; } aliases[] = {
		{ "PgUp", "PageUp" }, { "PgDn", "PageDown" }, { "PageDn", "PageDown" },
		{ "Esc", "Escape" }, { "Del", "Delete" }, { "Ins", "Insert" },
		{ "KeypadPlus", "Keypad +" }, { "KpPlus", "Keypad +" },
		{ "KeypadMinus", "Keypad -" }, { "KpMinus", "Keypad -" }
	};
	for (const auto& alias : aliases)
		if (win32_hotkey_text_equals(key_name, alias.alias)) key_name = alias.name;
	for (const auto& named : win32_hotkey_names)
		if (win32_hotkey_text_equals(key_name, named.name))
			{ binding.key = named.key; key_name = named.name; break; }
	for (unsigned i = 0; i < 24; ++i)
	{
		const std::string name = "F" + std::to_string(i + 1);
		if (win32_hotkey_text_equals(key_name, name.c_str()))
			{ binding.key = win32_named_key | (VK_F1 + i); key_name = name; break; }
	}
	if (!binding.key && key_name.size() == 1 &&
		(unsigned char)key_name[0] >= 33 && (unsigned char)key_name[0] <= 126)
	{
		// SDL resolves shifted key names (e.g. ! on a US keyboard) to the
		// underlying unshifted key, without adding an implicit Shift modifier.
		const SHORT mapped = VkKeyScanW((wchar_t)(unsigned char)key_name[0]);
		if (mapped != -1 && (HIBYTE(mapped) & 1))
		{
			const unsigned unshifted = MapVirtualKeyW(LOBYTE(mapped), MAPVK_VK_TO_CHAR) & 0xffff;
			if (unshifted >= 33 && unshifted <= 126) key_name[0] = (char)unshifted;
		}
		key_name[0] = (char)std::toupper((unsigned char)key_name[0]);
		binding.key = (unsigned char)key_name[0];
	}
	if (invalid || !binding.key)
	{
		printf("%%WIN32-W-HOTKEY: %s has invalid binding \"%s\"; "
			"action disabled.\n", config_name, value.c_str());
		return binding;
	}
	binding.enabled = true;
	if (binding.modifiers & WIN32_HOTKEY_CTRL) binding.display += "Ctrl+";
	if (binding.modifiers & WIN32_HOTKEY_ALT) binding.display += "Alt+";
	if (binding.modifiers & WIN32_HOTKEY_SHIFT) binding.display += "Shift+";
	if (binding.modifiers & WIN32_HOTKEY_GUI) binding.display += "GUI+";
	binding.display += key_name;
	return binding;
}

static unsigned win32_host_hotkey_key(WPARAM key, unsigned scan)
{
	// Ctrl+Break reports VK_CANCEL, but SDL exposes it as the Pause key.
	if (scan == win32_pause_scan) return win32_named_key | VK_PAUSE;
	// NumLock changes virtual-key codes; SDL keeps keypad keys distinct.
	if (scan >= 0x47 && scan <= 0x53)
	{
		static const unsigned keypad[] = { VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9,
			VK_SUBTRACT, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6, VK_ADD,
			VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD0, VK_DECIMAL };
		return win32_keypad_key | keypad[scan - 0x47];
	}
	if (key == VK_MULTIPLY || key == VK_DIVIDE ||
		(key == VK_RETURN && (scan & 0x100))) return win32_keypad_key | (unsigned)key;
	if (key >= VK_F1 && key <= VK_F24) return win32_named_key | (unsigned)key;
	for (const auto& named : win32_hotkey_names)
		if (named.key == (win32_named_key | (unsigned)key)) return named.key;
	// This does not consume text or change the host's dead-key composition state.
	return MapVirtualKeyW((UINT)key, MAPVK_VK_TO_CHAR) & 0xffff;
}

// Use SDL-style host names in the existing three-column keymap format,
// translated to native identities without requiring SDL in this backend.
static const struct { const char* name; unsigned key; } win32_keymap_names[] = {
	{ "CLEAR", win32_named_key | VK_CLEAR },
	{ "NUMLOCKCLEAR", win32_named_key | VK_NUMLOCK },
	{ "LSHIFT", win32_named_key | VK_LSHIFT }, { "RSHIFT", win32_named_key | VK_RSHIFT },
	{ "LCTRL", win32_named_key | VK_LCONTROL }, { "RCTRL", win32_named_key | VK_RCONTROL },
	{ "LALT", win32_named_key | VK_LMENU }, { "RALT", win32_named_key | VK_RMENU },
	{ "LGUI", win32_named_key | VK_LWIN }, { "RGUI", win32_named_key | VK_RWIN },
	{ "MENU", win32_named_key | VK_APPS }, { "HELP", win32_named_key | VK_HELP },
	{ "KP_ENTER", win32_keypad_key | VK_RETURN },
	{ "KP_PERIOD", win32_keypad_key | VK_DECIMAL },
	{ "KP_DIVIDE", win32_keypad_key | VK_DIVIDE },
	{ "KP_MULTIPLY", win32_keypad_key | VK_MULTIPLY },
	{ "KP_MINUS", win32_keypad_key | VK_SUBTRACT },
	{ "KP_PLUS", win32_keypad_key | VK_ADD },
	{ "KP_EQUALS", win32_keypad_key | VK_OEM_NEC_EQUAL },
	{ "EXCLAIM", '!' }, { "DBLAPOSTROPHE", '"' }, { "HASH", '#' },
	{ "DOLLAR", '$' }, { "AMPERSAND", '&' }, { "APOSTROPHE", '\'' },
	{ "LEFTPAREN", '(' }, { "RIGHTPAREN", ')' }, { "ASTERISK", '*' },
	{ "PLUS", '+' }, { "COMMA", ',' }, { "MINUS", '-' }, { "PERIOD", '.' },
	{ "SLASH", '/' }, { "COLON", ':' }, { "SEMICOLON", ';' },
	{ "LESS", '<' }, { "EQUALS", '=' }, { "GREATER", '>' },
	{ "QUESTION", '?' }, { "AT", '@' }, { "LEFTBRACKET", '[' },
	{ "BACKSLASH", '\\' }, { "RIGHTBRACKET", ']' }, { "CARET", '^' },
	{ "UNDERSCORE", '_' }, { "GRAVE", '`' }
};

static u32 win32_keymap_symbol(const char* name)
{
	if (strncmp(name, "SDLK_", 5)) return BX_KEYMAP_UNKNOWN;
	const std::string key = name + 5;
	if (key.size() == 1 && std::isalnum((unsigned char)key[0]) &&
		(unsigned char)key[0] < 128)
		return (unsigned)std::toupper((unsigned char)key[0]);
	if (key.size() == 4 && key.compare(0, 3, "KP_") == 0 &&
		key[3] >= '0' && key[3] <= '9')
		return win32_keypad_key | (VK_NUMPAD0 + key[3] - '0');
	for (unsigned i = 0; i < 24; ++i)
		if (key == "F" + std::to_string(i + 1)) return win32_named_key | (VK_F1 + i);
	for (const auto& named : win32_hotkey_names)
		if (win32_hotkey_text_equals(key, named.name)) return named.key;
	for (const auto& named : win32_keymap_names)
		if (key == named.name) return named.key;
	return BX_KEYMAP_UNKNOWN;
}

static unsigned win32_host_mapping_key(WPARAM key, unsigned scan)
{
	// WM_KEY* uses generic modifier VKs; the scan identifies each side.
	switch (scan)
	{
	case win32_pause_scan: return win32_named_key | VK_PAUSE;
	case 0x2a: return win32_named_key | VK_LSHIFT;
	case 0x36: return win32_named_key | VK_RSHIFT;
	case 0x1d: return win32_named_key | VK_LCONTROL;
	case 0x11d: return win32_named_key | VK_RCONTROL;
	case 0x38: return win32_named_key | VK_LMENU;
	case 0x138: return win32_named_key | VK_RMENU;
	}
	if (scan >= 0x47 && scan <= 0x53) return win32_host_hotkey_key(key, scan);
	// SDL's default key events normalize symbol-based number rows to digits.
	if (key >= '0' && key <= '9') return (unsigned)key;
	if (key == VK_OEM_NEC_EQUAL) return win32_keypad_key | VK_OEM_NEC_EQUAL;
	for (const auto& named : win32_keymap_names)
		if (named.key == (win32_named_key | (unsigned)key)) return named.key;
	return win32_host_hotkey_key(key, scan);
}

static unsigned win32_host_mouse_buttons()
{
	return ((GetKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0) |
		((GetKeyState(VK_RBUTTON) & 0x8000) ? 2 : 0) |
		((GetKeyState(VK_MBUTTON) & 0x8000) ? 4 : 0);
}

static unsigned win32_key_state_key(unsigned key, unsigned scan)
{
	// WM_KEY* reports generic modifier VKs, but each side can be held alone.
	if (key == VK_SHIFT) return scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
	if (key == VK_CONTROL) return scan & 0x100 ? VK_RCONTROL : VK_LCONTROL;
	if (key == VK_MENU) return scan & 0x100 ? VK_RMENU : VK_LMENU;
	return key < 256 ? key : 0;
}

static unsigned win32_mouse_button_flags(unsigned buttons)
{
	return ((buttons & 1) ? MK_LBUTTON : 0) |
		((buttons & 2) ? MK_RBUTTON : 0) | ((buttons & 4) ? MK_MBUTTON : 0);
}

// One application owns the output bindings, window thread and guest input.
class win32_application
{
public:
	explicit win32_application(CConfigurator* cfg);
	~win32_application();
	bx_win32_gui_c& create_display();
	bx_win32_gui_c& display_for_output(const std::string&, unsigned);
	const bx_win32_gui_c* find_display_for_output(const std::string&, unsigned) const;
	void initialize();
	void pump();
	void stop();
	void invoke(std::function<void()> fn);
	void exit_displays();
	void poll_input(unsigned current_key_scan = 512);
	void pause_input();
	void resume_input() { input_paused = false; release_grabbed_keys(); set_keyboard_capture(capture_owner != nullptr); }
	void detach_input() { keyboard = nullptr; system = nullptr; set_keyboard_capture(false); }
	void request_shutdown();
	void record_callback_error() noexcept;
	void observe_key(unsigned scan, unsigned virtual_key, bool released, bool repeat, bool release_previous_press = true);
	void send_key(unsigned scan, bool released, unsigned host_key = 0, unsigned virtual_key = 0);
	bool consume_hotkey_key(unsigned scan, bool released, bool repeat, bool allow_repeat = false);
	void suppress_hotkey_keys(unsigned scan, unsigned modifiers, bool release_modifiers = true);
	void suppress_fullscreen_key(unsigned scan);
	void send_ctrl_alt_delete();
	void send_mouse(int dx, int dy, int dz, unsigned buttons);
	void send_host_mouse(double dx, double dy, int dz, unsigned buttons);
	void set_mouse_capture(bx_win32_gui_c* target);
	void claim_fullscreen_monitor(bx_win32_gui_c* target);
	void show_secondary_displays();
	void release_input_session();
	void release_media_input();
	bool media_key_event(UINT, WPARAM, LPARAM, bool synthetic_key = false);
	bool needs_print_screen_press() const
		{ return keyboard && !input_paused && !shutting_down && !print_screen_down_seen; }
	bool capture_keyboard_event(WPARAM, const KBDLLHOOKSTRUCT&) noexcept;
	void dispatch_captured_keys();
	bool filter_native_altgr(UINT, WPARAM, LPARAM);
	bool consume_native_key(unsigned, unsigned, bool, DWORD);
	unsigned keyboard_modifiers() const;
	bool dispatching_captured_key = false;
	bx_win32_gui_c* window_owner(HWND window) const;
	bx_keymap_c* keymap() const { return shared_keymap.get(); }
	HWND manager_host() const { return managed_host; }

	// Accessed only on the application thread, including callbacks.
	std::map<HWND, bx_win32_gui_c*> windows;
	bx_win32_gui_c* capture_owner = nullptr;
	unsigned mouse_buttons = 0;
	win32_media_selector media;
	win32_hotkey_binding hotkey_mouse_capture, hotkey_media, hotkey_ctrl_alt_delete, hotkey_reset_window;
	win32_hotkey_binding hotkey_scale_up, hotkey_scale_down;
	int configured_scale = 1;
	bool scale_change_enable = false;
	bool video_linear = true;
	bool mouse_absolute = false;
	bool input_paused = false;
	bool shutting_down = false;
private:
	void release_guest_keys();
	void reconcile_keyboard_releases(unsigned current_key_scan);
	void reconcile_mouse_releases();
	bool set_raw_mouse_target(HWND);
	void discard_raw_mouse_input(HWND);
	void sync_input();
	void set_keyboard_capture(bool);
	void release_grabbed_keys();
	SHORT keyboard_key_state(unsigned) const;
	CConfigurator* cfg;
	std::unique_ptr<bx_keymap_c> shared_keymap;
	std::vector<std::unique_ptr<bx_win32_gui_c>> displays;
	std::map<std::pair<std::string, unsigned>, bx_win32_gui_c*> outputs;
	const DWORD owner_thread = GetCurrentThreadId();
	HWND dispatcher = nullptr;
	HWND managed_host = nullptr;
	HANDLE manager_process = nullptr;
	DWORD manager_pid = 0;
	HWND raw_mouse_target = nullptr;
	unsigned mouse_capture_transition = 0;
	bool fullscreen_handoff = false;
	EXECUTION_STATE previous_execution_state = 0;
	std::atomic<bool> running{ false };
	CKeyboard* keyboard = nullptr;
	CSystem* system = nullptr;
	bool guest_mouse_captured = false;
	double mouse_speed = 1.0;
	bool mouse_invert_x = false, mouse_invert_y = false;
	// One guest pointer retains fractional motion across output handoffs.
	double mouse_remainder_x = 0.0, mouse_remainder_y = 0.0;
	bool input_reset_pending = false;
	bool keyboard_use_mapping = false;
	bool guest_keys[512] = {};
	u32 guest_key_by_scan[512] = {};
	unsigned host_virtual_key_by_scan[512] = {};
	unsigned guest_virtual_key_by_scan[512] = {};
	bool guest_key_state_ambiguous[512] = {};
	bool reconciled_key_releases[512] = {};
	bool pending_keyboard_releases[512] = {};
	bool swallowed_hotkey_keys[512] = {};
	bool fullscreen_hotkey_pressed[512] = {};
	// Physical press history survives guest releases at focus/dialog barriers.
	bool print_screen_down_seen = false;
	// Keep physical Control provenance separate from guest/session releases.
	bool native_altgr_control = false, native_left_control_down = false;
	bool native_altgr_press_pending = false;
	bool native_altgr_release_pending = false;
	DWORD native_altgr_release_time = 0;
	HHOOK keyboard_hook = nullptr;
	bool keyboard_hook_failed = false;
	bool keyboard_capture_starting = false;
	bool hook_scan_down[512] = {};
	bool captured_key_down[256] = {}, pre_hook_key_down[256] = {};
	// Bounded storage keeps allocation and guest work out of the hook callback.
	static constexpr unsigned keyboard_queue_capacity = 1024;
	win32_captured_key captured_keys[keyboard_queue_capacity] = {};
	unsigned captured_key_first = 0, captured_key_count = 0;
	win32_native_key native_keys[keyboard_queue_capacity] = {};
	unsigned next_native_key = 0;
	std::exception_ptr callback_error;
};

class bx_win32_gui_c : public bx_gui_c
{
public:
	bx_win32_gui_c(win32_application& app, std::size_t ordinal)
		: application(app), window_title("ES40 - Output " + std::to_string(ordinal)), primary(ordinal == 0) {}
	void specific_init(unsigned, unsigned) override;
	// GPU renderers submit complete ARGB frames, including VGA text, as with SDL.
	void text_update(u8*, u8*, unsigned long, unsigned long,
		bx_vga_tminfo_t, unsigned) override {}
	void graphics_tile_update(u8*, unsigned, unsigned) override {}
	bx_svga_tileinfo_t* graphics_tile_info(bx_svga_tileinfo_t*) override { return nullptr; }
	u8* graphics_tile_get(unsigned, unsigned, unsigned*, unsigned*) override { return nullptr; }
	void graphics_tile_update_in_place(unsigned, unsigned, unsigned, unsigned) override {}
	void graphics_frame_update(const u32*, unsigned, unsigned) override;
	void handle_events() override;
	void flush() override {}
	void clear_screen() override;
	bool palette_change(unsigned, unsigned, unsigned, unsigned) override { return true; }
	void dimension_update(unsigned, unsigned, unsigned = 0,
		unsigned = 0, unsigned = 8) override;
	void mouse_enabled_changed_specific(bool) override;
	void get_capabilities(u16*, u16*, u16*) override;
	void exit() override;
	void exit_application() override;
	bx_gui_c& display_for_output(const std::string& path, unsigned id) override
		{ return application.display_for_output(path, id); }
	const bx_gui_c* find_display_for_output(const std::string& path, unsigned id) const override
		{ return application.find_display_for_output(path, id); }
	bool requires_main_thread() override { return true; }
	void main_thread_init() override { application.initialize(); }
	void main_thread_pump() override { application.pump(); }
	void main_thread_stop() override { application.stop(); }
	void pause_guest_input() override
		{ application.invoke([this] { application.pause_input(); }); }
	void resume_guest_input() override
		{ application.invoke([this] { application.resume_input(); }); }
	void detach_guest_input() override
		{ application.invoke([this] { application.detach_input(); }); }
private:
	friend class win32_application;
	friend LRESULT CALLBACK win32_window_proc(HWND, UINT, WPARAM, LPARAM);
	void initialize_window();
	void dimension_update_impl(unsigned, unsigned);
	int window_scale() const;
	void update_driven_size();
	RECT window_rect_for_client(int, int) const;
	void resize_window_to_guest();
	void reset_window_size();
	void adjust_window_scale(int);
	void update_mouse_geometry();
	void exit_impl();
	void update_title();
	void paint(HDC);
	const u32* linear_frame(unsigned, unsigned);
	void prepare_paint_buffer(HDC, int, int);
	void release_paint_buffer();
	void mouse_event(int, int, int, unsigned, bool motion = false);
	void raw_mouse_event(HRAWINPUT);
	void reset_absolute_mouse_position()
		{ mouse_position_valid = false; raw_mouse_positions.clear(); }
	bool confine_mouse();
	void toggle_fullscreen();
	LRESULT window_event(HWND, UINT, WPARAM, LPARAM, bool synthetic_key = false);
	win32_application& application;
	HWND window = nullptr;
	const std::string window_title;
	const bool primary;
	HWND embedded_host = nullptr;
	std::vector<u32> pixels;
	std::vector<u32> filtered_pixels;
	std::vector<win32_linear_sample> linear_columns;
	unsigned width = 0, height = 0;
	int driven_width = 0, driven_height = 0;
	bool pending_guest_resize = false;
	int runtime_scale_override = 0;
	HDC paint_dc = nullptr;
	HBITMAP paint_bitmap = nullptr;
	HGDIOBJ paint_original_bitmap = nullptr;
	int paint_width = 0, paint_height = 0;
	int mouse_last_x = 0, mouse_last_y = 0;
	bool mouse_position_valid = false, mouse_tracking = false;
	std::map<HANDLE, win32_raw_mouse_position> raw_mouse_positions;
	int wheel_remainder = 0;
	unsigned ignored_mouse_buttons = 0;
	bool retired = false;
	bool fullscreen = false;
	bool fullscreen_transition = false;
	WINDOWPLACEMENT windowed_placement = { sizeof(WINDOWPLACEMENT) };
};

static std::unique_ptr<win32_application> win32_app;

static const u32 win32_to_bx_key[2][0x100] =
{
  {     /* normal-keys */
	/* 0x00 - 0x0f */
	0,
	BX_KEY_ESC,
	BX_KEY_1,
	BX_KEY_2,
	BX_KEY_3,
	BX_KEY_4,
	BX_KEY_5,
	BX_KEY_6,
	  BX_KEY_7,
	  BX_KEY_8,
	  BX_KEY_9,
	  BX_KEY_0,
	  BX_KEY_MINUS,
	  BX_KEY_EQUALS,
		BX_KEY_BACKSPACE,
		BX_KEY_TAB,

		/* 0x10 - 0x1f */
		BX_KEY_Q,
		BX_KEY_W,
		BX_KEY_E,
		BX_KEY_R,
		BX_KEY_T,
		BX_KEY_Y,
		BX_KEY_U,
		  BX_KEY_I,
		  BX_KEY_O,
		  BX_KEY_P,
		  BX_KEY_LEFT_BRACKET,
		  BX_KEY_RIGHT_BRACKET,
			BX_KEY_ENTER,
			BX_KEY_CTRL_L,
			BX_KEY_A,
			BX_KEY_S,

			/* 0x20 - 0x2f */
			BX_KEY_D,
			BX_KEY_F,
			BX_KEY_G,
			BX_KEY_H,
			BX_KEY_J,
			BX_KEY_K,
			BX_KEY_L,
			  BX_KEY_SEMICOLON,
			  BX_KEY_SINGLE_QUOTE,
			  BX_KEY_GRAVE,
			  BX_KEY_SHIFT_L,
				BX_KEY_BACKSLASH,
				BX_KEY_Z,
				BX_KEY_X,
				BX_KEY_C,
				BX_KEY_V,

				/* 0x30 - 0x3f */
				BX_KEY_B,
				BX_KEY_N,
				BX_KEY_M,
				BX_KEY_COMMA,
				BX_KEY_PERIOD,
				BX_KEY_SLASH,
				  BX_KEY_SHIFT_R,
				  BX_KEY_KP_MULTIPLY,
				  BX_KEY_ALT_L,
				  BX_KEY_SPACE,
					BX_KEY_CAPS_LOCK,
					BX_KEY_F1,
					BX_KEY_F2,
					BX_KEY_F3,
					BX_KEY_F4,
					BX_KEY_F5,

					/* 0x40 - 0x4f */
					BX_KEY_F6,
					BX_KEY_F7,
					BX_KEY_F8,
					BX_KEY_F9,
					BX_KEY_F10,
					BX_KEY_PAUSE,
					  BX_KEY_SCRL_LOCK,
					  BX_KEY_KP_HOME,
					  BX_KEY_KP_UP,
					  BX_KEY_KP_PAGE_UP,
						BX_KEY_KP_SUBTRACT,
						BX_KEY_KP_LEFT,
						BX_KEY_KP_5,
						BX_KEY_KP_RIGHT,
						  BX_KEY_KP_ADD,
						  BX_KEY_KP_END,

						  /* 0x50 - 0x5f */
						  BX_KEY_KP_DOWN,
						  BX_KEY_KP_PAGE_DOWN,
						  BX_KEY_KP_INSERT,
							BX_KEY_KP_DELETE,
							0,
							0,
							BX_KEY_LEFT_BACKSLASH,
							BX_KEY_F11,
							  BX_KEY_F12,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,

							  /* 0x60 - 0x6f */
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,

							  /* 0x70 - 0x7f */
							  0,  /* Todo: "Katakana" key ( ibm 133 ) for Japanese 106 keyboard */
							  0,
							  0,
							  0,  /* Todo: "Ro" key ( ibm 56 ) for Japanese 106 keyboard */
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,  /* Todo: "convert" key ( ibm 132 ) for Japanese 106 keyboard */
							  0,
							  0,  /* Todo: "non-convert" key ( ibm 131 ) for Japanese 106 keyboard */
							  0,
							  0,  /* Todo: "Yen" key ( ibm 14 ) for Japanese 106 keyboard */
							  0,
							  0,
							},
							{     /* extended-keys */
							  /* 0x00 - 0x0f */
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,

							  /* 0x10 - 0x1f */
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  BX_KEY_KP_ENTER,
							  BX_KEY_CTRL_R,
							  0,
							  0,

							  /* 0x20 - 0x2f */
							  0,
							  BX_KEY_POWER_CALC,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,
							  0,

							  /* 0x30 - 0x3f */
							  0,
							  0,
							  BX_KEY_INT_HOME,
							  0,
							  0,
							  BX_KEY_KP_DIVIDE,
							  0,
							  BX_KEY_PRINT,
								BX_KEY_ALT_R,
								0,
								0,
								0,
								0,
								0,
								0,
								0,

								/* 0x40 - 0x4f */
								0,
								0,
								0,
								0,
								0,
								BX_KEY_NUM_LOCK,
								BX_KEY_PAUSE,
								BX_KEY_HOME,
								BX_KEY_UP,
								  BX_KEY_PAGE_UP,
								  0,
								  BX_KEY_LEFT,
								  0,
								  BX_KEY_RIGHT,
								  0,
								  BX_KEY_END,

								  /* 0x50 - 0x5f */
								  BX_KEY_DOWN,
								  BX_KEY_PAGE_DOWN,
								  BX_KEY_INSERT,
									BX_KEY_DELETE,
									0,
									0,
									0,
									0,
									0,
									0,
									0,
									BX_KEY_WIN_L,
									BX_KEY_WIN_R,
									  BX_KEY_MENU,
									  BX_KEY_POWER_POWER,
									  BX_KEY_POWER_SLEEP,

									  /* 0x60 - 0x6f */
									  0,
									  0,
									  0,
									  BX_KEY_POWER_WAKE,
									  0,
									  BX_KEY_INT_SEARCH,
									  BX_KEY_INT_FAV,
									  0,
										BX_KEY_INT_STOP,
										BX_KEY_INT_FORWARD,
										BX_KEY_INT_BACK,
										BX_KEY_POWER_MYCOMP,
										  BX_KEY_INT_MAIL,
										  0,
										  0,
										  0,
										}
};


win32_application::win32_application(CConfigurator* cfg)
	: media([this](UINT msg, WPARAM key, LPARAM flags) { return media_key_event(msg, key, flags); },
		[this] { release_media_input(); }, [this] { if (system) system->RequestSystemReset(); }),
	  cfg(cfg), shared_keymap(new bx_keymap_c(cfg)) {}

win32_application::~win32_application()
{
	// Normal teardown is explicit after the workers join. Cover startup errors
	// too; before main_thread_init(), no display can have native resources.
	try { exit_displays(); } catch (...) {}
	for (const auto& display : displays)
		if (bx_gui == display.get()) bx_gui = nullptr;
	if (bx_keymap == shared_keymap.get()) bx_keymap = nullptr;
}

bx_win32_gui_c& win32_application::create_display()
{
	if (shutting_down)
		FAILURE(Win32, "Cannot create a display after application shutdown");
	auto display = std::make_unique<bx_win32_gui_c>(*this, displays.size());
	displays.push_back(std::move(display));
	return *displays.back();
}

bx_win32_gui_c& win32_application::display_for_output(
	const std::string& device_path, unsigned output_id)
{
	if (shutting_down)
		FAILURE(Win32, "Cannot bind a display after application shutdown");
	if (device_path.empty())
		FAILURE(Configuration, "Display output requires a device identity");
	const auto key = std::make_pair(device_path, output_id);
	const auto existing = outputs.find(key);
	if (existing != outputs.end()) return *existing->second;
	const auto entry = outputs.emplace(key, nullptr).first;
	try
	{
		entry->second = outputs.size() == 1 && !displays.empty()
			? displays.front().get() : &create_display();
	}
	catch (...) { outputs.erase(entry); throw; }
	return *entry->second;
}

const bx_win32_gui_c* win32_application::find_display_for_output(
	const std::string& device_path, unsigned output_id) const
{
	if (shutting_down || device_path.empty()) return nullptr;
	const auto entry = outputs.find(std::make_pair(device_path, output_id));
	return entry == outputs.end() ? nullptr : entry->second;
}

int libwin32_LTX_plugin_init(CConfigurator* cfg)
{
	if (bx_gui || win32_app)
		FAILURE(Configuration, "A GUI application is already installed");
	auto app = std::make_unique<win32_application>(cfg);
	auto& initial_display = app->create_display();
	win32_app = std::move(app);
	bx_gui = &initial_display;
	bx_keymap = win32_app->keymap();
	printf("%%GUI-I-INS: Installing win32 module as the ES40 GUI\n");
	return 0;
}

void libwin32_LTX_plugin_fini() {}

static void register_win32_class(const char* name, WNDPROC proc)
{
	WNDCLASS wc = {};
	wc.lpfnWndProc = proc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = name;
	if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		FAILURE(Win32, "Unable to register display window class");
}

void win32_application::initialize()
{
	if (GetCurrentThreadId() != owner_thread || shutting_down)
		FAILURE(Win32, "Invalid display application initialization");
	if (dispatcher) return;
	keyboard_use_mapping = cfg->get_bool_value("keyboard.use_mapping", false);
	if (keyboard_use_mapping)
	{
		// Publish only a complete map. No dispatcher or input can observe a
		// partial load, and a failed initialization can retry from scratch.
		auto loaded = std::make_unique<bx_keymap_c>(cfg);
		loaded->loadKeymap(win32_keymap_symbol);
		const bool published = bx_keymap == shared_keymap.get();
		shared_keymap.swap(loaded);
		if (published) bx_keymap = shared_keymap.get();
	}
	const char* speed = cfg->get_text_value("mouse.speed", "1.0");
	mouse_speed = std::atof(speed);
	if (!std::isfinite(mouse_speed) || mouse_speed <= 0.0 ||
		mouse_speed > 10.0)
	{
		printf("%%WIN32-W-MOUSESPEED: invalid mouse.speed \"%s\" "
			"(valid: 0.0 < speed <= 10.0); using 1.0.\n", speed);
		mouse_speed = 1.0;
	}
	mouse_invert_x = cfg->get_bool_value("mouse.invert_x", false);
	mouse_invert_y = cfg->get_bool_value("mouse.invert_y", false);
	mouse_absolute = cfg->get_bool_value("mouse.absolute", false);
	const u64 scale = cfg->get_num_value("video.scale_ratio", true, 0);
	if (scale > 10)
		printf("%%WIN32-W-SCALE: invalid video.scale_ratio %llu (valid: 0 - 10); "
			"using 1x.\n", (unsigned long long)scale);
	configured_scale = scale >= 1 && scale <= 10 ? (int)scale : 1;
	scale_change_enable = cfg->get_bool_value("video.scale_change_enable", false);
	video_linear = cfg->get_bool_value("video.linear", true);
	hotkey_mouse_capture = parse_win32_hotkey(cfg, "hotkey.mouse_capture", "Ctrl+F10");
	hotkey_media = parse_win32_hotkey(cfg, "hotkey.media", "Ctrl+F11");
	hotkey_ctrl_alt_delete = parse_win32_hotkey(cfg, "hotkey.ctrl_alt_delete", "Ctrl+Alt+End");
	hotkey_reset_window = parse_win32_hotkey(cfg, "hotkey.reset_window", "Ctrl+Alt+Home");
	hotkey_scale_up = parse_win32_hotkey(cfg, "hotkey.scale_up", "Ctrl+PageUp");
	hotkey_scale_down = parse_win32_hotkey(cfg, "hotkey.scale_down", "Ctrl+PageDown");
	win32_hotkey_binding* bindings[] = {
		&hotkey_mouse_capture, &hotkey_media, &hotkey_ctrl_alt_delete, &hotkey_reset_window,
		&hotkey_scale_up, &hotkey_scale_down
	};
	const bool active[] = {
		hotkey_mouse_capture.enabled, hotkey_media.enabled, hotkey_ctrl_alt_delete.enabled, hotkey_reset_window.enabled,
		hotkey_scale_up.enabled && scale_change_enable, hotkey_scale_down.enabled && scale_change_enable
	};
	constexpr size_t binding_count = sizeof(bindings) / sizeof(bindings[0]);
	bool duplicate[binding_count] = {};
	for (size_t i = 0; i < binding_count; ++i)
	{
		for (size_t j = i + 1; j < binding_count; ++j)
		{
			if (active[i] && active[j] &&
				bindings[i]->key == bindings[j]->key &&
				bindings[i]->modifiers == bindings[j]->modifiers)
			{
				printf("%%WIN32-W-HOTKEY: %s and %s both use \"%s\"; both actions disabled.\n",
					bindings[i]->config_name, bindings[j]->config_name, bindings[i]->display.c_str());
				duplicate[i] = duplicate[j] = true;
			}
		}
	}
	for (size_t i = 0; i < binding_count; ++i)
		if (duplicate[i]) bindings[i]->enabled = false;
	register_win32_class(win32_window_class, win32_window_proc);
	register_win32_class(win32_dispatch_class, win32_dispatch_proc);
	dispatcher = CreateWindowEx(0, win32_dispatch_class, "", 0, 0, 0,
		0, 0, HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), this);
	if (!dispatcher)
		FAILURE(Win32, "Unable to create display dispatcher");
	keyboard = theKeyboard;
	system = theSystem;
	if (keyboard) keyboard->set_mouse_capture(false);
	// Match SDL's default: guest video keeps the host display on even when
	// there is no host input. Restore the caller's thread state at shutdown.
	previous_execution_state = SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
	if (!previous_execution_state)
		printf("%%WIN32-W-DISPLAYPOWER: Unable to prevent host display blanking.\n");
	running.store(true, std::memory_order_release);
	const char* host_value = getenv("ES40_MANAGER_HOST");
	if (host_value && *host_value)
	{
		char* end = nullptr;
		const unsigned long long host_number = _strtoui64(host_value, &end, 10);
		if (!end || *end || !host_number || host_number > UINTPTR_MAX)
			FAILURE(Win32, "Invalid machine manager host window");
		managed_host = reinterpret_cast<HWND>(static_cast<UINT_PTR>(host_number));
		const char* pid_value = getenv("ES40_MANAGER_PID");
		const unsigned long long pid_number = pid_value ? _strtoui64(pid_value, &end, 10) : 0;
		if (!pid_number || !end || *end || pid_number > MAXDWORD)
			FAILURE(Win32, "Invalid machine manager process");
		manager_pid = static_cast<DWORD>(pid_number);
		wchar_t host_class[64] = {};
		DWORD host_pid = 0;
		GetWindowThreadProcessId(managed_host, &host_pid);
		if (host_pid != manager_pid || manager_pid == GetCurrentProcessId() ||
			!GetClassNameW(managed_host, host_class, 64) || wcscmp(host_class, win32_manager_host_class))
			FAILURE(Win32, "Machine manager host is no longer available");
		manager_process = OpenProcess(SYNCHRONIZE, FALSE, manager_pid);
		if (!manager_process || WaitForSingleObject(manager_process, 0) != WAIT_TIMEOUT)
			FAILURE(Win32, "Machine manager process is no longer available");
		// Allow stop and media commands without a GPU.
		if (!displays.empty()) displays.front()->initialize_window();
	}
}

void win32_application::invoke(std::function<void()> fn)
{
	if (GetCurrentThreadId() == owner_thread) { fn(); return; }
	if (!running.load(std::memory_order_acquire) || !dispatcher)
		FAILURE(Win32, "Display application thread is not running");
	win32_call call = { std::move(fn), nullptr };
	if (SendMessage(dispatcher, win32_invoke_message, 0, (LPARAM)&call) != 1)
		FAILURE(Win32, "Unable to reach the display application thread");
	if (call.error) std::rethrow_exception(call.error);
}

static LRESULT CALLBACK win32_dispatch_proc(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_NCCREATE)
		SetWindowLongPtr(hwnd, GWLP_USERDATA,
			(LONG_PTR)reinterpret_cast<CREATESTRUCT*>(lparam)->lpCreateParams);
	if (msg == win32_invoke_message)
	{
		auto& call = *reinterpret_cast<win32_call*>(lparam);
		try { call.fn(); } catch (...) { call.error = std::current_exception(); }
		return 1;
	}
	if (msg == win32_wake_message)
	{
		auto* app = reinterpret_cast<win32_application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		if (app)
		{
			try { app->dispatch_captured_keys(); app->media.close(); }
			catch (...) { app->record_callback_error(); }
		}
		return 0;
	}
	if (msg == win32_keyboard_message)
	{
		auto* app = reinterpret_cast<win32_application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		if (app)
		{
			try { app->dispatch_captured_keys(); }
			catch (...) { app->record_callback_error(); }
		}
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK win32_keyboard_proc(int code, WPARAM msg, LPARAM data)
{
	if (code == HC_ACTION && win32_keyboard_application &&
		win32_keyboard_application->capture_keyboard_event(msg, *reinterpret_cast<KBDLLHOOKSTRUCT*>(data)))
		return 1;
	return CallNextHookEx(nullptr, code, msg, data);
}

void win32_application::set_keyboard_capture(bool enabled)
{
	if (enabled)
	{
		if (keyboard_hook || keyboard_capture_starting || !keyboard || input_paused || shutting_down || callback_error || media.active() ||
			!running.load(std::memory_order_acquire)) return;
		// Finish keys already queued before installing the hook.
		const unsigned transition = mouse_capture_transition;
		MSG pending;
		while (PeekMessage(&pending, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE | PM_QS_INPUT))
		{
			if (pending.message == WM_QUIT) { request_shutdown(); return; }
			TranslateMessage(&pending); DispatchMessage(&pending);
			if (transition != mouse_capture_transition || keyboard_hook || input_paused || shutting_down || media.active()) return;
		}
		if (transition != mouse_capture_transition || keyboard_hook || !keyboard || input_paused ||
			shutting_down || media.active() || !running.load(std::memory_order_acquire) ||
			!capture_owner || !win32_display_active(capture_owner->window)) return;
		BYTE state[256];
		HMODULE module = nullptr;
		if (!GetKeyboardState(state) ||
			!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(win32_keyboard_proc), &module))
		{
			printf("%%WIN32-W-KEYBOARD: Unable to prepare keyboard capture.\n");
			return;
		}
		std::fill_n(hook_scan_down, 512, false);
		for (unsigned key = 0; key < 256; ++key)
		{
			captured_key_down[key] = pre_hook_key_down[key] = (state[key] & 0x80) != 0;
			if (key == VK_LCONTROL && native_altgr_control)
				captured_key_down[key] = native_left_control_down;
			// Shared VKs cannot identify which physical key was already held.
			if (!captured_key_down[key] || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU ||
				key == VK_RETURN || key == VK_CLEAR || (key >= VK_PRIOR && key <= VK_DOWN) ||
				key == VK_INSERT || key == VK_DELETE) continue;
			const unsigned scan = win32_key_scan(key, 0, false);
			if (scan) hook_scan_down[scan] = true;
		}
		for (unsigned scan = 0; scan < 512; ++scan)
			if (host_virtual_key_by_scan[scan] && pre_hook_key_down[host_virtual_key_by_scan[scan]]) hook_scan_down[scan] = true;
		keyboard_hook_failed = false;
		struct starting_guard
		{
			bool& starting;
			starting_guard(bool& value) : starting(value) { starting = true; }
			~starting_guard() { starting = false; }
		} guard(keyboard_capture_starting);
		keyboard_hook = SetWindowsHookExA(WH_KEYBOARD_LL, win32_keyboard_proc, module, 0);
		if (keyboard_hook) win32_keyboard_application = this;
		else printf("%%WIN32-W-KEYBOARD: Unable to capture system shortcuts (error %lu).\n", GetLastError());
		// Close the gap between the first drain and installation.
		while (keyboard_hook && PeekMessage(&pending, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE | PM_QS_INPUT))
		{
			if (pending.message == WM_QUIT) { request_shutdown(); set_keyboard_capture(false); return; }
			TranslateMessage(&pending); DispatchMessage(&pending);
			if (transition != mouse_capture_transition || input_paused || shutting_down || media.active()) break;
		}
		if (transition != mouse_capture_transition || !keyboard || input_paused || shutting_down || media.active() ||
			!running.load(std::memory_order_acquire) || !capture_owner ||
			!win32_display_active(capture_owner->window))
			set_keyboard_capture(false);
		return;
	}
	if (win32_keyboard_application == this) win32_keyboard_application = nullptr;
	if (!keyboard_hook) return;
	UnhookWindowsHookEx(keyboard_hook);
	keyboard_hook = nullptr;
	// Retired input must not reach the guest.
	for (unsigned i = 0; i < captured_key_count; ++i)
	{
		const auto& event = captured_keys[(captured_key_first + i) % keyboard_queue_capacity];
		observe_key(event.scan, event.key, event.released, event.repeat, false);
	}
	captured_key_first = captured_key_count = 0;
	// Windows did not see intercepted presses.
	if (keyboard)
		for (unsigned scan = 0; scan < 512; ++scan)
			if (guest_keys[scan] && win32_grabbed_key(guest_virtual_key_by_scan[scan]))
				pending_keyboard_releases[scan] = true;
	release_grabbed_keys();
}

void win32_application::release_grabbed_keys()
{
	if (!keyboard || input_paused) return;
	for (unsigned scan = 0; scan < 512; ++scan)
		if (pending_keyboard_releases[scan])
		{
			send_key(scan, true);
			host_virtual_key_by_scan[scan] = 0;
			reconciled_key_releases[scan] = true;
			pending_keyboard_releases[scan] = false;
		}
}

bool win32_application::capture_keyboard_event(WPARAM msg, const KBDLLHOOKSTRUCT& event) noexcept
{
	if (!keyboard_hook || keyboard_hook_failed || input_paused || !keyboard || shutting_down || callback_error || media.active() ||
		!running.load(std::memory_order_acquire)) return false;
	HWND focus = GetFocus();
	if (!capture_owner || !window_owner(focus) || !win32_display_active(focus))
	{
		PostMessage(dispatcher, win32_keyboard_message, 0, 0);
		return false;
	}
	if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN && msg != WM_KEYUP && msg != WM_SYSKEYUP) return false;
	const bool released = msg == WM_KEYUP || msg == WM_SYSKEYUP;
	// Windows adds a synthetic left Control to AltGr; SDL excludes it too.
	if ((event.scanCode & ~0x80u) == 0x21d)
	{
		// Balance a synthetic press Windows saw before capture began.
		if (released && native_altgr_control)
		{
			native_altgr_release_pending = true;
			native_altgr_release_time = event.time;
			return false;
		}
		return true;
	}
	const unsigned scan = win32_key_scan(event.vkCode, event.scanCode,
		(event.flags & LLKHF_EXTENDED) != 0);
	const unsigned key = win32_key_state_key(event.vkCode, scan);
	if (!key) return false;
	const bool pass_native = !win32_grabbed_key(key) ||
		(released && (keyboard_capture_starting || pre_hook_key_down[key] || !hook_scan_down[scan]));
	unsigned native_slot = next_native_key;
	if (pass_native)
		for (unsigned count = 0; count < keyboard_queue_capacity && native_keys[native_slot].pending; ++count)
			native_slot = (native_slot + 1) % keyboard_queue_capacity;
	if (captured_key_count == keyboard_queue_capacity || (pass_native && native_keys[native_slot].pending))
	{
		keyboard_hook_failed = true;
		PostMessage(dispatcher, win32_keyboard_message, 0, 0);
		return false;
	}
	const unsigned slot = (captured_key_first + captured_key_count) % keyboard_queue_capacity;
	captured_keys[slot] = { focus, key, scan, released, !released && hook_scan_down[scan] };
	++captured_key_count;
	if (!PostMessage(dispatcher, win32_keyboard_message, 0, 0))
	{
		--captured_key_count;
		keyboard_hook_failed = true;
		return false;
	}
	if (pass_native)
	{
		native_keys[native_slot] = { event.time, key, released, true };
		next_native_key = (native_slot + 1) % keyboard_queue_capacity;
	}
	hook_scan_down[scan] = !released;
	if (released) pre_hook_key_down[key] = false;
	return !pass_native;
}

bool win32_application::filter_native_altgr(UINT msg, WPARAM key, LPARAM flags)
{
	const bool released = msg == WM_KEYUP || msg == WM_SYSKEYUP;
	const DWORD time = (DWORD)GetMessageTime();
	if (native_altgr_release_pending && (LONG)(time - native_altgr_release_time) > 0)
		native_altgr_release_pending = false;
	const unsigned scan = HIWORD(flags) & 0x1ff;
	if ((key == VK_MENU || key == VK_RMENU) && scan == 0x138)
	{
		native_altgr_press_pending = false;
		if (released && native_altgr_control)
		{
			// The synthetic Control release can follow the RightAlt release.
			native_altgr_release_pending = true;
			native_altgr_release_time = time;
		}
	}
	if ((key != VK_CONTROL && key != VK_LCONTROL) || (scan & 0x100)) return false;
	if (released && !native_altgr_control && !native_altgr_release_pending) return false;
	bool synthetic = released && native_altgr_release_pending && native_altgr_release_time == time;
	MSG next;
	if (!synthetic && PeekMessage(&next, nullptr, 0, 0, PM_NOREMOVE))
	{
		// Like SDL, recognize AltGr's fake Control by its adjacent RightAlt.
		const bool matching_message = released ? next.message == WM_KEYUP || next.message == WM_SYSKEYUP :
			next.message == WM_KEYDOWN || next.message == WM_SYSKEYDOWN;
		synthetic = matching_message && next.time == time &&
			(next.wParam == VK_MENU || next.wParam == VK_RMENU) &&
			(HIWORD(win32_key_flags(next.wParam, next.lParam)) & 0x1ff) == 0x138;
	}
	if (!synthetic) return false;
	if (!released && !native_altgr_control && (flags & (LPARAM(1) << 30)))
		native_left_control_down = true; // Genuine Control was already held before this fake press.
	// A paired fake release must not release a separately held genuine Control.
	native_altgr_control = !released;
	native_altgr_press_pending = !released;
	if (released) native_altgr_release_pending = false;
	return true;
}

bool win32_application::consume_native_key(unsigned key, unsigned scan, bool released, DWORD time)
{
	key = win32_key_state_key(key, scan);
	for (auto& event : native_keys)
	{
		// Some host keys (ex Print Screen) omit a native message.
		if (event.pending && (LONG)(time - event.time) > 0) event.pending = false;
		if (event.pending && event.key == key && event.released == released && event.time == time)
		{
			event.pending = false;
			return true;
		}
	}
	// Input already queued when capture began has no hook duplicate. 
	if (keyboard_hook && key)
	{
		captured_key_down[key] = !released;
		if (keyboard_capture_starting)
		{
			bool newer_event = false;
			bool queued_release = false;
			for (unsigned i = 0; i < captured_key_count; ++i)
			{
				auto& queued = captured_keys[(captured_key_first + i) % keyboard_queue_capacity];
				if (queued.scan != scan) continue;
				if (!newer_event) queued.repeat = !queued.released && !released;
				newer_event = true;
				queued_release = queued_release || queued.released;
			}
			pre_hook_key_down[key] = !released && !queued_release;
			if (!newer_event)
			{
				hook_scan_down[scan] = !released;
			}
		}
	}
	return false;
}

SHORT win32_application::keyboard_key_state(unsigned key) const
{
	if (!keyboard_hook || key >= 256)
	{
		if (native_altgr_control && (key == VK_LCONTROL || key == VK_CONTROL))
			return native_left_control_down || (key == VK_CONTROL && (GetKeyState(VK_RCONTROL) & 0x8000))
				? (SHORT)0x8000 : 0;
		return GetKeyState(key);
	}
	bool down = captured_key_down[key];
	if (key == VK_CONTROL) down = captured_key_down[VK_LCONTROL] || captured_key_down[VK_RCONTROL];
	if (key == VK_MENU) down = captured_key_down[VK_LMENU] || captured_key_down[VK_RMENU];
	if (key == VK_SHIFT) down = captured_key_down[VK_LSHIFT] || captured_key_down[VK_RSHIFT];
	return down ? (SHORT)0x8000 : 0;
}

unsigned win32_application::keyboard_modifiers() const
{
	return ((keyboard_key_state(VK_CONTROL) & 0x8000) ? WIN32_HOTKEY_CTRL : 0) |
		((keyboard_key_state(VK_MENU) & 0x8000) ? WIN32_HOTKEY_ALT : 0) |
		((keyboard_key_state(VK_SHIFT) & 0x8000) ? WIN32_HOTKEY_SHIFT : 0) |
		(((keyboard_key_state(VK_LWIN) | keyboard_key_state(VK_RWIN)) & 0x8000) ? WIN32_HOTKEY_GUI : 0);
}

void win32_application::dispatch_captured_keys()
{
	if (dispatching_captured_key || keyboard_capture_starting) return;
	if (!running.load(std::memory_order_acquire)) { set_keyboard_capture(false); return; }
	if (keyboard_hook_failed)
	{
		set_keyboard_capture(false);
		keyboard_hook_failed = false;
		printf("%%WIN32-W-KEYBOARD: Keyboard capture queue failed; system shortcuts released.\n");
		return;
	}
	if (keyboard_hook && (!window_owner(GetFocus()) || !win32_display_active(GetFocus())))
		{ release_input_session(); return; }
	while (keyboard_hook && captured_key_count)
	{
		const auto event = captured_keys[captured_key_first];
		captured_key_first = (captured_key_first + 1) % keyboard_queue_capacity;
		--captured_key_count;
		auto* target = window_owner(event.window);
		if (!target) continue;
		captured_key_down[event.key] = !event.released;
		LPARAM flags = 1 | (LPARAM(event.scan) << 16);
		if (keyboard_modifiers() & WIN32_HOTKEY_ALT) flags |= LPARAM(1) << 29;
		if (event.repeat || event.released) flags |= LPARAM(1) << 30;
		if (event.released) flags |= LPARAM(1) << 31;
		dispatching_captured_key = true;
		try { target->window_event(event.window, event.released ? WM_KEYUP : WM_KEYDOWN, event.key, flags); }
		catch (...) { dispatching_captured_key = false; set_keyboard_capture(false); throw; }
		dispatching_captured_key = false;
		// SDL lets Alt+Tab escape a captured fullscreen window by minimizing it.
		if (!event.released && event.key == VK_TAB && target->fullscreen &&
			(keyboard_modifiers() & WIN32_HOTKEY_ALT))
		{
			ShowWindow(event.window, SW_MINIMIZE);
			release_input_session();
		}
	}
}

void win32_application::pump()
{
	while (running.load(std::memory_order_acquire))
	{
		if (managed_host)
		{
			DWORD host_pid = 0;
			GetWindowThreadProcessId(managed_host, &host_pid);
			if (host_pid != manager_pid || WaitForSingleObject(manager_process, 0) != WAIT_TIMEOUT)
				request_shutdown();
		}
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT) request_shutdown();
			else { TranslateMessage(&msg); DispatchMessage(&msg); }
		}
		try { dispatch_captured_keys(); }
		catch (...) { record_callback_error(); }
		MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
	}
	set_keyboard_capture(false);
}

void win32_application::stop()
{
	running.store(false, std::memory_order_release);
	if (dispatcher) PostMessage(dispatcher, win32_wake_message, 0, 0);
}

bx_win32_gui_c* win32_application::window_owner(HWND window) const
{
	const auto entry = windows.find(window);
	return entry == windows.end() ? nullptr : entry->second;
}

void win32_application::request_shutdown()
{
	if (system) system->RequestShutdown();
}

void win32_application::record_callback_error() noexcept
{
	if (!callback_error)
	{
		callback_error = std::current_exception();
		request_shutdown();
	}
}

void win32_application::poll_input(unsigned current_key_scan)
{
	if (callback_error) std::rethrow_exception(callback_error);
	if (!dispatching_captured_key && current_key_scan == 512) dispatch_captured_keys();
	if (input_paused) return;
	if (!keyboard_hook && !native_altgr_release_pending)
	{
		if (!(GetKeyState(VK_LCONTROL) & 0x8000))
			native_altgr_control = native_left_control_down = native_altgr_press_pending = false;
		// AltGr may end outside our windows before a genuine Control is pressed.
		// Wait for the paired RightAlt down before using its state to retire the mask.
		else if (!native_altgr_press_pending && !(GetKeyState(VK_RMENU) & 0x8000))
			native_altgr_control = false;
	}
	release_grabbed_keys();
	media.reconcile_input();
	sync_input();
	if (media.active()) { release_input_session(); return; }
	auto* focus = window_owner(GetFocus());
	if (!focus || !win32_display_active(focus->window)) release_input_session();
	else if (capture_owner && capture_owner != focus) set_mouse_capture(focus);
	else if (capture_owner && !mouse_absolute && GetCapture() != capture_owner->window)
		set_mouse_capture(nullptr);
	reconcile_keyboard_releases(current_key_scan);
	reconcile_mouse_releases();
}

void win32_application::reconcile_keyboard_releases(unsigned current_key_scan)
{
	if (!keyboard || input_paused) return;
	for (unsigned scan = 0; scan < 512; ++scan)
	{
		const unsigned key = guest_virtual_key_by_scan[scan];
		// GetKeyState follows the message queue. Let the current key event finish normally
		// and then only repair releases missing from the guest stream.
		if (scan == current_key_scan || !guest_keys[scan] || !key ||
			guest_key_state_ambiguous[scan] || (keyboard_key_state(key) & 0x8000)) continue;
		send_key(scan, true);
		host_virtual_key_by_scan[scan] = 0;
		reconciled_key_releases[scan] = true;
	}
}

void win32_application::reconcile_mouse_releases()
{
	if (!capture_owner || input_paused) return;
	// Repair only releases; never manufacture a guest press.
	const unsigned host_buttons = win32_host_mouse_buttons();
	capture_owner->ignored_mouse_buttons &= win32_mouse_button_flags(host_buttons);
	const unsigned held = mouse_buttons & host_buttons;
	if (held == mouse_buttons) return;
	send_mouse(0, 0, 0, held);
	mouse_buttons = held;
}

void win32_application::pause_input()
{
	input_paused = true;
	set_keyboard_capture(false);
	media.close();
	for (const auto& display : displays) display->reset_absolute_mouse_position();
}

void win32_application::release_media_input()
{
	release_input_session();
	std::fill_n(swallowed_hotkey_keys, 512, true);
}

bool win32_application::media_key_event(UINT msg, WPARAM wparam, LPARAM lparam, bool synthetic_key)
{
	lparam = win32_key_flags(wparam, lparam);
	const bool released = msg == WM_KEYUP || msg == WM_SYSKEYUP;
	if (!synthetic_key && filter_native_altgr(msg, wparam, lparam)) return true;
	const unsigned scan = HIWORD(lparam) & 0x1ff;
	// A captured Print Screen or Pause/Break can open the popup before its native copy arrives.
	if (!synthetic_key && (wparam == VK_SNAPSHOT || scan == win32_pause_scan) &&
		consume_native_key((unsigned)wparam, scan, released, (DWORD)GetMessageTime())) return true;
	if (!synthetic_key && wparam == VK_SNAPSHOT && released && needs_print_screen_press())
	{
		media_key_event(msg == WM_SYSKEYUP ? WM_SYSKEYDOWN : WM_KEYDOWN,
			wparam, lparam & ~(LPARAM(3) << 30), true);
		return media_key_event(msg, wparam, lparam, true);
	}
	if (wparam == VK_SNAPSHOT || wparam == VK_CONTROL || wparam == VK_LCONTROL)
		observe_key(HIWORD(lparam) & 0x1ff, (unsigned)wparam, released, (lparam & (1L << 30)) != 0, false);
	if (input_paused || shutting_down) return true;
	if (msg == WM_KEYUP || msg == WM_SYSKEYUP)
	{
		consume_hotkey_key(scan, true, false);
		return true;
	}
	const unsigned key = win32_host_hotkey_key(wparam, scan);
	const unsigned modifiers = keyboard_modifiers();
	if (hotkey_media.matches(key, modifiers))
	{
		if (!(lparam & (1L << 30)))
		{
			release_media_input();
			media.toggle(media.parent());
		}
		return true;
	}
	suppress_hotkey_keys(scan, 0, false);
	return false;
}

void win32_application::sync_input()
{
	if (input_paused || !keyboard || !input_reset_pending) return;
	input_reset_pending = false;
	release_guest_keys();
	if (guest_mouse_captured) keyboard->mouse_motion(0, 0, 0, 0);
	guest_mouse_captured = capture_owner != nullptr;
	keyboard->set_mouse_capture(guest_mouse_captured);
	mouse_buttons = 0;
}

static u32 translate_win32_key(unsigned scan)
{
	if (scan >= 512) return BX_KEY_UNHANDLED;
	const u32 key = win32_to_bx_key[(scan >> 8) & 1][scan & 0xff];
	// Zero denotes an unmapped scan code except for the left Control key.
	return key || scan == 0x1d ? key : BX_KEY_UNHANDLED;
}

void win32_application::observe_key(unsigned scan, unsigned virtual_key, bool released, bool repeat, bool release_previous_press)
{
	if (scan >= 512) return;
	if (scan == 0x1d && (virtual_key == VK_CONTROL || virtual_key == VK_LCONTROL))
		native_left_control_down = !released;
	if (virtual_key == VK_SNAPSHOT && (released || !repeat || !reconciled_key_releases[scan]))
		print_screen_down_seen = !released;
	if (released) { host_virtual_key_by_scan[scan] = 0; return; }
	if (repeat && reconciled_key_releases[scan]) return;
	// A fresh press proves that an earlier press ended, even if no poll ran between actions
	if (release_previous_press && !repeat && guest_keys[scan]) send_key(scan, true);
	if (!repeat || !host_virtual_key_by_scan[scan])
		host_virtual_key_by_scan[scan] = win32_key_state_key(virtual_key, scan);
	const unsigned key = host_virtual_key_by_scan[scan];
	// Observe unmapped keys and host shortcuts too: their shared VK state could 
	// make a still valid for the guest held key look released
	for (unsigned other = 0; key && other < 512; ++other)
		if (other != scan && guest_keys[other] && guest_virtual_key_by_scan[other] == key)
			guest_key_state_ambiguous[other] = true;
}

void win32_application::send_key(unsigned scan, bool released, unsigned host_key, unsigned virtual_key)
{
	if (!keyboard || input_paused || scan >= 512) return;
	sync_input();
	if (released && !guest_keys[scan]) return;
	u32 key;
	if (guest_keys[scan]) key = guest_key_by_scan[scan];
	else if (keyboard_use_mapping)
	{
		const BXKeyEntry* entry = shared_keymap->findHostKey(host_key);
		if (!entry)
		{
			BX_ERROR(("host key 0x%x not mapped!", host_key));
			return;
		}
		key = entry->baseKey;
	}
	else key = translate_win32_key(scan);
	if (key == BX_KEY_UNHANDLED) return;
	if (!released && guest_keys[scan] && (key == BX_KEY_CTRL_L ||
		key == BX_KEY_CTRL_R || key == BX_KEY_ALT_L || key == BX_KEY_ALT_R ||
		key == BX_KEY_SHIFT_L || key == BX_KEY_SHIFT_R)) return;
	keyboard->gen_scancode(key | (released ? BX_KEY_RELEASED : 0));
	if (!released && !guest_keys[scan])
	{
		const unsigned state_key = win32_key_state_key(virtual_key, scan);
		guest_virtual_key_by_scan[scan] = state_key;
		guest_key_state_ambiguous[scan] = false;
		// Main/keypad Enter and NumLock-off navigation can share a VK. 
		for (unsigned other = 0; state_key && other < 512; ++other)
			if (other != scan && host_virtual_key_by_scan[other] == state_key)
				guest_key_state_ambiguous[scan] = true;
	}
	if (released) guest_virtual_key_by_scan[scan] = 0;
	// Repeats, focus-loss cleanup and releases from any output retain the
	// original guest key even if the host keyboard layout changes meanwhile.
	guest_key_by_scan[scan] = key;
	guest_keys[scan] = !released;
}

void win32_application::release_guest_keys()
{
	if (input_paused) { input_reset_pending = true; return; }
	for (unsigned i = 0; i < 512; ++i)
		if (guest_keys[i]) send_key(i, true);
}

bool win32_application::consume_hotkey_key(unsigned scan, bool released, bool repeat,
	bool allow_repeat)
{
	if (scan >= 512) return false;
	// A fresh press also recovers when the previous release went to another app.
	if (!released && !repeat)
	{
		swallowed_hotkey_keys[scan] = false;
		fullscreen_hotkey_pressed[scan] = false;
		reconciled_key_releases[scan] = false;
	}
	// Fullscreen owns the whole press, even if releasing Alt makes a scale
	// shortcut match. Keep this state across focus changes and output windows.
	if (fullscreen_hotkey_pressed[scan])
	{
		if (released) fullscreen_hotkey_pressed[scan] = false;
		return true;
	}
	// A delayed key-up or repeat must not revive a repaired press.
	if (reconciled_key_releases[scan]) return true;
	if (!swallowed_hotkey_keys[scan]) return false;
	if (!released && repeat && allow_repeat) return false;
	if (released) swallowed_hotkey_keys[scan] = false;
	return true;
}

void win32_application::suppress_fullscreen_key(unsigned scan)
{
	if (input_paused || scan >= 512) return;
	send_key(scan, true);
	fullscreen_hotkey_pressed[scan] = true;
}

void win32_application::suppress_hotkey_keys(unsigned scan, unsigned modifiers,
	bool release_modifiers)
{
	if (input_paused || scan >= 512) return;
	sync_input();
	swallowed_hotkey_keys[scan] = true;
	if (!release_modifiers) return;
	// release the guest modifiers before injecting the complete sequence. 
	// Suppression belongs to the shared keyboard so repeats and releases can arrive 
	// at a different output window.
	const struct { unsigned scan; int key; unsigned modifier; } keys[] = {
		{ 0x1d, VK_LCONTROL, WIN32_HOTKEY_CTRL }, { 0x11d, VK_RCONTROL, WIN32_HOTKEY_CTRL },
		{ 0x38, VK_LMENU, WIN32_HOTKEY_ALT }, { 0x138, VK_RMENU, WIN32_HOTKEY_ALT },
		{ 0x2a, VK_LSHIFT, WIN32_HOTKEY_SHIFT }, { 0x36, VK_RSHIFT, WIN32_HOTKEY_SHIFT },
		{ 0x15b, VK_LWIN, WIN32_HOTKEY_GUI }, { 0x15c, VK_RWIN, WIN32_HOTKEY_GUI }
	};
	for (const auto& key : keys)
	{
		if ((modifiers & key.modifier) &&
			(guest_keys[key.scan] || (keyboard_key_state(key.key) & 0x8000)))
		{
			swallowed_hotkey_keys[key.scan] = true;
			send_key(key.scan, true);
		}
	}
}

void win32_application::send_ctrl_alt_delete()
{
	if (!keyboard || input_paused) return;
	const u32 sequence[] = {
		BX_KEY_CTRL_L, BX_KEY_ALT_L, BX_KEY_DELETE,
		BX_KEY_DELETE | BX_KEY_RELEASED, BX_KEY_ALT_L | BX_KEY_RELEASED,
		BX_KEY_CTRL_L | BX_KEY_RELEASED
	};
	for (u32 key : sequence) keyboard->gen_scancode(key);
}

void win32_application::send_mouse(int dx, int dy, int dz, unsigned buttons)
{
	if (keyboard && !input_paused)
		keyboard->mouse_motion(dx, dy, dz, buttons);
}

void win32_application::send_host_mouse(double dx, double dy, int dz,
	unsigned buttons)
{
	if (!keyboard || input_paused) return;
	// The window has already converted host Y to the PS/2 positive-up axis.
	mouse_remainder_x += dx * (mouse_invert_x ? -mouse_speed : mouse_speed);
	mouse_remainder_y += dy * (mouse_invert_y ? -mouse_speed : mouse_speed);
	// Raw motion has signed 32-bit counts rather than client-pixel deltas.
	// Bound the scaled result before converting to the guest's integer API.
	const double minimum = (std::numeric_limits<int>::min)();
	const double maximum = (std::numeric_limits<int>::max)();
	mouse_remainder_x = (std::max)(minimum, (std::min)(maximum, mouse_remainder_x));
	mouse_remainder_y = (std::max)(minimum, (std::min)(maximum, mouse_remainder_y));
	const int scaled_x = (int)mouse_remainder_x;
	const int scaled_y = (int)mouse_remainder_y;
	mouse_remainder_x -= scaled_x;
	mouse_remainder_y -= scaled_y;
	if (scaled_x || scaled_y || dz || buttons != mouse_buttons)
		send_mouse(scaled_x, scaled_y, dz, buttons);
	mouse_buttons = buttons;
}

void win32_application::release_input_session()
{
	release_guest_keys();
	mouse_remainder_x = mouse_remainder_y = 0.0;
	set_mouse_capture(nullptr);
	std::fill_n(host_virtual_key_by_scan, 512, 0);
}

bool win32_application::set_raw_mouse_target(HWND target)
{
	if (raw_mouse_target == target) return true;
	// Keep legacy buttons and wheel events, and receive motion only while
	// this application is in the foreground. These APIs predate Windows 7.
	RAWINPUTDEVICE mouse = { 0x01, 0x02, target ? DWORD(RIDEV_DEVNOTIFY) : DWORD(RIDEV_REMOVE), target };
	if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse)))
	{
		printf("%%WIN32-W-RAWMOUSE: Unable to %s raw mouse input (error %lu).\n",
			target ? "capture" : "release", GetLastError());
		return false;
	}
	raw_mouse_target = target;
	return true;
}

void win32_application::discard_raw_mouse_input(HWND window)
{
	if (!window) return;
	MSG msg;
	while (PeekMessage(&msg, window, WM_INPUT, WM_INPUT, PM_REMOVE | PM_QS_INPUT))
	{
		if (msg.message == WM_QUIT) request_shutdown();
		else DefWindowProc(msg.hwnd, msg.message, msg.wParam, msg.lParam);
	}
}

void win32_application::set_mouse_capture(bx_win32_gui_c* target)
{
	if (target && (input_paused || media.active())) return;
	if (target && (!target->window || target->retired ||
		!win32_display_active(target->window))) return;
	if (capture_owner == target) return;
	const unsigned transition = ++mouse_capture_transition;
	if (!mouse_absolute)
	{
		HWND previous_target = raw_mouse_target;
		set_raw_mouse_target(nullptr);
		discard_raw_mouse_input(previous_target);
		if (target && target->window != previous_target) discard_raw_mouse_input(target->window);
		// PeekMessage can run a nested main-thread callback. A newer capture
		// request or display retirement takes precedence over this request.
		if (transition != mouse_capture_transition ||
			(target && (shutting_down || !target->window || target->retired ||
			 !win32_display_active(target->window))))
		{
			if (capture_owner && !raw_mouse_target &&
				!set_raw_mouse_target(capture_owner->window)) set_mouse_capture(nullptr);
			return;
		}
	}
	auto* previous = capture_owner;
	const bool transferring = previous && target;
	// Absolute coordinates belong to one window, while fractional motion
	// belongs to the shared guest stream. Only handoffs retain that carry.
	if (mouse_absolute && (!previous || !target))
		mouse_remainder_x = mouse_remainder_y = 0.0;
	capture_owner = nullptr;
	if (previous)
	{
		previous->reset_absolute_mouse_position();
		if (!mouse_absolute)
		{
			ClipCursor(nullptr);
			if (GetCapture() == previous->window) ReleaseCapture();
		}
		previous->update_title();
	}
	if (target)
	{
		target->reset_absolute_mouse_position();
		target->wheel_remainder = 0;
		// Continue guest-held buttons during a handoff. 
		const unsigned ignored = win32_host_mouse_buttons() &
			~(transferring ? mouse_buttons : 0);
		target->ignored_mouse_buttons = win32_mouse_button_flags(ignored);
		// Publish the new owner before SetCapture notifies the previous window.
		capture_owner = target;
		// Like SDL with relative mode disabled, absolute input leaves the
		// host pointer free to leave and reenter the client area.
		if (!mouse_absolute)
		{
			SetCapture(target->window);
			if (GetCapture() != target->window || !target->confine_mouse() ||
				!set_raw_mouse_target(target->window))
			{
				capture_owner = nullptr;
				mouse_remainder_x = mouse_remainder_y = 0.0;
				ClipCursor(nullptr);
				if (GetCapture() == target->window) ReleaseCapture();
			}
		}
		target->update_title();
	}
	if (transferring && capture_owner)
		reconcile_mouse_releases();
	else
	{
		// A release or failed handoff ends the mouse session. 
		// Send the/ release before clearing capture.
		if (guest_mouse_captured) send_mouse(0, 0, 0, 0);
		if (input_paused) input_reset_pending = true;
		mouse_buttons = 0;
	}
	if (!capture_owner) set_raw_mouse_target(nullptr);
	set_keyboard_capture(capture_owner != nullptr);
	SetCursor(capture_owner ? nullptr : LoadCursor(nullptr, IDC_ARROW));
	if (keyboard && !input_paused)
	{
		guest_mouse_captured = capture_owner != nullptr;
		keyboard->set_mouse_capture(guest_mouse_captured);
	}
}

void bx_win32_gui_c::specific_init(unsigned, unsigned)
{
	application.invoke([this] { initialize_window(); });
	new_gfx_api = true;
}

void bx_win32_gui_c::initialize_window()
{
	if (retired || application.shutting_down)
		FAILURE(Win32, "Cannot initialize a retired display");
	if (window) return;
	if (width == 0 || height == 0) dimension_update_impl(640, 480);
	embedded_host = primary ? application.manager_host() : nullptr;
	const DWORD style = embedded_host ? WS_CHILD | WS_CLIPSIBLINGS : win32_window_style;
	RECT rect = window_rect_for_client(driven_width, driven_height);
	if (embedded_host && !GetClientRect(embedded_host, &rect))
		FAILURE(Win32, "Unable to size managed display");
	HWND created = CreateWindowEx(0, win32_window_class, window_title.c_str(),
		style, embedded_host ? 0 : CW_USEDEFAULT, embedded_host ? 0 : CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top, embedded_host, nullptr, GetModuleHandle(nullptr), this);
	if (!created)
		FAILURE(Win32, "Unable to create display window");
	resize_window_to_guest();
	update_title();
	ShowWindow(window, embedded_host ? SW_SHOWNA : SW_SHOW);
	if (embedded_host)
	{
		PostMessage(embedded_host, win32_manager_display_ready, (WPARAM)window, GetCurrentProcessId());
		PostMessage(embedded_host, win32_manager_display_size, width, height);
	}
}

void bx_win32_gui_c::update_title()
{
	if (!window) return;
	// Same supported hints and ordering as SDL's build_window_titles().
	std::string title = window_title;
	const auto append_hint = [&](const win32_hotkey_binding& binding, const char* hint)
	{
		if (binding.enabled) title += " - " + binding.display + " " + hint;
	};
	if (application.capture_owner == this)
		append_hint(application.hotkey_mouse_capture, "releases mouse");
	append_hint(application.hotkey_media, "media");
	append_hint(application.hotkey_ctrl_alt_delete, "sends C+A+Del");
	append_hint(application.hotkey_reset_window, "resets window");
	SetWindowText(window, title.c_str());
}

void bx_win32_gui_c::dimension_update(unsigned x, unsigned y,
	unsigned, unsigned, unsigned)
{
	application.invoke([=] { dimension_update_impl(x, y); });
}

void bx_win32_gui_c::dimension_update_impl(unsigned x, unsigned y)
{
	if (retired) return;
	if (!x || !y || x > (unsigned)(std::numeric_limits<int>::max)() ||
		y > (unsigned)(std::numeric_limits<int>::max)() ||
		(size_t)x > (std::numeric_limits<size_t>::max)() / sizeof(u32) / y)
		FAILURE(Win32, "Invalid display dimensions");
	if (x == width && y == height) return;
	reset_absolute_mouse_position();
	pixels.assign((size_t)x * y, 0);
	width = x;
	height = y;
	host_xres = (u16)x;
	host_yres = (u16)y;
	host_bpp = 32;
	update_driven_size();
	resize_window_to_guest();
	update_mouse_geometry();
	if (embedded_host) PostMessage(embedded_host, win32_manager_display_size, width, height);
}

int bx_win32_gui_c::window_scale() const
{
	if (runtime_scale_override) return runtime_scale_override;
	return application.configured_scale;
}

void bx_win32_gui_c::update_driven_size()
{
	const unsigned scale = (unsigned)window_scale();
	const unsigned limit = (unsigned)(std::numeric_limits<int>::max)() / scale;
	if (!width || !height || width > limit || height > limit)
		FAILURE(Win32, "Scaled display dimensions are out of range");
	driven_width = (int)(width * scale);
	driven_height = (int)(height * scale);
}

RECT bx_win32_gui_c::window_rect_for_client(int client_width, int client_height) const
{
	RECT rect = { 0, 0, client_width, client_height };
	const DWORD style = window ? (DWORD)GetWindowLongPtr(window, GWL_STYLE) : win32_window_style;
	const DWORD ex_style = window ? (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE) : 0;
	if (!AdjustWindowRectEx(&rect, style, FALSE, ex_style) ||
		(long long)rect.right - rect.left <= 0 || (long long)rect.bottom - rect.top <= 0 ||
		(long long)rect.right - rect.left > (std::numeric_limits<int>::max)() ||
		(long long)rect.bottom - rect.top > (std::numeric_limits<int>::max)())
		FAILURE(Win32, "Unable to calculate display window size");
	return rect;
}

void bx_win32_gui_c::resize_window_to_guest()
{
	if (!window || retired) return;
	if (embedded_host && !fullscreen)
	{
		RECT rect = {};
		pending_guest_resize = false;
		if (GetClientRect(embedded_host, &rect))
			SetWindowPos(window, nullptr, 0, 0, rect.right, rect.bottom,
				SWP_NOZORDER | SWP_NOACTIVATE);
		return;
	}
	if (fullscreen)
	{
		// A mode change while fullscreen must survive the return to a saved
		// maximized/minimized window. A normal saved rectangle stays manual.
		if (windowed_placement.showCmd == SW_SHOWMAXIMIZED ||
			windowed_placement.showCmd == SW_SHOWMINIMIZED)
			pending_guest_resize = true;
		return;
	}
	if (IsZoomed(window) || IsIconic(window))
	{
		pending_guest_resize = true;
		return;
	}
	RECT rect = window_rect_for_client(driven_width, driven_height);
	// SetWindowPos can synchronously send WM_SIZE. Consume the request first
	// so that notification cannot recursively apply the same guest resize.
	pending_guest_resize = false;
	// Allow exact guest-driven sizes beyond Windows' interactive tracking limits.
	if (!SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left,
		rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING))
		pending_guest_resize = true;
}

void bx_win32_gui_c::reset_window_size()
{
	if (!window || retired) return;
	reset_absolute_mouse_position();
	if (fullscreen) toggle_fullscreen();
	if (IsZoomed(window) || IsIconic(window)) ShowWindow(window, SW_RESTORE);
	// Manual resizing changes only the host window. 
	// The framebuffer dimensions remain the guest-driven state.
	resize_window_to_guest();
}

void bx_win32_gui_c::adjust_window_scale(int delta)
{
	if (!window || retired) return;
	const int current = window_scale();
	const int next = (std::max)(1, (std::min)(8, current + delta));
	if (next == runtime_scale_override) return;
	runtime_scale_override = next;
	update_driven_size();
	reset_window_size();
}

void bx_win32_gui_c::update_mouse_geometry()
{
	if (application.capture_owner == this)
	{
		if (IsIconic(window)) application.set_mouse_capture(nullptr);
		else if (!application.mouse_absolute)
		{
			if (!confine_mouse()) application.set_mouse_capture(nullptr);
		}
	}
}

void bx_win32_gui_c::graphics_frame_update(const u32* frame,
	unsigned x, unsigned y)
{
	application.invoke([=]
	{
		if (!window || retired || !frame) return;
		dimension_update_impl(x, y);
		std::copy_n(frame, (size_t)x * y, pixels.data());
		InvalidateRect(window, nullptr, FALSE);
		UpdateWindow(window);
	});
}

const u32* bx_win32_gui_c::linear_frame(unsigned x, unsigned y)
{
	if ((size_t)x > filtered_pixels.max_size() / y)
		FAILURE(Win32, "Filtered display dimensions are out of range");
	filtered_pixels.resize((size_t)x * y);
	linear_columns.resize(x);
	for (unsigned column = 0; column < x; ++column)
		linear_columns[column] = win32_linear_axis(column, width, x);
	for (unsigned row = 0; row < y; ++row)
	{
		const auto sample = win32_linear_axis(row, height, y);
		const u32* top = pixels.data() + (size_t)sample.first * width;
		const u32* bottom = pixels.data() + (size_t)sample.second * width;
		u32* target = filtered_pixels.data() + (size_t)row * x;
		for (unsigned column = 0; column < x; ++column)
		{
			const auto& horizontal = linear_columns[column];
			const u32 a = win32_linear_mix(top[horizontal.first], top[horizontal.second], horizontal.fraction);
			const u32 b = win32_linear_mix(bottom[horizontal.first], bottom[horizontal.second], horizontal.fraction);
			target[column] = 0xff000000 | win32_linear_mix(a, b, sample.fraction);
		}
	}
	return filtered_pixels.data();
}

void bx_win32_gui_c::paint(HDC dc)
{
	RECT rect;
	if (!GetClientRect(window, &rect) || IsRectEmpty(&rect)) return;
	prepare_paint_buffer(dc, rect.right, rect.bottom);
	// Compose the background and image offscreen.
	FillRect(paint_dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
	if (!pixels.empty() && width && height)
	{
		// Match SDL's letterboxing if the client area differs from the mode.
		const double scale = (std::min)((double)rect.right / width,
			(double)rect.bottom / height);
		const int w = (int)(width * scale), h = (int)(height * scale);
		const bool filter = application.video_linear && w > 0 && h > 0 &&
			((unsigned)w != width || (unsigned)h != height);
		const u32* frame = filter ? linear_frame((unsigned)w, (unsigned)h) : pixels.data();
		const unsigned source_width = filter ? (unsigned)w : width;
		const unsigned source_height = filter ? (unsigned)h : height;
		BITMAPINFO info = {};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = (LONG)source_width;
		info.bmiHeader.biHeight = -(LONG)source_height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		SetStretchBltMode(paint_dc, COLORONCOLOR);
		StretchDIBits(paint_dc, (rect.right - w) / 2, (rect.bottom - h) / 2,
			w, h, 0, 0, source_width, source_height, frame, &info,
			DIB_RGB_COLORS, SRCCOPY);
	}
	BitBlt(dc, 0, 0, rect.right, rect.bottom, paint_dc, 0, 0, SRCCOPY);
}

void bx_win32_gui_c::prepare_paint_buffer(HDC reference, int x, int y)
{
	if (paint_dc && paint_width == x && paint_height == y) return;
	HDC buffer_dc = CreateCompatibleDC(reference);
	HBITMAP bitmap = buffer_dc ? CreateCompatibleBitmap(reference, x, y)
		: nullptr;
	HGDIOBJ original = bitmap ? SelectObject(buffer_dc, bitmap) : nullptr;
	if (!original || original == HGDI_ERROR)
	{
		if (bitmap) DeleteObject(bitmap);
		if (buffer_dc) DeleteDC(buffer_dc);
		FAILURE(Win32, "Unable to create display paint buffer");
	}
	// Keep the previous buffer intact until its replacement is complete.
	release_paint_buffer();
	paint_dc = buffer_dc;
	paint_bitmap = bitmap;
	paint_original_bitmap = original;
	paint_width = x;
	paint_height = y;
}

void bx_win32_gui_c::release_paint_buffer()
{
	if (paint_dc && paint_original_bitmap)
		SelectObject(paint_dc, paint_original_bitmap);
	if (paint_bitmap) DeleteObject(paint_bitmap);
	if (paint_dc) DeleteDC(paint_dc);
	paint_dc = nullptr;
	paint_bitmap = nullptr;
	paint_original_bitmap = nullptr;
	paint_width = paint_height = 0;
}

void bx_win32_gui_c::clear_screen()
{
	application.invoke([this]
	{
		if (!window || retired) return;
		std::fill(pixels.begin(), pixels.end(), 0);
		InvalidateRect(window, nullptr, FALSE);
		UpdateWindow(window);
	});
}

void bx_win32_gui_c::handle_events()
{
	application.invoke([this] { application.poll_input(); });
}

void bx_win32_gui_c::mouse_enabled_changed_specific(bool val)
{
	application.invoke([=] { application.set_mouse_capture(val ? this : nullptr); });
}

void bx_win32_gui_c::get_capabilities(u16* xres, u16* yres, u16* bpp)
{
	*xres = (u16)GetSystemMetrics(SM_CXSCREEN);
	*yres = (u16)GetSystemMetrics(SM_CYSCREEN);
	*bpp = 32;
}

bool bx_win32_gui_c::confine_mouse()
{
	RECT rect;
	if (!GetClientRect(window, &rect) || IsRectEmpty(&rect)) return false;
	MapWindowPoints(window, nullptr, (POINT*)&rect, 2);
	return ClipCursor(&rect) != 0;
}

void bx_win32_gui_c::raw_mouse_event(HRAWINPUT handle)
{
	if (application.mouse_absolute || application.input_paused || application.capture_owner != this) return;
	RAWINPUT input = {};
	UINT size = sizeof(input);
	if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != sizeof(input) ||
		input.header.dwSize != sizeof(input) || input.header.dwType != RIM_TYPEMOUSE) return;
	const RAWMOUSE& mouse = input.data.mouse;
	if (!(mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
	{
		// A source can switch modes; relative counts cannot extend its last
		// absolute position. Other devices keep their independent baselines.
		raw_mouse_positions.erase(input.header.hDevice);
		if (mouse.lLastX || mouse.lLastY)
			application.send_host_mouse((double)mouse.lLastX, -(double)mouse.lLastY,
				0, application.mouse_buttons);
		return;
	}

	// Like SDL, accept local absolute-reporting mice/tablets in relative mode.
	// Some sources use pixel coordinates (0x40), otherwise Windows normalizes
	// them to 0..65535 over the primary display or the whole virtual desktop.
	static constexpr USHORT raw_coordinates = 0x40;
	const USHORT mode = mouse.usFlags & (MOUSE_VIRTUAL_DESKTOP | raw_coordinates);
	const bool virtual_desktop = (mode & MOUSE_VIRTUAL_DESKTOP) != 0;
	const int screen_width = GetSystemMetrics(virtual_desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
	const int screen_height = GetSystemMetrics(virtual_desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
	const int screen_left = virtual_desktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0;
	const int screen_top = virtual_desktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
	if (screen_width <= 0 || screen_height <= 0 ||
		(!(mode & raw_coordinates) && (mouse.lLastX < 0 || mouse.lLastX > 65535 ||
		 mouse.lLastY < 0 || mouse.lLastY > 65535)))
	{
		raw_mouse_positions.erase(input.header.hDevice);
		return;
	}
	const long long x = (mode & raw_coordinates) ? mouse.lLastX :
		(long long)mouse.lLastX * screen_width / 65535;
	const long long y = (mode & raw_coordinates) ? mouse.lLastY :
		(long long)mouse.lLastY * screen_height / 65535;
	auto& position = raw_mouse_positions[input.header.hDevice];
	const bool continue_motion = position.valid && !(mouse.usFlags & MOUSE_ATTRIBUTES_CHANGED) &&
		position.mode == mode && position.screen_width == screen_width &&
		position.screen_height == screen_height && position.screen_left == screen_left &&
		position.screen_top == screen_top;
	const long long dx = x - position.x, dy = y - position.y;
	position = { x, y, screen_width, screen_height, screen_left, screen_top, mode, true };
	// Seed a new source/coordinate space, and ignore tablet lift/drop jumps.
	// Retain the new position after a rejected jump so normal motion resumes.
	if (!continue_motion || dx < -32 || dx > 32 || dy < -32 || dy > 32) return;
	if (dx || dy)
		application.send_host_mouse((double)dx, -(double)dy, 0, application.mouse_buttons);
}

void bx_win32_gui_c::mouse_event(int x, int y, int z, unsigned flags, bool motion)
{
	if (application.capture_owner != this || application.input_paused) return;
	ignored_mouse_buttons &= flags;
	flags &= ~ignored_mouse_buttons;
	unsigned buttons = ((flags & MK_LBUTTON) ? 1 : 0) |
		((flags & MK_RBUTTON) ? 2 : 0) | ((flags & MK_MBUTTON) ? 4 : 0);
	int dx = 0, dy = 0;
	if (application.mouse_absolute && motion)
	{
		if (mouse_position_valid)
		{
			dx = x - mouse_last_x;
			dy = mouse_last_y - y;
		}
		// The first absolute motion seeds the window-local baseline. Button
		// and wheel coordinates must never move it or generate motion.
		mouse_last_x = x;
		mouse_last_y = y;
		mouse_position_valid = true;
	}
	application.send_host_mouse(dx, dy, z, buttons);
}

void win32_application::claim_fullscreen_monitor(bx_win32_gui_c* target)
{
	if (fullscreen_handoff || !target || !target->window || target->retired ||
		!target->fullscreen || target->fullscreen_transition || IsIconic(target->window)) return;
	const HMONITOR monitor = MonitorFromWindow(target->window, MONITOR_DEFAULTTONEAREST);
	struct handoff_guard
	{
		bool& active;
		explicit handoff_guard(bool& value) : active(value) { active = true; }
		~handoff_guard() { active = false; }
	} guard(fullscreen_handoff);
	// Like SDL, one visible fullscreen output owns each host monitor. Keep
	// the other output's fullscreen state so restoring it can claim it back.
	for (const auto& display : displays)
		if (display.get() != target && display->window && !display->retired &&
			display->fullscreen && !display->fullscreen_transition && !IsIconic(display->window) &&
			MonitorFromWindow(display->window, MONITOR_DEFAULTTONEAREST) == monitor)
			ShowWindow(display->window, SW_MINIMIZE);
}

void win32_application::show_secondary_displays()
{
	for (const auto& display : displays)
		if (!display->primary && display->window && !display->retired)
			ShowWindow(display->window, IsIconic(display->window) ? SW_RESTORE : SW_SHOW);
}

void bx_win32_gui_c::toggle_fullscreen()
{
	if (!window || retired) return;
	if (embedded_host) application.release_input_session();
	const LONG_PTR style = GetWindowLongPtr(window, GWL_STYLE);
	if (!fullscreen)
	{
		MONITORINFO monitor = { sizeof(MONITORINFO) };
		if (!GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
		if (!GetWindowPlacement(window, &windowed_placement)) return;
		fullscreen_transition = true;
		fullscreen = true;
		SetWindowLongPtr(window, GWL_STYLE,
			(style & ~(win32_window_style | WS_MAXIMIZE | WS_MINIMIZE | WS_CHILD)) | WS_POPUP);
		if (embedded_host)
		{
			SetLastError(0);
			if (!SetParent(window, nullptr) && GetLastError())
			{
				SetWindowLongPtr(window, GWL_STYLE, style);
				fullscreen = fullscreen_transition = false;
				return;
			}
		}
		SetWindowPos(window, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
			monitor.rcMonitor.right - monitor.rcMonitor.left,
			monitor.rcMonitor.bottom - monitor.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOZORDER);
		fullscreen_transition = false;
		application.claim_fullscreen_monitor(this);
		if (embedded_host) { SetForegroundWindow(window); SetFocus(window); }
	}
	else
	{
		// Keep intermediate WM_SIZE notifications from consuming a deferred
		// guest resize before the saved window placement has been restored.
		fullscreen_transition = true;
		if (embedded_host)
		{
			if (!IsWindow(embedded_host))
			{
				fullscreen_transition = false;
				application.request_shutdown();
				return;
			}
			SetWindowLongPtr(window, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE);
			SetLastError(0);
			if (!SetParent(window, embedded_host) && GetLastError())
			{
				SetWindowLongPtr(window, GWL_STYLE, style);
				fullscreen_transition = false;
				return;
			}
			fullscreen = fullscreen_transition = false;
			resize_window_to_guest();
			SetWindowPos(window, nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
			SetForegroundWindow(GetAncestor(embedded_host, GA_ROOT));
			SetFocus(window);
			return;
		}
		SetWindowLongPtr(window, GWL_STYLE, (style & ~WS_POPUP) | win32_window_style);
		SetWindowPlacement(window, &windowed_placement);
		SetWindowPos(window, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
		fullscreen = false;
		fullscreen_transition = false;
		if (pending_guest_resize) resize_window_to_guest();
	}
}

LRESULT bx_win32_gui_c::window_event(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam, bool synthetic_key)
{
	// Focus the child before filtering input.
	if (embedded_host && msg == WM_LBUTTONDOWN && IsWindowVisible(hwnd)) SetFocus(hwnd);
	const bool key_message = msg == WM_KEYDOWN || msg == WM_KEYUP ||
		msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
	if (key_message) lparam = win32_key_flags(wparam, lparam);
	const bool input_message = key_message || msg == WM_INPUT ||
		(msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
	if (key_message && !synthetic_key && !application.dispatching_captured_key &&
		application.filter_native_altgr(msg, wparam, lparam)) return 0;
	if (key_message && !synthetic_key && !application.dispatching_captured_key &&
		application.consume_native_key((unsigned)wparam, HIWORD(lparam) & 0x1ff,
			msg == WM_KEYUP || msg == WM_SYSKEYUP, (DWORD)GetMessageTime())) return 0;
	if (!synthetic_key && wparam == VK_SNAPSHOT && (msg == WM_KEYUP || msg == WM_SYSKEYUP) &&
		application.needs_print_screen_press() && !application.media.active() && application.window_owner(GetFocus()))
	{
		// SDL repairs release-only Print Screen before mapping or shortcuts.
		// Run the complete pair so normal barriers and press bookkeeping apply.
		window_event(hwnd, msg == WM_SYSKEYUP ? WM_SYSKEYDOWN : WM_KEYDOWN,
			wparam, lparam & ~(LPARAM(3) << 30), true);
		return window_event(hwnd, msg, wparam, lparam, true);
	}
	if (input_message)
	{
		// Keep native alias bookkeeping through a pause; send_key() still
		// blocks all guest changes until input resumes.
		if (key_message)
			application.observe_key(HIWORD(lparam) & 0x1ff, (unsigned)wparam,
				msg == WM_KEYUP || msg == WM_SYSKEYUP, (lparam & (1L << 30)) != 0);
		if (application.input_paused) return 0;
		application.poll_input(key_message ? HIWORD(lparam) & 0x1ff : 512);
		if (!application.media.active() && !application.window_owner(GetFocus())) return 0;
	}
	if (key_message)
	{
		const unsigned scan = HIWORD(lparam) & 0x1ff;
		const unsigned key = win32_host_hotkey_key(wparam, scan);
		const unsigned modifiers = application.keyboard_modifiers();
		const bool scale_repeat = application.scale_change_enable &&
			(application.hotkey_scale_up.matches(key, modifiers) ||
			 application.hotkey_scale_down.matches(key, modifiers));
		if (application.consume_hotkey_key(scan, msg == WM_KEYUP || msg == WM_SYSKEYUP,
			(lparam & (1L << 30)) != 0, scale_repeat))
			return 0;
		if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && application.hotkey_media.matches(key, modifiers))
		{
			if (!(lparam & (1L << 30)))
			{
				application.release_media_input();
				application.media.toggle(hwnd);
			}
			return 0;
		}
	}
	if (input_message && application.media.filter_message(msg, wparam, lparam)) return 0;
	switch (msg)
	{
	case win32_manager_command:
		if (!application.manager_host()) return 0;
		switch (wparam)
		{
		case win32_manager_shutdown: application.request_shutdown(); break;
		case win32_manager_capture:
			SetFocus(hwnd);
			application.set_mouse_capture(this);
			break;
		case win32_manager_release: application.release_input_session(); break;
		case win32_manager_ctrl_alt_delete:
			application.release_input_session();
			application.send_ctrl_alt_delete();
			break;
		case win32_manager_media:
			application.release_media_input();
			application.media.toggle(hwnd);
			break;
		case win32_manager_fullscreen: toggle_fullscreen(); break;
		case win32_manager_reset_size: reset_window_size(); break;
		case win32_manager_show_displays: application.show_secondary_displays(); break;
		}
		return 0;
	case WM_SHOWWINDOW:
		if (!wparam && application.capture_owner == this) application.release_input_session();
		break;
	case WM_INPUT:
		if (GET_RAWINPUT_CODE_WPARAM(wparam) == RIM_INPUT)
			raw_mouse_event((HRAWINPUT)lparam);
		return 0;
	case WM_INPUT_DEVICE_CHANGE:
		raw_mouse_positions.erase((HANDLE)lparam);
		return 0;
	case WM_CLOSE:
		if (application.manager_host() && !primary)
		{
			application.release_input_session();
			ShowWindow(hwnd, SW_HIDE);
		}
		else application.request_shutdown();
		return 0;
	case WM_SYSCOMMAND:
		// SetThreadExecutionState handles display power; screen savers also
		// require suppressing their window command, as in SDL's Windows driver.
		if ((wparam & 0xfff0) == SC_SCREENSAVE || (wparam & 0xfff0) == SC_MONITORPOWER)
			return 0;
		break;
	case WM_SETFOCUS:
		reset_absolute_mouse_position();
		if (application.capture_owner && application.capture_owner != this)
			application.set_mouse_capture(this);
		return 0;
	case WM_KILLFOCUS:
		reset_absolute_mouse_position();
		if (!application.window_owner((HWND)wparam)) application.release_input_session();
		return 0;
	case WM_ACTIVATEAPP:
		if (!wparam) application.release_input_session();
		return 0;
	case WM_CANCELMODE:
		application.release_input_session();
		return 0;
	case WM_CAPTURECHANGED:
		if (!application.mouse_absolute && application.capture_owner == this && (HWND)lparam != hwnd)
		{
			// Focus can still be changing between our windows. Reconcile after
			// that transition; never reacquire capture inside this notification.
			ClipCursor(nullptr);
			SetCursor(LoadCursor(nullptr, IDC_ARROW));
			PostMessage(hwnd, win32_reconcile_message, 0, 0);
		}
		return 0;
	case win32_reconcile_message:
		application.poll_input();
		return 0;
	case WM_DISPLAYCHANGE:
		reset_absolute_mouse_position();
		application.claim_fullscreen_monitor(this);
		update_mouse_geometry();
		return 0;
	case WM_MOVE:
		reset_absolute_mouse_position();
		application.claim_fullscreen_monitor(this);
		update_mouse_geometry();
		return 0;
	case WM_SIZE:
		reset_absolute_mouse_position();
		if (wparam != SIZE_MINIMIZED) application.claim_fullscreen_monitor(this);
		// SIZE_RESTORED is also sent during manual resizing. Only apply a
		// guest size that was deferred while minimized or maximized.
		if (wparam == SIZE_RESTORED && pending_guest_resize)
			resize_window_to_guest();
		update_mouse_geometry();
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_SETCURSOR:
		if (application.capture_owner == this && LOWORD(lparam) == HTCLIENT)
		{
			SetCursor(nullptr);
			return TRUE;
		}
		break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		try { paint(dc); }
		catch (...) { EndPaint(hwnd, &ps); throw; }
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_MOUSEMOVE:
		if (application.mouse_absolute && !mouse_tracking)
		{
			TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
			mouse_tracking = TrackMouseEvent(&tracking) != 0;
		}
		mouse_event(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0,
			GET_KEYSTATE_WPARAM(wparam), true);
		return 0;
	case WM_MOUSELEAVE:
		mouse_tracking = false;
		reset_absolute_mouse_position();
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
		mouse_event(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0,
			GET_KEYSTATE_WPARAM(wparam));
		return 0;
	case WM_LBUTTONDOWN:
		if (application.capture_owner != this) application.set_mouse_capture(this);
		else mouse_event(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0,
			GET_KEYSTATE_WPARAM(wparam));
		return 0;
	case WM_MOUSEWHEEL:
		if (application.capture_owner == this)
		{
			wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
			int steps = wheel_remainder / WHEEL_DELTA;
			wheel_remainder %= WHEEL_DELTA;
			POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
			ScreenToClient(hwnd, &pt);
			mouse_event(pt.x, pt.y, steps, GET_KEYSTATE_WPARAM(wparam));
		}
		return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		const unsigned scan = HIWORD(lparam) & 0x1ff;
		const unsigned key = win32_host_hotkey_key(wparam, scan);
		const unsigned modifiers = application.keyboard_modifiers();
		if (application.hotkey_ctrl_alt_delete.matches(key, modifiers))
		{
			if (!(lparam & (1L << 30)))
			{
				application.suppress_hotkey_keys(scan, modifiers);
				application.send_ctrl_alt_delete();
			}
			return 0;
		}
		if (application.hotkey_reset_window.matches(key, modifiers))
		{
			if (!(lparam & (1L << 30)))
			{
				application.suppress_hotkey_keys(scan, modifiers, false);
				reset_window_size();
			}
			return 0;
		}
		if (application.scale_change_enable && application.hotkey_scale_up.matches(key, modifiers))
		{
			if (!(lparam & (1L << 30))) application.suppress_hotkey_keys(scan, modifiers, false);
			adjust_window_scale(+1);
			return 0;
		}
		if (application.scale_change_enable && application.hotkey_scale_down.matches(key, modifiers))
		{
			if (!(lparam & (1L << 30))) application.suppress_hotkey_keys(scan, modifiers, false);
			adjust_window_scale(-1);
			return 0;
		}
		if (application.hotkey_mouse_capture.matches(key, modifiers))
		{
			if (!(lparam & (1L << 30)))
			{
				application.suppress_hotkey_keys(scan, modifiers);
				application.set_mouse_capture(application.capture_owner ? nullptr : this);
			}
			return 0;
		}
		if (wparam == VK_RETURN && (HIWORD(lparam) & KF_ALTDOWN))
		{
			if (!(lparam & (1L << 30)))
			{
				application.suppress_fullscreen_key(scan);
				toggle_fullscreen();
			}
			return 0;
		}
		application.send_key(scan, false, win32_host_mapping_key(wparam, scan), (unsigned)wparam);
		return 0;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP:
		application.send_key((HIWORD(lparam) & 0x1ff), true);
		return 0;
	case WM_CHAR:
	case WM_SYSCHAR:
	case WM_DEADCHAR:
	case WM_SYSDEADCHAR:
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK win32_window_proc(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam)
{
	auto* owner = reinterpret_cast<bx_win32_gui_c*>(
		GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (msg == WM_NCCREATE)
	{
		owner = static_cast<bx_win32_gui_c*>(
			reinterpret_cast<CREATESTRUCT*>(lparam)->lpCreateParams);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)owner);
	}
	if (!owner) return DefWindowProc(hwnd, msg, wparam, lparam);
	try
	{
		if (msg == WM_NCCREATE)
		{
			owner->window = hwnd;
			owner->application.windows.emplace(hwnd, owner);
			return DefWindowProc(hwnd, msg, wparam, lparam);
		}
		if (msg == WM_NCDESTROY)
		{
			owner->application.windows.erase(hwnd);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
			owner->window = nullptr;
			return DefWindowProc(hwnd, msg, wparam, lparam);
		}
		const LRESULT result = owner->window_event(hwnd, msg, wparam, lparam);
		if (msg == WM_INPUT) DefWindowProc(hwnd, msg, wparam, lparam);
		return result;
	}
	catch (...)
	{
		// Never unwind through a native callback. A display worker rethrows the
		// first error through handle_events(), and main keeps serving shutdown.
		owner->application.record_callback_error();
		if (msg == WM_INPUT) DefWindowProc(hwnd, msg, wparam, lparam);
		return msg == WM_NCCREATE ? FALSE : 0;
	}
}

void bx_win32_gui_c::exit_impl()
{
	if (retired) return;
	retired = true;
	if (application.capture_owner == this) application.set_mouse_capture(nullptr);
	if (window)
	{
		if (application.media.parent() == window) application.media.close();
		if (GetFocus() == window) application.release_input_session();
		DestroyWindow(window);
	}
	release_paint_buffer();
	pixels.clear();
	std::vector<u32>().swap(filtered_pixels);
	std::vector<win32_linear_sample>().swap(linear_columns);
}

void bx_win32_gui_c::exit()
{
	application.invoke([this] { exit_impl(); });
}

void win32_application::exit_displays()
{
	shutting_down = true;
	if (previous_execution_state)
	{
		SetThreadExecutionState(previous_execution_state | ES_CONTINUOUS);
		previous_execution_state = 0;
	}
	set_keyboard_capture(false);
	media.shutdown();
	for (const auto& display : displays) display->exit_impl();
	set_raw_mouse_target(nullptr);
	if (dispatcher) { DestroyWindow(dispatcher); dispatcher = nullptr; }
	if (manager_process) { CloseHandle(manager_process); manager_process = nullptr; }
	managed_host = nullptr;
}

void bx_win32_gui_c::exit_application()
{
	application.invoke([this] { application.exit_displays(); });
}

#endif // _WIN32
