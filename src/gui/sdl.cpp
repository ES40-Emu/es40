/* ES40 emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2025-2026 by gdwnldsKSC
 * Copyright (C) 2026 by WNT50
 * Copyright (C) 2026 by Cacodemon345
 * Copyright (C) 2026 by Antoni Sawicki
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
 */

 /**
  * \file
  * Contains the code for the bx_sdl_gui_c class used for interfacing with
  * SDL.
  *
  * $Id$
  *
  * X-1.18       Martin Borgman                                  10-APR-2008
  *	    Handle SDL support on OS X through OS_X/SDLMain.m.
  *
  * X-1.15       Camiel Vanderhoeven                             29-FEB-2008
  *      Comments
  *
  * X-1.14       Camiel Vanderhoeven                             12-FEB-2008
  *      Moved keyboard code into it's own class (CKeyboard)
  *
  * X-1.13       Camiel Vanderhoeven                             22-JAN-2008
  *      Minor cleanups.
  *
  * X-1.12       Fang Zhe                                        05-JAN-2008
  *      Last patch was applied incompletely.
  *
  * X-1.11       Fang Zhe                                        04-JAN-2008
  *      Improved compatibility with Apple OS X; keyboard works now.
  *
  * X-1.10       Fang Zhe                                        03-JAN-2008
  *      Compatibility with Apple OS X.
  *
  * X-1.9        Camiel Vanderhoeven                             02-JAN-2008
  *      Comments.
  *
  * X-1.4        Camiel Vanderhoeven                             10-DEC-2007
  *      Use Configurator.
  *
  * X-1.3        Camiel Vanderhoeven                             7-DEC-2007
  *      Made keyboard messages conditional.
  *
  * X-1.2        Camiel Vanderhoeven                             7-DEC-2007
  *      Code cleanup.
  *
  * X-1.1        Camiel Vanderhoeven                             6-DEC-2007
  *      Initial version for ES40 emulator.
  *
  **/
#include "../StdAfx.h"

#if defined(HAVE_SDL)
#include "gui.h"
#include "keymap.h"
#include "sdl_media.h"
#include "../graphics/VGA.h"
#include "system/System.h"

  //#include "chipset/AliM1543C.h"
#include "io/Keyboard.h"
#include "config/Configurator.h"

#define _MULTI_THREAD

// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE

#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <cctype>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>

#include "sdl_fonts.h"

enum
{
	SDL_HOTKEY_MOD_CTRL = 1u << 0,
	SDL_HOTKEY_MOD_ALT = 1u << 1,
	SDL_HOTKEY_MOD_SHIFT = 1u << 2,
	SDL_HOTKEY_MOD_GUI = 1u << 3,
	// AltGr/Mode and Level5 are deliberately not configurable today, but
	// they must participate in exact matching so they cannot accidentally
	// satisfy a Ctrl+Alt binding.
	SDL_HOTKEY_MOD_UNSUPPORTED = 1u << 31
};

struct sdl_hotkey_binding
{
	const char* config_name = NULL;
	SDL_Keycode key = SDLK_UNKNOWN;
	unsigned modifiers = 0;
	bool enabled = false;
	std::string display;

	bool matches(const SDL_KeyboardEvent& event) const;
};

static unsigned sdl_hotkey_modifiers(SDL_Keymod modifiers);
static sdl_hotkey_binding parse_sdl_hotkey(CConfigurator* cfg,
	const char* config_name, const char* default_value);

class bx_sdl_gui_c;
class sdl_application;

// Application event handling has no display receiver.
// Targets are borrowed for each main-thread call only.
class sdl_event_dispatcher
{
public:
	static void poll();
	static void dispatch(const SDL_Event& event);
private:
	friend class bx_sdl_gui_c;
	static void defer_event(const SDL_Event& event);
	static void reconcile_input_focus();
	static void reconcile_input_releases();
	static void release_input_session();
	static void suppress_hotkey_releases(const SDL_KeyboardEvent& event,
		const sdl_hotkey_binding& binding, bool release_guest_modifiers);
	static void release_guest_key(SDL_Scancode scancode);
	static void release_all_guest_keys();
	static void send_guest_ctrl_alt_delete();
	static void clear_hotkey_release_state();
	static void reconcile_hotkey_release_state();
	static void send_guest_key(u32 key);
	static void send_guest_mouse(int dx, int dy, int dz, unsigned buttons);
	static void notify_guest_capture(bool captured);
	// Enabling requires a live target; release uses the recorded capture owner.
	static void set_mouse_capture(bx_sdl_gui_c* target, bool val);
	static void mouse_enabled_changed(bx_sdl_gui_c* target, bool val);
	static void mouse_enabled_changed_impl(bx_sdl_gui_c* target, bool val);
};

/**
 * \brief GUI implementation using SDL3.
 **/
class bx_sdl_gui_c : public bx_gui_c
{
public:
	explicit bx_sdl_gui_c(sdl_application& application);
	~bx_sdl_gui_c() override;
	bx_sdl_gui_c(const bx_sdl_gui_c&) = delete;
	bx_sdl_gui_c& operator=(const bx_sdl_gui_c&) = delete;
	virtual void    specific_init(unsigned x_tilesize, unsigned y_tilesize) override;
	virtual void    text_update(u8* old_text, u8* new_text, unsigned long cursor_x, unsigned long cursor_y, bx_vga_tminfo_t tm_info, unsigned rows) override {}
	virtual void    graphics_tile_update(u8* snapshot, unsigned x, unsigned y) override;
	virtual void    handle_events(void) override;
	virtual void    flush(void) override;
	virtual void    clear_screen(void) override;
	virtual bool    palette_change(unsigned index, unsigned red, unsigned green, unsigned blue) override;
	virtual void    dimension_update(unsigned x, unsigned y, unsigned fheight = 0, unsigned fwidth = 0, unsigned bpp = 8) override;
	virtual void    mouse_enabled_changed_specific(bool val) override;
	virtual void    exit(void) override;
	void           exit_application() override;
	bx_gui_c&      display_for_output(const std::string& device_path,
		unsigned output_id) override;
	const bx_gui_c* find_display_for_output(const std::string& device_path,
		unsigned output_id) const override;
	virtual			bx_svga_tileinfo_t* graphics_tile_info(bx_svga_tileinfo_t* info) override;
	virtual			u8* graphics_tile_get(unsigned x, unsigned y, unsigned* w, unsigned* h) override;
	virtual void    graphics_tile_update_in_place(unsigned x, unsigned y, unsigned w, unsigned h) override;
	void			graphics_frame_update(const u32* pixels, unsigned w, unsigned h) override;
	virtual bool    requires_main_thread() override;
	virtual void    main_thread_init() override;
	virtual void    main_thread_pump() override;
	virtual void    main_thread_stop() override;
	void           pause_guest_input() override;
	void           resume_guest_input() override;
	void           detach_guest_input() override;
private:
	friend class sdl_event_dispatcher;
	sdl_application& application;
	CConfigurator* myCfg;
	// Host presentation resources and geometry belong to this display.
	SDL_Window*    sdl_window = NULL;
	SDL_Renderer*  sdl_renderer = NULL;
	SDL_Texture*   sdl_texture = NULL;
	SDL_WindowID   registered_window_id = 0;
	unsigned       res_x = 0, res_y = 0;
	unsigned       half_res_x = 0, half_res_y = 0;
	int            last_driven_w = 0, last_driven_h = 0;
	bool           pending_guest_resize = false;
	bool           fullscreen_requested = false;
	bool           fullscreen_transition_pending = false;
	bool           fullscreen_windowed_deferred = false;
	int            runtime_scale_override = 0;  // 0 = use the configured/display scale
	unsigned int   vid_scale = 0;
	bool           vid_linear = true;
	bool           vid_scale_change_enable = false;
	double         mouse_speed = 1.0;
	bool           mouse_invert_x = false;
	bool           mouse_invert_y = false;
	bool           mouse_absolute = false;
	bool           mouse_position_valid = false;
	double         mouse_last_x = 0.0;
	double         mouse_last_y = 0.0;
	sdl_hotkey_binding hotkey_mouse_capture;
	sdl_hotkey_binding hotkey_media;
	sdl_hotkey_binding hotkey_ctrl_alt_delete;
	sdl_hotkey_binding hotkey_reset_window;
	sdl_hotkey_binding hotkey_scale_up;
	sdl_hotkey_binding hotkey_scale_down;
	std::string    window_title;
	std::string    window_title_grabbed;
	// Bodies of the public entry points above; always executed on the thread
	// that owns the SDL window (see on_main_thread).
	void           specific_init_impl(unsigned x_tilesize, unsigned y_tilesize);
	void           handle_keyboard_event_impl(const SDL_Event& event);
	void           handle_focus_event_impl(const SDL_Event& event);
	void           handle_mouse_event_impl(const SDL_Event& event);
	void           handle_display_event_impl(const SDL_Event& event);
	void           redraw_impl();
	void           clear_screen_impl();
	void           dimension_update_impl(unsigned x, unsigned y, unsigned fheight,
		unsigned fwidth, unsigned bpp);
	void           graphics_frame_update_impl(const u32* pixels, unsigned w, unsigned h);
	void           set_mouse_capture_impl(bool val);
	void           set_window_mouse_capture_impl(bool val);
	void           exit_impl();
	void           register_window(SDL_Window* window);
	void           unregister_window() noexcept;
	// Main-thread lookup; the result borrows the display's existing lifetime.
	static bx_sdl_gui_c* find_window_owner(SDL_WindowID window_id);
	bool           set_fullscreen(bool enabled);
	bool           restore_window_for_resize();
	void           resize_window_to_guest();
	void           reset_window_size();
	void           adjust_window_scale(int delta);
	void           load_hotkeys();
	void           build_window_titles();
	void           reset_absolute_mouse_position();
	void           reset_absolute_mouse_motion();
};

// Owns host displays only. Native resources are ended explicitly on main().
class sdl_application
{
public:
	explicit sdl_application(CConfigurator* cfg);
	~sdl_application();
	bx_sdl_gui_c& create_display();
	bx_sdl_gui_c& display_for_output(const std::string& device_path, unsigned output_id);
	const bx_sdl_gui_c* find_display_for_output(const std::string& device_path,
		unsigned output_id) const;
	std::string display_title(const bx_sdl_gui_c* display) const;
	void initialize_keymap();
	void exit_displays();
	CConfigurator* configuration() const { return cfg; }
	bx_keymap_c* keymap() const { return shared_keymap.get(); }
private:
	CConfigurator* cfg;
	std::unique_ptr<bx_keymap_c> shared_keymap;
	// Destroy display objects before their shared keymap.
	std::vector<std::unique_ptr<bx_sdl_gui_c>> displays;
	std::map<std::pair<std::string, unsigned>, bx_sdl_gui_c*> display_outputs;
	bool keymap_initialized = false;
	bool shutdown_started = false;
};

// Stop direct callers and retire a live display through synchronous exit() on
// the SDL main thread before deleting it. 
static std::mutex sdl_window_owners_mutex;
static std::map<SDL_WindowID, bx_sdl_gui_c*> sdl_window_owners;

static unsigned     prev_cursor_x = 0;
static unsigned     prev_cursor_y = 0;
static u32          convertStringToSDLKey(const char* string);

/// Set once main() has taken ownership of SDL and is driving main_thread_pump()
/// on our behalf, so on_main_thread() must bounce SDL calls across to it.
static std::atomic<bool> sdl_main_thread_owns_sdl(false);

// Main-thread-only attachment, outside guest state.
static CKeyboard* sdl_guest_keyboard = NULL;
static MouseInput* sdl_guest_mouse = NULL;
static bool sdl_guest_input_paused = false;
static bool sdl_poll_in_progress = false;
// At most one event can have been removed when SDL_PollEvent runs a pause
// callback. Keep it ahead of the remaining queue until polling resumes.
static SDL_Event sdl_deferred_event = {};
static bool sdl_has_deferred_event = false;

/// How long main_thread_pump() blocks waiting for work before looping. Only
/// caps how quickly it notices main_thread_stop(); callbacks wake it at once.
static const int sdl_pump_idle_ms = 10;

/**
 * Run fn on the thread that called main().
 *
 * SDL wants the video subsystem, the window and the event pump all touched
 * from that one thread. The emulator drives the GUI from the VGA card's
 * thread instead, so every SDL call made from there is bounced across with
 * SDL_RunOnMainThread(). Exceptions (FAILURE_*) must not escape into SDL's
 * C event loop, so they are caught and rethrown on the calling thread. The
 * inline path only covers calls made before main() takes ownership and
 * calls that already run on the main thread (e.g. from within a bounced
 * callback).
 **/
template <typename F>
static void on_main_thread(F fn)
{
	if (!sdl_main_thread_owns_sdl.load(std::memory_order_acquire) || SDL_IsMainThread())
	{
		fn();
		return;
	}

	struct SCall
	{
		F* fn;
		std::exception_ptr error;
	} call = { &fn, nullptr };

	if (!SDL_RunOnMainThread([](void* p)
		{
			SCall* c = static_cast<SCall*>(p);
			try { (*c->fn)(); }
			catch (...) { c->error = std::current_exception(); }
		}, &call, true))
	{
		FAILURE_1(SDL, "Unable to reach the SDL main thread: %s", SDL_GetError());
	}

	if (call.error)
		std::rethrow_exception(call.error);
}

bool bx_sdl_gui_c::requires_main_thread()
{
	// SDL wants the video subsystem, windows and event pump driven from the
	// thread that called main() on every platform; macOS merely enforces it.
	// Claim main() unconditionally so all platforms share one threading model.
	return true;
}

void bx_sdl_gui_c::main_thread_init()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
		FAILURE_1(SDL, "Unable to initialize SDL3 video subsystem: %s", SDL_GetError());

	application.initialize_keymap();
	sdl_guest_keyboard = theKeyboard;
	sdl_guest_mouse = theSystem ? theSystem->mouse_input() : NULL;
	sdl_main_thread_owns_sdl.store(true, std::memory_order_release);
}

void bx_sdl_gui_c::pause_guest_input()
{
	on_main_thread([] { sdl_guest_input_paused = true; });
}

void bx_sdl_gui_c::resume_guest_input()
{
	on_main_thread([] { sdl_guest_input_paused = false; });
}

void bx_sdl_gui_c::detach_guest_input()
{
	// Do not send releases or alter guest capture during teardown.
	on_main_thread([] { sdl_guest_keyboard = NULL; sdl_guest_mouse = NULL; });
}

void bx_sdl_gui_c::main_thread_pump()
{
	// Collect OS events into SDL's queue and dispatch whatever the GUI thread
	// posted with on_main_thread(). handle_events() drains the queue itself,
	// so the wait is passed NULL and leaves the events where they are.
	//
	// SDL_PumpEvents() is what runs the queued on_main_thread() callbacks, so
	// it has to happen every time round: SDL_WaitEventTimeout() returns early
	// without pumping while the queue is non-empty, and waiting on that alone
	// would hang any thread blocked in on_main_thread(). With an empty queue
	// it sleeps until a posted callback or an OS event wakes it, which keeps
	// the loop off the CPU without adding latency to the GUI thread.
	while (sdl_main_thread_owns_sdl.load(std::memory_order_acquire))
	{
		SDL_PumpEvents();
		if (SDL_WaitEventTimeout(NULL, sdl_pump_idle_ms))
			SDL_Delay(1);  // queue not drained yet; let the GUI thread catch up
	}
}

void bx_sdl_gui_c::main_thread_stop()
{
	sdl_main_thread_owns_sdl.store(false, std::memory_order_release);
}

static const int    runtime_scale_min = 1;
static const int    runtime_scale_max = 8;
u8                  old_mousebuttons = 0, new_mousebuttons = 0;
int                 old_mousex = 0, new_mousex = 0;
int                 old_mousey = 0, new_mousey = 0;

// All displays feed one guest mouse. Capture selects the host input source,
// not the guest display containing its cursor. Buttons and fractional motion
// belong to this shared input session; window-coordinate baselines stay local.
struct sdl_mouse_input_state
{
	bool captured = false;
	SDL_WindowID capture_window_id = 0;
	int buttons = 0;
	// Retain motion left over after scaling so slow movement is not dropped.
	double remainder_x = 0.0;
	double remainder_y = 0.0;
};
static sdl_mouse_input_state sdl_mouse_input;

// All displays feed one guest keyboard.
// Initializing a display must not reset them.
// Bookkeeping for everything/display.
struct sdl_keyboard_input_state
{
	u32 guest_key_by_scancode[SDL_SCANCODE_COUNT] = {};
	bool guest_key_pressed[SDL_SCANCODE_COUNT] = {};
	bool swallowed_hotkey_releases[SDL_SCANCODE_COUNT] = {};
	bool fullscreen_hotkey_pressed[SDL_SCANCODE_COUNT] = {};
	bool reconciled_key_releases[SDL_SCANCODE_COUNT] = {};
};
static sdl_keyboard_input_state sdl_keyboard_input;

// Defined *after* the borrowed window registry and shared input state so owned
// displays are destroyed first.
static std::unique_ptr<sdl_application> sdl_app;

void bx_sdl_gui_c::reset_absolute_mouse_position()
{
	if (!mouse_absolute)
		return;
	// Window geometry can invalidate local coordinates without ending mouse
	// stream or discarding other display motion
	mouse_position_valid = false;
}

void bx_sdl_gui_c::reset_absolute_mouse_motion()
{
	if (!mouse_absolute)
		return;
	// Preserve the existing absolute-mode reset at focus/capture boundaries.
	reset_absolute_mouse_position();
	sdl_mouse_input.remainder_x = 0.0;
	sdl_mouse_input.remainder_y = 0.0;
}

static std::string trim_hotkey_text(const std::string& value)
{
	size_t first = 0;
	while (first < value.size() &&
		std::isspace((unsigned char)value[first]))
		first++;

	size_t last = value.size();
	while (last > first && std::isspace((unsigned char)value[last - 1]))
		last--;

	return value.substr(first, last - first);
}

static bool hotkey_text_equals(const std::string& lhs, const char* rhs)
{
	if (!rhs || lhs.size() != strlen(rhs))
		return false;

	for (size_t i = 0; i < lhs.size(); i++)
	{
		if (std::tolower((unsigned char)lhs[i]) !=
			std::tolower((unsigned char)rhs[i]))
			return false;
	}
	return true;
}

static bool parse_hotkey_modifier(const std::string& token,
	unsigned* modifier)
{
	if (hotkey_text_equals(token, "ctrl") ||
		hotkey_text_equals(token, "control"))
		*modifier = SDL_HOTKEY_MOD_CTRL;
	else if (hotkey_text_equals(token, "alt") ||
		hotkey_text_equals(token, "option"))
		*modifier = SDL_HOTKEY_MOD_ALT;
	else if (hotkey_text_equals(token, "shift"))
		*modifier = SDL_HOTKEY_MOD_SHIFT;
	else if (hotkey_text_equals(token, "gui") ||
		hotkey_text_equals(token, "super") ||
		hotkey_text_equals(token, "win") ||
		hotkey_text_equals(token, "windows") ||
		hotkey_text_equals(token, "cmd") ||
		hotkey_text_equals(token, "command") ||
		hotkey_text_equals(token, "meta"))
		*modifier = SDL_HOTKEY_MOD_GUI;
	else
		return false;

	return true;
}

static std::string normalize_hotkey_key_name(const std::string& name)
{
	if (hotkey_text_equals(name, "pgup")) return "PageUp";
	if (hotkey_text_equals(name, "pgdn") ||
		hotkey_text_equals(name, "pagedn")) return "PageDown";
	if (hotkey_text_equals(name, "esc")) return "Escape";
	if (hotkey_text_equals(name, "del")) return "Delete";
	if (hotkey_text_equals(name, "ins")) return "Insert";
	if (hotkey_text_equals(name, "keypadplus") ||
		hotkey_text_equals(name, "kpplus")) return "Keypad +";
	if (hotkey_text_equals(name, "keypadminus") ||
		hotkey_text_equals(name, "kpminus")) return "Keypad -";
	return name;
}

static bool is_hotkey_modifier_key(SDL_Keycode key)
{
	return key == SDLK_LCTRL || key == SDLK_RCTRL ||
		key == SDLK_LALT || key == SDLK_RALT ||
		key == SDLK_LSHIFT || key == SDLK_RSHIFT ||
		key == SDLK_LGUI || key == SDLK_RGUI;
}

static unsigned sdl_hotkey_modifiers(SDL_Keymod modifiers)
{
	unsigned result = 0;
	if (modifiers & SDL_KMOD_CTRL) result |= SDL_HOTKEY_MOD_CTRL;
	if (modifiers & SDL_KMOD_ALT) result |= SDL_HOTKEY_MOD_ALT;
	if (modifiers & SDL_KMOD_SHIFT) result |= SDL_HOTKEY_MOD_SHIFT;
	if (modifiers & SDL_KMOD_GUI) result |= SDL_HOTKEY_MOD_GUI;
	if (modifiers & (SDL_KMOD_MODE | SDL_KMOD_LEVEL5))
		result |= SDL_HOTKEY_MOD_UNSUPPORTED;
	return result;
}

bool sdl_hotkey_binding::matches(const SDL_KeyboardEvent& event) const
{
	return enabled && event.key == key &&
		sdl_hotkey_modifiers(event.mod) == modifiers;
}

static sdl_hotkey_binding parse_sdl_hotkey(CConfigurator* cfg,
	const char* config_name, const char* default_value)
{
	sdl_hotkey_binding binding;
	binding.config_name = config_name;

	const char* configured = cfg->get_text_value(config_name, default_value);
	std::string value = trim_hotkey_text(configured ? configured : "");
	if (hotkey_text_equals(value, "none"))
	{
		binding.display = "none";
		return binding;
	}
	if (value.empty())
	{
		printf("%%SDL-W-HOTKEY: %s has an empty binding; action disabled.\n",
			config_name);
		binding.display = "none";
		return binding;
	}

	std::string key_name = value;
	unsigned modifiers = 0;
	for (;;)
	{
		size_t separator = key_name.find('+');
		if (separator == std::string::npos)
			break;

		std::string token = trim_hotkey_text(key_name.substr(0, separator));
		unsigned modifier = 0;
		if (!parse_hotkey_modifier(token, &modifier))
			break;
		if (modifiers & modifier)
		{
			printf("%%SDL-W-HOTKEY: %s has invalid binding \"%s\"; "
				"action disabled.\n", config_name, value.c_str());
			binding.display = "none";
			return binding;
		}
		modifiers |= modifier;
		key_name = trim_hotkey_text(key_name.substr(separator + 1));
	}

	key_name = normalize_hotkey_key_name(trim_hotkey_text(key_name));
	if (key_name.empty())
	{
		printf("%%SDL-W-HOTKEY: %s has invalid binding \"%s\"; "
			"action disabled.\n", config_name, value.c_str());
		binding.display = "none";
		return binding;
	}

	SDL_ClearError();
	SDL_Keycode key = SDL_GetKeyFromName(key_name.c_str());
	if (key == SDLK_UNKNOWN || is_hotkey_modifier_key(key))
	{
		printf("%%SDL-W-HOTKEY: %s has invalid binding \"%s\"; "
			"action disabled.\n", config_name, value.c_str());
		binding.display = "none";
		return binding;
	}

	binding.key = key;
	binding.modifiers = modifiers;
	binding.enabled = true;
	if (modifiers & SDL_HOTKEY_MOD_CTRL) binding.display += "Ctrl+";
	if (modifiers & SDL_HOTKEY_MOD_ALT) binding.display += "Alt+";
	if (modifiers & SDL_HOTKEY_MOD_SHIFT) binding.display += "Shift+";
	if (modifiers & SDL_HOTKEY_MOD_GUI) binding.display += "GUI+";
	const char* display_key = SDL_GetKeyName(key);
	binding.display += (display_key && *display_key) ? display_key : key_name;
	return binding;
}

sdl_application::sdl_application(CConfigurator* cfg)
	: cfg(cfg), shared_keymap(new bx_keymap_c(cfg))
{
}

sdl_application::~sdl_application()
{
	for (const auto& display : displays)
		if (bx_gui == display.get())
			bx_gui = NULL;
	if (bx_keymap == shared_keymap.get())
		bx_keymap = NULL;
}

bx_sdl_gui_c& sdl_application::create_display()
{
	if (shutdown_started)
		FAILURE(SDL, "Cannot create a display after application shutdown");
	auto display = std::make_unique<bx_sdl_gui_c>(*this);
	displays.push_back(std::move(display));
	return *displays.back();
}

bx_sdl_gui_c& sdl_application::display_for_output(const std::string& device_path,
	unsigned output_id)
{
	if (shutdown_started)
		FAILURE(SDL, "Cannot bind a display after application shutdown");
	if (device_path.empty())
		FAILURE(Configuration, "Display output requires a device identity");
	const auto key = std::make_pair(device_path, output_id);
	const auto existing = display_outputs.find(key);
	if (existing != display_outputs.end())
		return *existing->second;

	// Reserve the identity first; a failed display allocation must not consume it.
	const auto entry = display_outputs.emplace(key, nullptr).first;
	try
	{
		if (display_outputs.size() == 1 && !displays.empty())
			entry->second = displays.front().get();
		else
			entry->second = &create_display();
	}
	catch (...)
	{
		display_outputs.erase(entry);
		throw;
	}
	return *entry->second;
}

bx_gui_c& bx_sdl_gui_c::display_for_output(const std::string& device_path,
	unsigned output_id)
{
	return application.display_for_output(device_path, output_id);
}

const bx_sdl_gui_c* sdl_application::find_display_for_output(
	const std::string& device_path, unsigned output_id) const
{
	if (shutdown_started || device_path.empty())
		return nullptr;
	const auto existing = display_outputs.find(std::make_pair(device_path, output_id));
	return existing != display_outputs.end() ? existing->second : nullptr;
}

const bx_gui_c* bx_sdl_gui_c::find_display_for_output(const std::string& device_path,
	unsigned output_id) const
{
	return application.find_display_for_output(device_path, output_id);
}

std::string sdl_application::display_title(const bx_sdl_gui_c* display) const
{
	// Number windows in creation order; hardware output IDs can repeat per card.
	for (std::size_t i = 0; i < displays.size(); ++i)
		if (displays[i].get() == display)
			return "ES40 - Output " + std::to_string(i);
	return "ES40 Emulator";
}

void sdl_application::initialize_keymap()
{
	if (keymap_initialized)
		return;
	if (cfg->get_bool_value("keyboard.use_mapping", false))
	{
		// Loading appends entries and may throw partway through.
		// Publish only a complete map, then failed load can retry without a partial prefix.
		auto loaded = std::make_unique<bx_keymap_c>(cfg);
		loaded->loadKeymap(convertStringToSDLKey);
		const bool published = bx_keymap == shared_keymap.get();
		shared_keymap.swap(loaded);
		if (published)
			bx_keymap = shared_keymap.get();
	}
	keymap_initialized = true;
}

void sdl_application::exit_displays()
{
	shutdown_started = true;
	std::exception_ptr first_error;
	for (const auto& display : displays)
	{
		try { display->exit(); }
		catch (...)
		{
			if (!first_error)
				first_error = std::current_exception();
		}
	}
	// Keep the receivers and any failed native resources alive until process exit. 
	// AlphaSim borrows the initial display during this call.
	if (first_error)
		std::rethrow_exception(first_error);
}

int libsdl_LTX_plugin_init(CConfigurator* cfg)
{
	if (bx_gui || sdl_app)
		FAILURE(Configuration, "A GUI application is already installed");
	auto app = std::make_unique<sdl_application>(cfg);
	bx_sdl_gui_c& initial_display = app->create_display();
	// Publish borrowed aliases only after ownership is established.
	sdl_app = std::move(app);
	bx_gui = &initial_display;
	bx_keymap = sdl_app->keymap();
	printf("%%GUI-I-INS: Installing sdl module as the ES40 GUI\n");
	return 0;
}

void libsdl_LTX_plugin_fini()
{
	// Native teardown is explicit, after the workers join on the main thread.
}

bx_sdl_gui_c::bx_sdl_gui_c(sdl_application& application)
	: application(application), myCfg(application.configuration())
{
}

bx_sdl_gui_c::~bx_sdl_gui_c()
{
	// Native cleanup belongs to explicit main-thread exit, never a destructor.
	unregister_window();
}

void bx_sdl_gui_c::register_window(SDL_Window* window)
{
	if (registered_window_id)
		FAILURE(SDL, "Display already has a registered SDL window");
	const SDL_WindowID id = SDL_GetWindowID(window);
	if (!id)
		FAILURE_1(SDL, "Unable to identify SDL window: %s", SDL_GetError());

	try
	{
		std::lock_guard<std::mutex> lock(sdl_window_owners_mutex);
		if (!sdl_window_owners.emplace(id, this).second)
			FAILURE(SDL, "SDL window already has a registered display");
		registered_window_id = id;
	}
	catch (const std::bad_alloc&)
	{
		FAILURE(SDL, "Unable to allocate SDL window registration");
	}
}

void bx_sdl_gui_c::unregister_window() noexcept
{
	if (!registered_window_id)
		return;
	std::lock_guard<std::mutex> lock(sdl_window_owners_mutex);
	const auto entry = sdl_window_owners.find(registered_window_id);
	if (entry != sdl_window_owners.end() && entry->second == this)
		sdl_window_owners.erase(entry);
	// Explicit exit releases native capture first. 
	if (sdl_mouse_input.capture_window_id == registered_window_id)
	{
		sdl_mouse_input.capture_window_id = 0;
		sdl_mouse_input.captured = false;
	}
	registered_window_id = 0;
}

bx_sdl_gui_c* bx_sdl_gui_c::find_window_owner(SDL_WindowID window_id)
{
	std::lock_guard<std::mutex> lock(sdl_window_owners_mutex);
	const auto entry = sdl_window_owners.find(window_id);
	return entry == sdl_window_owners.end() ? NULL : entry->second;
}

void bx_sdl_gui_c::load_hotkeys()
{
	hotkey_mouse_capture = parse_sdl_hotkey(myCfg,
		"hotkey.mouse_capture", "Ctrl+F10");
	hotkey_media = parse_sdl_hotkey(myCfg,
		"hotkey.media", "Ctrl+F11");
	hotkey_ctrl_alt_delete = parse_sdl_hotkey(myCfg,
		"hotkey.ctrl_alt_delete", "Ctrl+Alt+End");
	hotkey_reset_window = parse_sdl_hotkey(myCfg,
		"hotkey.reset_window", "Ctrl+Alt+Home");
	hotkey_scale_up = parse_sdl_hotkey(myCfg,
		"hotkey.scale_up", "Ctrl+PageUp");
	hotkey_scale_down = parse_sdl_hotkey(myCfg,
		"hotkey.scale_down", "Ctrl+PageDown");

	sdl_hotkey_binding* bindings[] = {
		&hotkey_mouse_capture,
		&hotkey_media,
		&hotkey_ctrl_alt_delete,
		&hotkey_reset_window,
		&hotkey_scale_up,
		&hotkey_scale_down
	};
	const size_t binding_count = sizeof(bindings) / sizeof(bindings[0]);
	bool active[] = {
		hotkey_mouse_capture.enabled,
		hotkey_media.enabled,
		hotkey_ctrl_alt_delete.enabled,
		hotkey_reset_window.enabled,
		hotkey_scale_up.enabled && vid_scale_change_enable,
		hotkey_scale_down.enabled && vid_scale_change_enable
	};
	bool duplicate[sizeof(bindings) / sizeof(bindings[0])] = {};
	for (size_t i = 0; i < binding_count; i++)
	{
		if (!active[i])
			continue;
		for (size_t j = i + 1; j < binding_count; j++)
		{
			if (active[j] &&
				bindings[i]->key == bindings[j]->key &&
				bindings[i]->modifiers == bindings[j]->modifiers)
			{
				printf("%%SDL-W-HOTKEY: %s and %s both use \"%s\"; "
					"both actions disabled.\n",
					bindings[i]->config_name, bindings[j]->config_name,
					bindings[i]->display.c_str());
				duplicate[i] = true;
				duplicate[j] = true;
			}
		}
	}
	for (size_t i = 0; i < binding_count; i++)
	{
		if (duplicate[i])
		{
			bindings[i]->enabled = false;
			bindings[i]->display = "none";
		}
	}
}

void bx_sdl_gui_c::build_window_titles()
{
	auto append_hint = [](std::string& title,
		const sdl_hotkey_binding& binding, const char* description)
	{
		if (binding.enabled)
		{
			title += " - ";
			title += binding.display;
			title += " ";
			title += description;
		}
	};

	window_title = application.display_title(this);
	window_title_grabbed = window_title;
	append_hint(window_title, hotkey_media, "media");
	append_hint(window_title, hotkey_ctrl_alt_delete, "sends C+A+Del");
	append_hint(window_title, hotkey_reset_window, "resets window");

	append_hint(window_title_grabbed, hotkey_mouse_capture, "releases mouse");
	append_hint(window_title_grabbed, hotkey_media, "media");
	append_hint(window_title_grabbed, hotkey_ctrl_alt_delete, "sends C+A+Del");
	append_hint(window_title_grabbed, hotkey_reset_window, "resets window");
}

void bx_sdl_gui_c::specific_init(unsigned x_tilesize, unsigned y_tilesize)
{
	on_main_thread([&] { specific_init_impl(x_tilesize, y_tilesize); });
}

void bx_sdl_gui_c::specific_init_impl(unsigned x_tilesize, unsigned y_tilesize)
{
	// main_thread_init() has already done this; kept as a fallback so the
	// backend still works if it ever runs without claiming main().
	if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_Init(SDL_INIT_VIDEO))
	{
		FAILURE_1(SDL, "Unable to initialize SDL3 video subsystem: %s", SDL_GetError());
	}

	// SDL3: key repeat is handled by the OS; no SDL_EnableKeyRepeat().

	// Alt+F4 belongs to the guest, as in the native Win32 backend.
	SDL_SetHintWithPriority(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0", SDL_HINT_OVERRIDE);

	// Normally prepared before workers start; retain the initialization fallback.
	application.initialize_keymap();

	this->vid_linear = myCfg->get_bool_value("video.linear", true);
	this->vid_scale = (int)myCfg->get_num_value("video.scale_ratio", true, 0);
	this->vid_scale_change_enable = myCfg->get_bool_value("video.scale_change_enable", false);

	const char* ms = myCfg->get_text_value("mouse.speed", "1.0");
	this->mouse_speed = atof(ms);
	if (this->mouse_speed <= 0.0 || this->mouse_speed > 10.0)
	{
		printf("%%SDL-W-MOUSESPEED: invalid mouse.speed \"%s\" (valid: 0.0 < speed <= 10.0); using 1.0.\n", ms);
		this->mouse_speed = 1.0;
	}

	this->mouse_invert_x = myCfg->get_bool_value("mouse.invert_x", false);
	this->mouse_invert_y = myCfg->get_bool_value("mouse.invert_y", false);
	this->mouse_absolute = myCfg->get_bool_value("mouse.absolute", false);
	reset_absolute_mouse_position();
	load_hotkeys();
	build_window_titles();

	// Create the initial window + renderer + texture at 640x480.
	// dimension_update() will recreate the texture if the resolution changes.
	dimension_update(640, 480);

	new_gfx_api = 1;
}

void bx_sdl_gui_c::graphics_frame_update(const u32* pixels, unsigned width, unsigned height)
{
	on_main_thread([&] { graphics_frame_update_impl(pixels, width, height); });
}

void bx_sdl_gui_c::graphics_frame_update_impl(const u32* pixels, unsigned width, unsigned height)
{
	if (!sdl_texture || !sdl_renderer)
		return;

	// Upload the ARGB32 pixels directly to the streaming texture.
	// pitch = width * 4 bytes per pixel
	SDL_UpdateTexture(sdl_texture, NULL, pixels, (int)(width * sizeof(u32)));

	// Present: clear -> draw texture -> flip
	SDL_RenderClear(sdl_renderer);
	SDL_RenderTexture(sdl_renderer, sdl_texture, NULL, NULL);
	SDL_RenderPresent(sdl_renderer);
}

void bx_sdl_gui_c::graphics_tile_update(u8* snapshot, unsigned x, unsigned y)
{
//
}

bx_svga_tileinfo_t* bx_sdl_gui_c::graphics_tile_info(bx_svga_tileinfo_t* info)
{
	return NULL;
}

u8* bx_sdl_gui_c::graphics_tile_get(unsigned x0, unsigned y0, unsigned* w,
	unsigned* h)
{
	return NULL;
}

void bx_sdl_gui_c::graphics_tile_update_in_place(unsigned x0, unsigned y0,
	unsigned w, unsigned h)
{
	//
}

static u32 sdl_scan_to_bx_key(SDL_Scancode sym)
{
	switch (sym)
	{
	case SDL_SCANCODE_BACKSPACE:    return BX_KEY_BACKSPACE;
	case SDL_SCANCODE_TAB:          return BX_KEY_TAB;
	case SDL_SCANCODE_RETURN:       return BX_KEY_ENTER;
	case SDL_SCANCODE_PAUSE:        return BX_KEY_PAUSE;
	case SDL_SCANCODE_ESCAPE:       return BX_KEY_ESC;
	case SDL_SCANCODE_SPACE:        return BX_KEY_SPACE;
	case SDL_SCANCODE_APOSTROPHE:   return BX_KEY_SINGLE_QUOTE;
	case SDL_SCANCODE_COMMA:        return BX_KEY_COMMA;
	case SDL_SCANCODE_MINUS:        return BX_KEY_MINUS;
	case SDL_SCANCODE_PERIOD:       return BX_KEY_PERIOD;
	case SDL_SCANCODE_SLASH:        return BX_KEY_SLASH;

	case SDL_SCANCODE_0:            return BX_KEY_0;
	case SDL_SCANCODE_1:            return BX_KEY_1;
	case SDL_SCANCODE_2:            return BX_KEY_2;
	case SDL_SCANCODE_3:            return BX_KEY_3;
	case SDL_SCANCODE_4:            return BX_KEY_4;
	case SDL_SCANCODE_5:            return BX_KEY_5;
	case SDL_SCANCODE_6:            return BX_KEY_6;
	case SDL_SCANCODE_7:            return BX_KEY_7;
	case SDL_SCANCODE_8:            return BX_KEY_8;
	case SDL_SCANCODE_9:            return BX_KEY_9;

	case SDL_SCANCODE_SEMICOLON:    return BX_KEY_SEMICOLON;
	case SDL_SCANCODE_EQUALS:       return BX_KEY_EQUALS;

	case SDL_SCANCODE_LEFTBRACKET:  return BX_KEY_LEFT_BRACKET;
	case SDL_SCANCODE_BACKSLASH:    return BX_KEY_BACKSLASH;
	case SDL_SCANCODE_NONUSBACKSLASH: return BX_KEY_BACKSLASH;
	case SDL_SCANCODE_RIGHTBRACKET: return BX_KEY_RIGHT_BRACKET;
	case SDL_SCANCODE_GRAVE:        return BX_KEY_GRAVE;

	case SDL_SCANCODE_A:            return BX_KEY_A;
	case SDL_SCANCODE_B:            return BX_KEY_B;
	case SDL_SCANCODE_C:            return BX_KEY_C;
	case SDL_SCANCODE_D:            return BX_KEY_D;
	case SDL_SCANCODE_E:            return BX_KEY_E;
	case SDL_SCANCODE_F:            return BX_KEY_F;
	case SDL_SCANCODE_G:            return BX_KEY_G;
	case SDL_SCANCODE_H:            return BX_KEY_H;
	case SDL_SCANCODE_I:            return BX_KEY_I;
	case SDL_SCANCODE_J:            return BX_KEY_J;
	case SDL_SCANCODE_K:            return BX_KEY_K;
	case SDL_SCANCODE_L:            return BX_KEY_L;
	case SDL_SCANCODE_M:            return BX_KEY_M;
	case SDL_SCANCODE_N:            return BX_KEY_N;
	case SDL_SCANCODE_O:            return BX_KEY_O;
	case SDL_SCANCODE_P:            return BX_KEY_P;
	case SDL_SCANCODE_Q:            return BX_KEY_Q;
	case SDL_SCANCODE_R:            return BX_KEY_R;
	case SDL_SCANCODE_S:            return BX_KEY_S;
	case SDL_SCANCODE_T:            return BX_KEY_T;
	case SDL_SCANCODE_U:            return BX_KEY_U;
	case SDL_SCANCODE_V:            return BX_KEY_V;
	case SDL_SCANCODE_W:            return BX_KEY_W;
	case SDL_SCANCODE_X:            return BX_KEY_X;
	case SDL_SCANCODE_Y:            return BX_KEY_Y;
	case SDL_SCANCODE_Z:            return BX_KEY_Z;

	case SDL_SCANCODE_DELETE:       return BX_KEY_DELETE;

		// Keypad
	case SDL_SCANCODE_KP_0:         return BX_KEY_KP_INSERT;
	case SDL_SCANCODE_KP_1:         return BX_KEY_KP_END;
	case SDL_SCANCODE_KP_2:         return BX_KEY_KP_DOWN;
	case SDL_SCANCODE_KP_3:         return BX_KEY_KP_PAGE_DOWN;
	case SDL_SCANCODE_KP_4:         return BX_KEY_KP_LEFT;
	case SDL_SCANCODE_KP_5:         return BX_KEY_KP_5;
	case SDL_SCANCODE_KP_6:         return BX_KEY_KP_RIGHT;
	case SDL_SCANCODE_KP_7:         return BX_KEY_KP_HOME;
	case SDL_SCANCODE_KP_8:         return BX_KEY_KP_UP;
	case SDL_SCANCODE_KP_9:         return BX_KEY_KP_PAGE_UP;
	case SDL_SCANCODE_KP_PERIOD:    return BX_KEY_KP_DELETE;
	case SDL_SCANCODE_KP_DIVIDE:    return BX_KEY_KP_DIVIDE;
	case SDL_SCANCODE_KP_MULTIPLY:  return BX_KEY_KP_MULTIPLY;
	case SDL_SCANCODE_KP_MINUS:     return BX_KEY_KP_SUBTRACT;
	case SDL_SCANCODE_KP_PLUS:      return BX_KEY_KP_ADD;
	case SDL_SCANCODE_KP_ENTER:     return BX_KEY_KP_ENTER;

		// Arrows + Home/End pad
	case SDL_SCANCODE_UP:           return BX_KEY_UP;
	case SDL_SCANCODE_DOWN:         return BX_KEY_DOWN;
	case SDL_SCANCODE_RIGHT:        return BX_KEY_RIGHT;
	case SDL_SCANCODE_LEFT:         return BX_KEY_LEFT;
	case SDL_SCANCODE_INSERT:       return BX_KEY_INSERT;
	case SDL_SCANCODE_HOME:         return BX_KEY_HOME;
	case SDL_SCANCODE_END:          return BX_KEY_END;
	case SDL_SCANCODE_PAGEUP:       return BX_KEY_PAGE_UP;
	case SDL_SCANCODE_PAGEDOWN:     return BX_KEY_PAGE_DOWN;

		// Function keys
	case SDL_SCANCODE_F1:           return BX_KEY_F1;
	case SDL_SCANCODE_F2:           return BX_KEY_F2;
	case SDL_SCANCODE_F3:           return BX_KEY_F3;
	case SDL_SCANCODE_F4:           return BX_KEY_F4;
	case SDL_SCANCODE_F5:           return BX_KEY_F5;
	case SDL_SCANCODE_F6:           return BX_KEY_F6;
	case SDL_SCANCODE_F7:           return BX_KEY_F7;
	case SDL_SCANCODE_F8:           return BX_KEY_F8;
	case SDL_SCANCODE_F9:           return BX_KEY_F9;
	case SDL_SCANCODE_F10:          return BX_KEY_F10;
	case SDL_SCANCODE_F11:          return BX_KEY_F11;
	case SDL_SCANCODE_F12:          return BX_KEY_F12;

		// Modifier keys
	case SDL_SCANCODE_NUMLOCKCLEAR: return BX_KEY_NUM_LOCK;
	case SDL_SCANCODE_CAPSLOCK:     return BX_KEY_CAPS_LOCK;
	case SDL_SCANCODE_SCROLLLOCK:   return BX_KEY_SCRL_LOCK;
	case SDL_SCANCODE_RSHIFT:       return BX_KEY_SHIFT_R;
	case SDL_SCANCODE_LSHIFT:       return BX_KEY_SHIFT_L;
	case SDL_SCANCODE_RCTRL:        return BX_KEY_CTRL_R;
	case SDL_SCANCODE_LCTRL:        return BX_KEY_CTRL_L;
	case SDL_SCANCODE_RALT:         return BX_KEY_ALT_R;
	case SDL_SCANCODE_LALT:         return BX_KEY_ALT_L;
	case SDL_SCANCODE_LGUI:         return BX_KEY_WIN_L;
	case SDL_SCANCODE_RGUI:         return BX_KEY_WIN_R;

		// Misc function keys
	case SDL_SCANCODE_PRINTSCREEN:  return BX_KEY_PRINT;
	case SDL_SCANCODE_MENU:         return BX_KEY_MENU;

	default:
		BX_ERROR(("sdl3 scancode 0x%x not mapped", (unsigned)sym));
		return BX_KEY_UNHANDLED;
	}
}

void sdl_event_dispatcher::send_guest_key(u32 key)
{
	if (sdl_guest_keyboard && !sdl_guest_input_paused)
		sdl_guest_keyboard->gen_scancode(key);
}

void sdl_event_dispatcher::send_guest_mouse(int dx, int dy, int dz, unsigned buttons)
{
	if (sdl_guest_mouse && !sdl_guest_input_paused)
		sdl_guest_mouse->mouse_motion(dx, dy, dz, buttons);
}

void sdl_event_dispatcher::notify_guest_capture(bool captured)
{
	if (sdl_guest_mouse && !sdl_guest_input_paused)
		sdl_guest_mouse->set_mouse_capture(captured);
}

void sdl_event_dispatcher::release_guest_key(SDL_Scancode scancode)
{
	if (scancode > SDL_SCANCODE_UNKNOWN &&
		scancode < SDL_SCANCODE_COUNT && sdl_keyboard_input.guest_key_pressed[scancode])
	{
		sdl_event_dispatcher::send_guest_key(
			sdl_keyboard_input.guest_key_by_scancode[scancode] | BX_KEY_RELEASED);
		sdl_keyboard_input.guest_key_pressed[scancode] = false;
	}
}

void sdl_event_dispatcher::release_all_guest_keys()
{
	for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
		release_guest_key((SDL_Scancode)i);
}

void sdl_event_dispatcher::clear_hotkey_release_state()
{
	memset(sdl_keyboard_input.swallowed_hotkey_releases, 0,
		sizeof(sdl_keyboard_input.swallowed_hotkey_releases));
	memset(sdl_keyboard_input.fullscreen_hotkey_pressed, 0,
		sizeof(sdl_keyboard_input.fullscreen_hotkey_pressed));
}

void sdl_event_dispatcher::reconcile_hotkey_release_state()
{
	const bool* keys = SDL_GetKeyboardState(NULL);
	if (!keys)
		return;

	for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
	{
		if (sdl_keyboard_input.swallowed_hotkey_releases[i] && !keys[i])
		{
			sdl_keyboard_input.swallowed_hotkey_releases[i] = false;
			sdl_keyboard_input.fullscreen_hotkey_pressed[i] = false;
		}
	}
}

void sdl_event_dispatcher::suppress_hotkey_releases(
	const SDL_KeyboardEvent& event, const sdl_hotkey_binding& binding,
	bool release_guest_modifiers)
{
	if (event.scancode > SDL_SCANCODE_UNKNOWN &&
		event.scancode < SDL_SCANCODE_COUNT)
		sdl_keyboard_input.swallowed_hotkey_releases[event.scancode] = true;

	if (!release_guest_modifiers)
		return;

	const bool* keys = SDL_GetKeyboardState(NULL);
	auto suppress_modifier = [keys](SDL_Scancode left,
		SDL_Scancode right)
	{
		SDL_Scancode scancodes[2] = { left, right };
		for (int i = 0; i < 2; i++)
		{
			SDL_Scancode scancode = scancodes[i];
			if ((keys && keys[scancode]) || sdl_keyboard_input.guest_key_pressed[scancode])
			{
				sdl_keyboard_input.swallowed_hotkey_releases[scancode] = true;
				release_guest_key(scancode);
			}
		}
	};

	if (binding.modifiers & SDL_HOTKEY_MOD_CTRL)
		suppress_modifier(SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL);
	if (binding.modifiers & SDL_HOTKEY_MOD_ALT)
		suppress_modifier(SDL_SCANCODE_LALT, SDL_SCANCODE_RALT);
	if (binding.modifiers & SDL_HOTKEY_MOD_SHIFT)
		suppress_modifier(SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT);
	if (binding.modifiers & SDL_HOTKEY_MOD_GUI)
		suppress_modifier(SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI);
}

void sdl_event_dispatcher::send_guest_ctrl_alt_delete()
{
	sdl_event_dispatcher::send_guest_key(BX_KEY_CTRL_L);
	sdl_event_dispatcher::send_guest_key(BX_KEY_ALT_L);
	sdl_event_dispatcher::send_guest_key(BX_KEY_DELETE);
	sdl_event_dispatcher::send_guest_key(BX_KEY_DELETE | BX_KEY_RELEASED);
	sdl_event_dispatcher::send_guest_key(BX_KEY_ALT_L | BX_KEY_RELEASED);
	sdl_event_dispatcher::send_guest_key(BX_KEY_CTRL_L | BX_KEY_RELEASED);
}

void bx_sdl_gui_c::handle_events(void)
{
	on_main_thread([] { sdl_event_dispatcher::poll(); });
}

void sdl_event_dispatcher::poll()
{
	if (sdl_guest_input_paused || sdl_poll_in_progress)
		return;
	// PollEvent also runs other workers' callbacks. One drain owns the queue,
	// including the single deferred event, until this call returns.
	struct PollGuard
	{
		PollGuard() { sdl_poll_in_progress = true; }
		~PollGuard() { sdl_poll_in_progress = false; }
	} guard;
	sdl_media_pump();
	if (sdl_guest_input_paused)
		return;
	if (sdl_media_input_active())
		reconcile_input_focus();
	while (!sdl_guest_input_paused)
	{
		SDL_Event event;
		if (sdl_has_deferred_event)
		{
			event = sdl_deferred_event;
			sdl_has_deferred_event = false;
		}
		else
		{
			const bool available = SDL_PollEvent(&event);
			// PollEvent can run a nested main-thread pause callback.
			if (sdl_guest_input_paused)
			{
				if (available)
					defer_event(event);
				return;
			}
			if (!available)
				break;
		}
		dispatch(event);
	}
	if (sdl_guest_input_paused)
		return;

	// Focus queries describe SDL's latest state, not each queued event's state.
	reconcile_input_focus();
	reconcile_input_releases();

	// Native popups can intercept releases. Reconcile against SDL's current
	// physical keyboard state so a later normal press is never swallowed.
	reconcile_hotkey_release_state();
}

void sdl_event_dispatcher::defer_event(const SDL_Event& event)
{
	// SDL owns pointer payloads until a later pump. Retain only the scalar
	// event families consumed by this dispatcher and the media UI; other
	// event types are ignored by both and need only their common fields.
	sdl_deferred_event = {};
	sdl_deferred_event.common = event.common;
	if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
		sdl_deferred_event.window = event.window;
	else switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		sdl_deferred_event.key = event.key;
		break;
	case SDL_EVENT_MOUSE_MOTION:
		sdl_deferred_event.motion = event.motion;
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		sdl_deferred_event.button = event.button;
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		sdl_deferred_event.wheel = event.wheel;
		break;
	default:
		break;
	}
	sdl_has_deferred_event = true;
}

void sdl_event_dispatcher::dispatch(const SDL_Event& event)
{
	if (sdl_guest_input_paused)
		return;
	// GUI hotkeys consume their trigger key and, for actions that release
	// guest modifiers, the corresponding physical modifier releases. Keep
	// this ahead of the media popup so a remapped media hotkey also closes it.
	if (event.type == SDL_EVENT_KEY_UP)
	{
		// The media popup has its own release tracker for dialog keys. Let it
		// clear that state before the global tracker consumes a release shared
		// with the media-toggle chord.
		bool media_release_handled = sdl_media_handle_event(&event);
		if (event.key.scancode > SDL_SCANCODE_UNKNOWN &&
			event.key.scancode < SDL_SCANCODE_COUNT &&
			(sdl_keyboard_input.swallowed_hotkey_releases[event.key.scancode] ||
			 sdl_keyboard_input.reconciled_key_releases[event.key.scancode]))
		{
			sdl_keyboard_input.swallowed_hotkey_releases[event.key.scancode] = false;
			sdl_keyboard_input.fullscreen_hotkey_pressed[event.key.scancode] = false;
			sdl_keyboard_input.reconciled_key_releases[event.key.scancode] = false;
			return;
		}
		if (media_release_handled)
			return;
	}

	if (event.type == SDL_EVENT_KEY_DOWN)
	{
		bx_sdl_gui_c* owner = bx_sdl_gui_c::find_window_owner(event.key.windowID);
		// The popup's toggle binding belongs to the display that opened it.
		if (!owner)
			owner = bx_sdl_gui_c::find_window_owner(
				sdl_media_parent_window_id(event.key.windowID));
		if (owner && owner->hotkey_media.matches(event.key))
		{
			if (!event.key.repeat)
			{
				suppress_hotkey_releases(event.key, owner->hotkey_media, true);
				release_input_session();
				sdl_select_media(owner->sdl_window);
			}
			return;
		}
	}

	if (event.type != SDL_EVENT_KEY_UP &&
		sdl_media_handle_event(&event))
		return;

	// Update the requested window, regardless of which display drained the
	// queue. Media sees the event once, before lookup; retired IDs have no owner.
	// Delivery and explicit retirement are serialized on this SDL main thread.
	switch (event.type)
	{
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		if (event.window.windowID &&
			bx_sdl_gui_c::find_window_owner(event.window.windowID) &&
			theSystem)
			theSystem->RequestShutdown();
		return;
	case SDL_EVENT_QUIT:
		if (theSystem)
			theSystem->RequestShutdown();
		return;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	{
		if (sdl_media_input_active())
			return;
		bx_sdl_gui_c* owner = bx_sdl_gui_c::find_window_owner(event.key.windowID);
		if (owner)
			owner->handle_keyboard_event_impl(event);
		return;
	}
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
	{
		bx_sdl_gui_c* owner = bx_sdl_gui_c::find_window_owner(event.window.windowID);
		if (owner)
			owner->handle_focus_event_impl(event);
		return;
	}
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_RESTORED:
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
	case SDL_EVENT_WINDOW_MOVED:
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
	case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
	{
		bx_sdl_gui_c* owner = bx_sdl_gui_c::find_window_owner(event.window.windowID);
		if (owner)
			owner->handle_display_event_impl(event);
		return;
	}
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
	case SDL_EVENT_MOUSE_WHEEL:
	{
		if (sdl_media_input_active())
			return;
		SDL_WindowID event_window;
		if (event.type == SDL_EVENT_MOUSE_MOTION)
			event_window = event.motion.windowID;
		else if (event.type == SDL_EVENT_MOUSE_WHEEL)
			event_window = event.wheel.windowID;
		else
			event_window = event.button.windowID;
		// The host capture source supplies one guest mouse across all outputs.
		if (!event_window || (sdl_mouse_input.captured &&
			event_window != sdl_mouse_input.capture_window_id))
			return;
		bx_sdl_gui_c* owner = bx_sdl_gui_c::find_window_owner(event_window);
		if (owner)
			owner->handle_mouse_event_impl(event);
		return;
	}
	default:
		break;
	}

}

void bx_sdl_gui_c::handle_focus_event_impl(const SDL_Event& event)
{
	reset_absolute_mouse_position();
	// Transfer before subsequent pointer events from the newly focused window.
	if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED &&
		!sdl_media_input_active() && SDL_GetKeyboardFocus() == sdl_window &&
		sdl_mouse_input.captured &&
		sdl_mouse_input.capture_window_id != registered_window_id)
		set_mouse_capture_impl(true);
}

void sdl_event_dispatcher::reconcile_input_focus()
{
	SDL_Window* focus = SDL_GetKeyboardFocus();
	bx_sdl_gui_c* owner = focus && !sdl_media_input_active() ?
		bx_sdl_gui_c::find_window_owner(SDL_GetWindowID(focus)) : NULL;
	if (owner)
	{
		if (sdl_mouse_input.captured &&
			sdl_mouse_input.capture_window_id != owner->registered_window_id)
			owner->set_mouse_capture_impl(true);
		return;
	}
	release_input_session();
}

void sdl_event_dispatcher::release_input_session()
{
	// The guest input session ends outside the displays, including media UI.
	release_all_guest_keys();
	sdl_mouse_input.remainder_x = 0.0;
	sdl_mouse_input.remainder_y = 0.0;
	{
		std::lock_guard<std::mutex> lock(sdl_window_owners_mutex);
		for (const auto& entry : sdl_window_owners)
			entry.second->reset_absolute_mouse_position();
	}
	if (sdl_mouse_input.captured)
		set_mouse_capture(NULL, false);
	else
		sdl_mouse_input.buttons = 0;
}

void sdl_event_dispatcher::reconcile_input_releases()
{
	// Repair releases, never new presses.
	const bool* keys = SDL_GetKeyboardState(NULL);
	if (keys)
	{
		for (int i = 1; i < SDL_SCANCODE_COUNT; i++)
		{
			if (sdl_keyboard_input.guest_key_pressed[i] && !keys[i])
			{
				release_guest_key((SDL_Scancode)i);
				// A later queued key-up must not use another display's mapping.
				sdl_keyboard_input.reconciled_key_releases[i] = true;
			}
		}
	}

	if (!sdl_mouse_input.captured || !sdl_mouse_input.buttons)
		return;
	const SDL_MouseButtonFlags state = SDL_GetMouseState(NULL, NULL);
	int buttons = 0;
	if (state & SDL_BUTTON_LMASK) buttons |= 0x01;
	if (state & SDL_BUTTON_RMASK) buttons |= 0x02;
	if (state & SDL_BUTTON_MMASK) buttons |= 0x04;
	buttons &= sdl_mouse_input.buttons;
	if (buttons != sdl_mouse_input.buttons)
	{
		sdl_mouse_input.buttons = buttons;
		sdl_event_dispatcher::send_guest_mouse(0, 0, 0, buttons);
	}
}

/**
 * Flush any changes to sdl_screen to the actual window.
 **/
void bx_sdl_gui_c::flush(void)
{
	//
}

void bx_sdl_gui_c::handle_keyboard_event_impl(const SDL_Event& event)
{
	u32 key_event;
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
		// A queued repeat cannot restart a reconciled press or turn a held
		// fullscreen trigger into guest input/another shortcut after Alt is up.
		if (event.key.repeat && event.key.scancode > SDL_SCANCODE_UNKNOWN &&
			event.key.scancode < SDL_SCANCODE_COUNT &&
			(sdl_keyboard_input.reconciled_key_releases[event.key.scancode] ||
			 sdl_keyboard_input.fullscreen_hotkey_pressed[event.key.scancode]))
			break;
		if (hotkey_ctrl_alt_delete.matches(event.key))
		{
			if (!event.key.repeat)
			{
				sdl_event_dispatcher::suppress_hotkey_releases(event.key,
					hotkey_ctrl_alt_delete, true);
				sdl_event_dispatcher::send_guest_ctrl_alt_delete();
			}
			break;
		}

		if (hotkey_reset_window.matches(event.key))
		{
			if (!event.key.repeat)
			{
				sdl_event_dispatcher::suppress_hotkey_releases(event.key,
					hotkey_reset_window, false);
				reset_window_size();
			}
			break;
		}

		// Runtime scale adjustment remains gated by video.scale_change_enable.
		if (vid_scale_change_enable && hotkey_scale_up.matches(event.key))
		{
			if (!event.key.repeat)
				sdl_event_dispatcher::suppress_hotkey_releases(event.key,
					hotkey_scale_up, false);
			adjust_window_scale(+1);
			break;
		}
		if (vid_scale_change_enable && hotkey_scale_down.matches(event.key))
		{
			if (!event.key.repeat)
				sdl_event_dispatcher::suppress_hotkey_releases(event.key,
					hotkey_scale_down, false);
			adjust_window_scale(-1);
			break;
		}

		if (hotkey_mouse_capture.matches(event.key))
		{
			if (!event.key.repeat)
			{
				sdl_event_dispatcher::suppress_hotkey_releases(event.key,
					hotkey_mouse_capture, true);
				set_mouse_capture_impl(!sdl_mouse_input.captured);
			}
			break;
		}

		// Match Win32's Alt+Enter desktop-fullscreen shortcut, after the
		// configurable bindings so an explicit user binding takes precedence.
		if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
			(event.key.mod & SDL_KMOD_ALT))
		{
			if (!event.key.repeat)
			{
				if (event.key.scancode > SDL_SCANCODE_UNKNOWN &&
					event.key.scancode < SDL_SCANCODE_COUNT)
				{
					sdl_event_dispatcher::release_guest_key(event.key.scancode);
					sdl_keyboard_input.swallowed_hotkey_releases[event.key.scancode] = true;
					sdl_keyboard_input.fullscreen_hotkey_pressed[event.key.scancode] = true;
				}
				const bool fullscreen = fullscreen_transition_pending ? fullscreen_requested :
					(SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_FULLSCREEN) != 0;
				set_fullscreen(!fullscreen);
			}
			break;
		}

		// convert sym -> bochs code
		if (event.key.scancode > SDL_SCANCODE_UNKNOWN &&
			event.key.scancode < SDL_SCANCODE_COUNT &&
			sdl_keyboard_input.guest_key_pressed[event.key.scancode])
		{
			// A held key keeps its mapping across host focus changes and repeats.
			key_event = sdl_keyboard_input.guest_key_by_scancode[event.key.scancode];
		}
		else if (!myCfg->get_bool_value("keyboard.use_mapping", false))
		{
			key_event = sdl_scan_to_bx_key(event.key.scancode);
		}
		else
		{
			/* use mapping */
			BXKeyEntry* entry = bx_keymap->findHostKey(event.key.key);
			if (!entry)
			{
				BX_ERROR(("host key 0x%x not mapped!",
					(unsigned)event.key.key));
				break;
			}
			key_event = entry->baseKey;
		}

		if (key_event == BX_KEY_UNHANDLED)
			break;

		if (event.key.scancode > SDL_SCANCODE_UNKNOWN &&
			event.key.scancode < SDL_SCANCODE_COUNT &&
			!sdl_keyboard_input.guest_key_pressed[event.key.scancode])
		{
			sdl_keyboard_input.guest_key_by_scancode[event.key.scancode] = key_event;
			sdl_keyboard_input.guest_key_pressed[event.key.scancode] = true;
			sdl_keyboard_input.reconciled_key_releases[event.key.scancode] = false;
		}
		sdl_event_dispatcher::send_guest_key(key_event);

		// Locks: generate immediate press+release pair
		if ((key_event == BX_KEY_NUM_LOCK) || (key_event == BX_KEY_CAPS_LOCK))
		{
			sdl_event_dispatcher::send_guest_key(key_event | BX_KEY_RELEASED);
			if (event.key.scancode > SDL_SCANCODE_UNKNOWN &&
				event.key.scancode < SDL_SCANCODE_COUNT)
				sdl_keyboard_input.guest_key_pressed[event.key.scancode] = false;
		}
		break;

	case SDL_EVENT_KEY_UP:
		if (event.key.scancode > SDL_SCANCODE_UNKNOWN &&
			event.key.scancode < SDL_SCANCODE_COUNT &&
			sdl_keyboard_input.guest_key_pressed[event.key.scancode])
		{
			key_event = sdl_keyboard_input.guest_key_by_scancode[event.key.scancode];
		}
		else if (!myCfg->get_bool_value("keyboard.use_mapping", false))
		{
			key_event = sdl_scan_to_bx_key(event.key.scancode);
		}
		else
		{
			BXKeyEntry* entry = bx_keymap->findHostKey(event.key.key);
			if (!entry)
			{
				BX_ERROR(("host key 0x%x not mapped!",
					(unsigned)event.key.key));
				break;
			}
			key_event = entry->baseKey;
		}

		if (key_event == BX_KEY_UNHANDLED)
			break;

		if ((key_event == BX_KEY_NUM_LOCK) || (key_event == BX_KEY_CAPS_LOCK))
		{
			sdl_event_dispatcher::send_guest_key(key_event);
		}

		sdl_event_dispatcher::send_guest_key(key_event | BX_KEY_RELEASED);
		if (event.key.scancode > SDL_SCANCODE_UNKNOWN &&
			event.key.scancode < SDL_SCANCODE_COUNT)
			sdl_keyboard_input.guest_key_pressed[event.key.scancode] = false;
		break;
	}
}

void bx_sdl_gui_c::handle_mouse_event_impl(const SDL_Event& event)
{
	if (!sdl_window)
		return;
	switch (event.type)
	{
	case SDL_EVENT_MOUSE_MOTION:
		if (sdl_mouse_input.captured)
		{
			double rel_x = (double)event.motion.xrel;
			double rel_y = (double)event.motion.yrel;
			if (mouse_absolute)
			{
				const double x = (double)event.motion.x;
				const double y = (double)event.motion.y;
				if (!mouse_position_valid)
				{
					mouse_last_x = x;
					mouse_last_y = y;
					mouse_position_valid = true;
					break;
				}
				rel_x = x - mouse_last_x;
				rel_y = y - mouse_last_y;
				mouse_last_x = x;
				mouse_last_y = y;
			}

			// PS/2 mouse Y is positive-up, SDL is positive-down; hence the
			// baseline Y negation. invert_x/y flip on top of that.
			double mx = rel_x * mouse_speed;
			double my = -rel_y * mouse_speed;
			sdl_mouse_input.remainder_x += mouse_invert_x ? -mx : mx;
			sdl_mouse_input.remainder_y += mouse_invert_y ? -my : my;

			int dx = (int)sdl_mouse_input.remainder_x;
			int dy = (int)sdl_mouse_input.remainder_y;

			if (dx != 0 || dy != 0)
			{
				sdl_mouse_input.remainder_x -= dx;
				sdl_mouse_input.remainder_y -= dy;
				sdl_event_dispatcher::send_guest_mouse(dx, dy, 0, sdl_mouse_input.buttons);
			}
		}
		break;

	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
	{
		if (!sdl_mouse_input.captured)
		{
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
				&& event.button.button == SDL_BUTTON_LEFT)
			{
				set_mouse_capture_impl(true);
			}
			break;
		}

		int bitmask = 0;
		switch (event.button.button)
		{
		case SDL_BUTTON_LEFT:   bitmask = 0x01; break;
		case SDL_BUTTON_RIGHT:  bitmask = 0x02; break;
		case SDL_BUTTON_MIDDLE: bitmask = 0x04; break;
		default: break;
		}

		if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
			sdl_mouse_input.buttons |= bitmask;
		else
			sdl_mouse_input.buttons &= ~bitmask;

		sdl_event_dispatcher::send_guest_mouse(0, 0, 0, sdl_mouse_input.buttons);
		break;
	}

	case SDL_EVENT_MOUSE_WHEEL:
		if (sdl_mouse_input.captured)
		{
			float wy = event.wheel.y;  // SDL3: float; +y = away from user (scroll up)
			if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
				wy = -wy;
			int dz = (int)wy;
			if (dz != 0)
				sdl_event_dispatcher::send_guest_mouse(0, 0, dz, sdl_mouse_input.buttons);
		}
		break;
	}
}

void bx_sdl_gui_c::handle_display_event_impl(const SDL_Event& event)
{
	if (!sdl_window)
		return;
	switch (event.type)
	{
	case SDL_EVENT_WINDOW_EXPOSED:
		redraw_impl();
		break;
	case SDL_EVENT_WINDOW_RESTORED:
		// Preserve manual geometry and the existing texture on an ordinary
		// restore. Only a deferred guest/scale change requests a new size.
		reset_absolute_mouse_position();
		resize_window_to_guest();
		redraw_impl();
		break;
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		if (res_x > 0 && res_y > 0)
			dimension_update(res_x, res_y);
		break;
	case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
	case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
	{
		// Events can be queued behind a newer fullscreen request. Consult the
		// current flags before releasing its resize barrier.
		const bool fullscreen = (SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_FULLSCREEN) != 0;
		if (!fullscreen_transition_pending)
			fullscreen_requested = fullscreen;
		else if ((event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) == fullscreen_requested &&
			fullscreen == fullscreen_requested)
			fullscreen_transition_pending = false;
		resize_window_to_guest();
		reset_absolute_mouse_position();
		break;
	}
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
	case SDL_EVENT_WINDOW_MOVED:
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		// Host window changes invalidate local coordinates, not the guest mode
		// or the one shared mouse stream's accumulated movement.
		reset_absolute_mouse_position();
		break;
	}
}

void bx_sdl_gui_c::redraw_impl()
{
	if (!sdl_window || !sdl_renderer || !sdl_texture)
		return;
	SDL_RenderClear(sdl_renderer);
	SDL_RenderTexture(sdl_renderer, sdl_texture, NULL, NULL);
	SDL_RenderPresent(sdl_renderer);
}

/**
 * Clear sdl_screen display, and flush it.
 **/
void bx_sdl_gui_c::clear_screen(void)
{
	on_main_thread([&] { clear_screen_impl(); });
}

void bx_sdl_gui_c::clear_screen_impl(void)
{
	if (!sdl_renderer)
		return;

	SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
	SDL_RenderClear(sdl_renderer);
	SDL_RenderPresent(sdl_renderer);
}

/**
 * Set palette-entry index to the desired value.
 *
 * The palette is used in text-mode and in 8bpp VGA mode.
 **/
bool bx_sdl_gui_c::palette_change(unsigned index, unsigned red, unsigned green,
	unsigned blue)
{
	return 1;
}

void bx_sdl_gui_c::dimension_update(unsigned x, unsigned y, unsigned fheight,
	unsigned fwidth, unsigned bpp)
{
	on_main_thread([&] { dimension_update_impl(x, y, fheight, fwidth, bpp); });
}

void bx_sdl_gui_c::dimension_update_impl(unsigned x, unsigned y, unsigned fheight,
	unsigned fwidth, unsigned bpp)
{
	reset_absolute_mouse_position();
	SDL_DisplayID display;
	float scaled_x, scaled_y;
	float content_scale = 1.0f;

	if (sdl_texture)
	{
		SDL_DestroyTexture(sdl_texture);
		sdl_texture = NULL;
	}

	if (!sdl_window)
		display = SDL_GetPrimaryDisplay();
	else
		display = SDL_GetDisplayForWindow(sdl_window);

	if (runtime_scale_override > 0)
		content_scale = (float)runtime_scale_override;
	else if (vid_scale)
		content_scale = (float)vid_scale;
	else
		content_scale = SDL_GetDisplayContentScale(display);

	scaled_x = x * content_scale;
	scaled_y = y * content_scale;
	const bool existing_window = sdl_window != NULL;
	const bool target_changed = x != res_x || y != res_y ||
		(int)scaled_x != last_driven_w || (int)scaled_y != last_driven_h;

	// Keep first-time resources local until the complete display is ready.
	// Failure unwinds on this main thread and leaves creation retryable.
	std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> new_window(
		NULL, SDL_DestroyWindow);
	std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> new_renderer(
		NULL, SDL_DestroyRenderer);
	SDL_Renderer* renderer = sdl_renderer;
	if (!sdl_window)
	{
		new_window.reset(SDL_CreateWindow(window_title.c_str(),
			(int)scaled_x, (int)scaled_y, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
		if (!new_window)
		{
			FAILURE_3(SDL, "Unable to create SDL3 window: %ix%i: %s\n",
				x, y, SDL_GetError());
		}

		new_renderer.reset(SDL_CreateRenderer(new_window.get(), NULL));
		if (!new_renderer)
		{
			FAILURE_3(SDL, "Unable to create SDL3 renderer: %ix%i: %s\n",
				x, y, SDL_GetError());
		}
		renderer = new_renderer.get();

		SDL_RaiseWindow(new_window.get());
		SDL_SetRenderLogicalPresentation(renderer,
			(int)x, (int)y,
			SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}
	else
	{
		SDL_SetRenderLogicalPresentation(sdl_renderer,
			(int)x, (int)y,
			SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}

	std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> new_texture(
		SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		(int)x, (int)y), SDL_DestroyTexture);
	if (!new_texture)
	{
		FAILURE_3(SDL, "Unable to create SDL3 texture: %ix%i: %s\n",
			x, y, SDL_GetError());
	}

	SDL_SetTextureScaleMode(new_texture.get(), vid_linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
	if (new_window)
	{
		// Insertion can fail; keep native resources local until it succeeds.
		register_window(new_window.get());
		sdl_window = new_window.release();
		sdl_renderer = new_renderer.release();
		pending_guest_resize = false;
		fullscreen_requested = false;
		fullscreen_transition_pending = false;
		fullscreen_windowed_deferred = false;
	}
	sdl_texture = new_texture.release();

	res_x = x;
	res_y = y;
	half_res_x = x / 2;
	half_res_y = y / 2;

	// Remember the requested size even while fullscreen or maximized.
	last_driven_w = (int)scaled_x;
	last_driven_h = (int)scaled_y;
	if (existing_window && target_changed)
	{
		const bool in_fullscreen = fullscreen_requested ||
			(SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_FULLSCREEN);
		// A normal-window fullscreen roundtrip preserves its saved rectangle.
		// A maximized/minimized origin still owes a resize on normal restore.
		if (!in_fullscreen || fullscreen_windowed_deferred)
			pending_guest_resize = true;
	}
	resize_window_to_guest();
}

bool bx_sdl_gui_c::set_fullscreen(bool enabled)
{
	if (!sdl_window)
		return false;
	const SDL_WindowFlags flags = SDL_GetWindowFlags(sdl_window);
	const bool was_requested = fullscreen_requested;
	const bool was_transition_pending = fullscreen_transition_pending;
	const bool was_windowed_deferred = fullscreen_windowed_deferred;
	if (enabled && !fullscreen_requested && !(flags & SDL_WINDOW_FULLSCREEN))
		fullscreen_windowed_deferred = (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) != 0;
	// Guard synchronous callbacks as well as platforms that complete later.
	fullscreen_requested = enabled;
	fullscreen_transition_pending = true;

	// A null display mode selects borderless desktop fullscreen, retaining
	// the monitor's resolution and SDL's saved windowed size/position.
	if ((enabled && !SDL_SetWindowFullscreenMode(sdl_window, NULL)) ||
		!SDL_SetWindowFullscreen(sdl_window, enabled))
	{
		fullscreen_requested = was_requested;
		fullscreen_transition_pending = was_transition_pending;
		fullscreen_windowed_deferred = was_windowed_deferred;
		printf("%%SDL-W-FULLSCREEN: Unable to %s fullscreen: %s\n",
			enabled ? "enter" : "leave", SDL_GetError());
		return false;
	}
	fullscreen_transition_pending =
		!!(SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_FULLSCREEN) != enabled;
	reset_absolute_mouse_position();
	resize_window_to_guest();
	return true;
}

bool bx_sdl_gui_c::restore_window_for_resize()
{
	const SDL_WindowFlags flags = SDL_GetWindowFlags(sdl_window);
	// In fullscreen, RestoreWindow also clears SDL's saved maximized state.
	if ((fullscreen_requested ||
		(flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN))) &&
		!SDL_RestoreWindow(sdl_window))
	{
		printf("%%SDL-W-RESTORE: Unable to restore window: %s\n", SDL_GetError());
		return false;
	}
	if ((fullscreen_requested || (flags & SDL_WINDOW_FULLSCREEN)) && !set_fullscreen(false))
		return false;
	return true;
}

void bx_sdl_gui_c::resize_window_to_guest()
{
	if (!sdl_window || !pending_guest_resize || fullscreen_requested ||
		fullscreen_transition_pending || last_driven_w == 0 || last_driven_h == 0)
		return;
	if (SDL_GetWindowFlags(sdl_window) &
		(SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
		return;
	// Consume before calling SDL, which can synchronously dispatch events.
	pending_guest_resize = false;
	if (!SDL_SetWindowSize(sdl_window, last_driven_w, last_driven_h))
		pending_guest_resize = true;
}

void bx_sdl_gui_c::reset_window_size()
{
	if (!sdl_window || last_driven_w == 0 || last_driven_h == 0)
		return;

	reset_absolute_mouse_position();
	pending_guest_resize = true;
	if (!restore_window_for_resize())
		return;
	resize_window_to_guest();
}

void bx_sdl_gui_c::adjust_window_scale(int delta)
{
	// Snapshot the currently-effective integer scale.
	int current;
	if (runtime_scale_override > 0)
		current = runtime_scale_override;
	else if (vid_scale)
		current = (int)vid_scale;
	else if (sdl_window)
		current = (int)(SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(sdl_window)) + 0.5f);
	else
		current = 1;

	int next = current + delta;
	if (next < runtime_scale_min) next = runtime_scale_min;
	if (next > runtime_scale_max) next = runtime_scale_max;
	if (next == runtime_scale_override) return;  // no change

	if (sdl_window && res_x > 0 && res_y > 0)
	{
		if (!restore_window_for_resize())
			return;
		runtime_scale_override = next;
		pending_guest_resize = true;
		dimension_update(res_x, res_y);
	}
	else
		runtime_scale_override = next;
}

void bx_sdl_gui_c::mouse_enabled_changed_specific(bool val)
{
	sdl_event_dispatcher::mouse_enabled_changed(this, val);
}

void sdl_event_dispatcher::mouse_enabled_changed(bx_sdl_gui_c* target, bool val)
{
	on_main_thread([target, val] {
		if (sdl_guest_input_paused)
			return;
		try
		{
			mouse_enabled_changed_impl(target, val);
		}
		catch (...)
		{
			if (!sdl_mouse_input.captured)
			{
				// A failed handoff can end capture after releasing the old window.
				if (sdl_mouse_input.buttons)
					sdl_event_dispatcher::send_guest_mouse(0, 0, 0, 0);
				sdl_mouse_input.buttons = 0;
				sdl_mouse_input.remainder_x = 0.0;
				sdl_mouse_input.remainder_y = 0.0;
			}
			// The caller may already have notified the guest.
			sdl_event_dispatcher::notify_guest_capture(sdl_mouse_input.captured);
			throw;
		}
	});
}

void bx_sdl_gui_c::set_mouse_capture_impl(bool val)
{
	sdl_event_dispatcher::set_mouse_capture(this, val);
}

void sdl_event_dispatcher::set_mouse_capture(bx_sdl_gui_c* target, bool val)
{
	if (sdl_guest_input_paused)
		return;
	if (val && (!target || !target->sdl_window || !target->registered_window_id))
		FAILURE(SDL, "Cannot capture an unregistered display");
	if (!val && sdl_mouse_input.buttons)
	{
		// The guest ignores mouse packets after its capture flag is cleared.
		sdl_event_dispatcher::send_guest_mouse(0, 0, 0, 0);
		sdl_mouse_input.buttons = 0;
	}
	sdl_event_dispatcher::notify_guest_capture(val);
	if (target)
		target->mouse_enabled_changed_specific(val);
	else
		mouse_enabled_changed(NULL, val);
}

void sdl_event_dispatcher::mouse_enabled_changed_impl(bx_sdl_gui_c* target, bool val)
{
	bx_sdl_gui_c* owner = bx_sdl_gui_c::find_window_owner(
		sdl_mouse_input.capture_window_id);
	if (val)
	{
		if (!target || !target->sdl_window || !target->registered_window_id)
			FAILURE(SDL, "Cannot capture an unregistered display");
		if (owner == target && sdl_mouse_input.captured)
			return;
		const bool transferring = owner && sdl_mouse_input.captured;
		if (transferring)
		{
			owner->set_window_mouse_capture_impl(false);
			owner->reset_absolute_mouse_position();
		}
		sdl_mouse_input.capture_window_id = 0;
		sdl_mouse_input.captured = false;
		try
		{
			target->set_window_mouse_capture_impl(true);
		}
		catch (...)
		{
			// The old source was released; a failed acquisition ends capture.
			if (transferring)
				owner->reset_absolute_mouse_motion();
			SDL_ShowCursor();
			throw;
		}
		if (transferring)
			target->reset_absolute_mouse_position();
		else
			target->reset_absolute_mouse_motion();
		SDL_HideCursor();
		sdl_mouse_input.capture_window_id = target->registered_window_id;
		sdl_mouse_input.captured = true;
	}
	else
	{
		if (owner)
		{
			owner->set_window_mouse_capture_impl(false);
			owner->reset_absolute_mouse_motion();
		}
		SDL_ShowCursor();
		sdl_mouse_input.capture_window_id = 0;
		sdl_mouse_input.captured = false;
	}
}

void bx_sdl_gui_c::set_window_mouse_capture_impl(bool val)
{
	// SDL restores the old relative-mode flag on failure. Do not lose track.
	if (!SDL_SetWindowRelativeMouseMode(sdl_window, val && !mouse_absolute))
		FAILURE_1(SDL, "Unable to change SDL mouse capture: %s", SDL_GetError());
	// Shortcut grabbing and the title remain best-effort presentation choices.
	SDL_SetWindowKeyboardGrab(sdl_window, val);
	SDL_SetWindowTitle(sdl_window,
		(val ? window_title_grabbed : window_title).c_str());
}

void bx_sdl_gui_c::exit(void)
{
	on_main_thread([&] { exit_impl(); });
}

void bx_sdl_gui_c::exit_application()
{
	on_main_thread([this] { application.exit_displays(); });
}

void bx_sdl_gui_c::exit_impl(void)
{
	if (registered_window_id &&
		sdl_mouse_input.capture_window_id == registered_window_id)
		sdl_event_dispatcher::mouse_enabled_changed_impl(NULL, false);
	unregister_window();
	sdl_media_shutdown();
	// Native dialogs can still refer to this parent after their callback. Keep
	// its resources until process exit; closing the custom popup above is safe.
	if (sdl_media_parent_was_used(sdl_window))
		return;
	if (sdl_texture) {
		SDL_DestroyTexture(sdl_texture);
		sdl_texture = NULL;
	}
	if (sdl_renderer) {
		SDL_DestroyRenderer(sdl_renderer);
		sdl_renderer = NULL;
	}
	if (sdl_window) {
		SDL_DestroyWindow(sdl_window);
		sdl_window = NULL;
	}
}

/// key mapping for SDL
typedef struct
{
	const char* name;
	u32           value;
} keyTableEntry;

#define DEF_SDL_KEY(key) \
  {                      \
    #key, key            \
  },

keyTableEntry keytable[] = {
	// this include provides all the entries.
  #include "sdlkeys.h"
	// one final entry to mark the end
	{ NULL, 0}
};

// function to convert key names into SDLKey values.
// This first try will be horribly inefficient, but it only has
// to be done while loading a keymap.  Once the simulation starts,

// this function won't be called.
static u32 convertStringToSDLKey(const char* string)
{
	keyTableEntry* ptr;
	for (ptr = &keytable[0]; ptr->name != NULL; ptr++)
	{
		if (!strcmp(string, ptr->name))
			return ptr->value;
	}
	return 0;
}
#endif //defined(HAVE_SDL)
