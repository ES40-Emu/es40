/* ES40 emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
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
 * X-Windows GUI implementation. Allows use of gfx without SDL on Linux.
 *
 * $Id$
 *
 * X-1.5        Camiel Vanderhoeven                             26-MAR-2008
 *      Fix compiler warnings.
 *
 * X-1.4        Pepito Grillo                                   15-MAR-2008
 *      Fixed FAILURE macro
 *
 * X-1.3        Camiel Vanderhoeven                             14-MAR-2008
 *      Formatting
 *
 * X-1.2        Camiel Vanderhoeven                             12-FEB-2008
 *      Moved keyboard code into it's own class (CKeyboard)
 *
 * X-1.1        Camiel Vanderhoeven                             20-JAN-2008
 *      Initial version for ES40 emulator.
 *
 **/
#define XK_PUBLISHING
#define XK_TECHNICAL

#include "../StdAfx.h"

#if defined(HAVE_X11)
#include "gui.h"
#include "keymap.h"
#include "config/Configurator.h"
#include "../graphics/VGA.h"
#include "io/Keyboard.h"
#include "system/System.h"
#include "x11_dispatch.h"
#include "x11_hotkey.h"
#include "x11_scale.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>

extern "C"
{
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
}
#include "gui_win32_font.h"

class bx_x11_gui_c : public bx_gui_c
{
  public:
    bx_x11_gui_c(CConfigurator* cfg)
      : myCfg(cfg), keyboard_map(std::make_unique<bx_keymap_c>(cfg))
    {
      bx_keymap = keyboard_map.get();
    };

    ~bx_x11_gui_c() override
    {
      try { exit(); } catch(...) {}
      if(bx_keymap == keyboard_map.get()) bx_keymap = nullptr;
    }

    virtual void                  specific_init(unsigned x_tilesize,
                                                unsigned y_tilesize);
    virtual void                  text_update(u8*  old_text, u8*  new_text,
                                              unsigned long cursor_x,
                                              unsigned long cursor_y,
                                              bx_vga_tminfo_t tm_info,
                                              unsigned rows);
    virtual void                  graphics_tile_update(u8*  snapshot, unsigned x,
                                                       unsigned y);
    void graphics_frame_update(const u32* pixels, unsigned width, unsigned height) override;
    virtual void                  handle_events(void);
    virtual void                  flush(void);
    virtual void                  clear_screen(void);
    virtual bool                  palette_change(unsigned index, unsigned red,
                                                 unsigned green, unsigned blue);
    virtual void                  dimension_update(unsigned x, unsigned y,
                                                   unsigned fheight = 0,
                                                   unsigned fwidth = 0,
                                                   unsigned bpp = 8);
    virtual void                  mouse_enabled_changed_specific(bool val);
    virtual void                  exit(void);
    bool requires_main_thread() override { return true; }
    void main_thread_init() override;
    void main_thread_pump() override;
    void main_thread_stop() override { dispatcher.stop(); }
    void pause_guest_input() override;
    void resume_guest_input() override;
    void detach_guest_input() override;
    virtual bx_svga_tileinfo_t*   graphics_tile_info(bx_svga_tileinfo_t* info);
    virtual u8*                   graphics_tile_get(unsigned x, unsigned y,
                                                    unsigned*  w, unsigned*  h);
    virtual void                  graphics_tile_update_in_place(unsigned x,
                                                                unsigned y,
                                                                unsigned w,
                                                                unsigned h);
    virtual void                  get_capabilities(u16*  xres, u16*  yres,
                                                   u16*  bpp);
  private:
    CConfigurator*  myCfg;
    std::unique_ptr<bx_keymap_c> keyboard_map;
    XImage* frame_image = nullptr;
    std::vector<u32> frame_pixels;
    unsigned client_width = 0, client_height = 0, configured_scale = 1;
    unsigned runtime_scale_override = 0;
    bool scale_change_enable = false;
    bool video_linear = true;
    bool frame_active = false;
    Atom wm_state_atom = None, net_wm_state_atom = None;
    Atom net_wm_max_horz = None, net_wm_max_vert = None;
    Atom net_wm_fullscreen = None, net_wm_hidden = None;
    Atom net_wm_supported = None;
    bool window_state_dirty = true, window_destroyed = false;
    bool window_maximized = false, window_minimized = false, window_fullscreen = false;
    bool fullscreen_windowed_deferred = false;
    bool fullscreen_requested = false, fullscreen_transition_pending = false;
    bool fullscreen_target = false, fullscreen_entry_resize = false;
    bool fullscreen_restore_maximized = false, fullscreen_restore_minimized = false;
    unsigned fullscreen_restore_width = 0, fullscreen_restore_height = 0;
    std::chrono::steady_clock::time_point fullscreen_deadline = {};
    enum class reset_phase { idle, fullscreen, map, unmaximize };
    reset_phase reset_stage = reset_phase::idle;
    bool reset_window_pending = false, reset_configure_seen = false;
    bool reset_map_maximized = false;
    unsigned long reset_configure_serial = 0;
    std::chrono::steady_clock::time_point reset_deadline = {};
    bool pending_guest_resize = false, pending_window_hints = false;
    bool read_window_state();
    void update_window_state();
    void apply_guest_size();
    bool fullscreen_supported();
    bool send_fullscreen_request(bool enabled);
    void toggle_fullscreen();
    void sync_fullscreen(bool state_known = true);
    void reset_window_size();
    void sync_reset_window(bool state_known = true);
    unsigned window_scale() const;
    void adjust_window_scale(int delta);
    void present_frame();
    void prepare_frame_image(unsigned width, unsigned height);
    void configure_window(int width, int height);
    void load_video_settings();
    void use_legacy_presentation();
    x11_dispatcher dispatcher;
    CKeyboard* guest_keyboard = nullptr;
    CSystem* guest_system = nullptr;
    bool input_paused = false, input_detached = false, retired = false;
    bool legacy_redraw_pending = false;
    bool keyboard_focused = true, detectable_repeat = false;
    bool keyboard_grabbed = false;
    bool mouse_captured = false, guest_mouse_captured = false, mouse_sync_pending = true;
    bool mouse_warp_pending = false, mouse_position_valid = false;
    bool mouse_invert_x = false, mouse_invert_y = false, mouse_absolute = false;
    double mouse_speed = 1.0, mouse_remainder_x = 0.0, mouse_remainder_y = 0.0;
    unsigned mouse_buttons = 0, ignored_mouse_buttons = 0;
    int mouse_x = 0, mouse_y = 0, mouse_warp_x = 0, mouse_warp_y = 0;
    unsigned long mouse_input_serial = 0, mouse_motion_serial = 0, mouse_warp_serial = 0;
    x11_hotkey_binding hotkey_ctrl_alt_delete, hotkey_reset_window;
    x11_hotkey_binding hotkey_scale_up, hotkey_scale_down, hotkey_mouse_capture;
    unsigned hotkey_modifier_masks[8] = {};
    unsigned hotkey_key_modifiers[256] = {};
    struct key_press
    {
      u32 guest = BX_KEY_UNHANDLED;
      bool down = false, modifier = false, control = false;
      bool suppressed = false;
      unsigned host_modifiers = 0;
      int scale_delta = 0;
    } pressed_keys[256];
    unsigned long key_release_serial[256] = {};
    std::exception_ptr event_error;

    void invoke(std::function<void()> fn);
    void pump_events(bool process_input = false) noexcept;
    void poll_events(bool process_input);
    void keypress(XKeyEvent* event, bool released);
    void dispatch_keypress(XKeyEvent* event);
    void suppress_guest_keys();
    u32 translate_key(KeySym keysym, KeySym sdl_symbol = NoSymbol);
    void release_keys();
    void load_keyboard_mapping();
    void load_hotkeys();
    void update_mouse_title();
    void load_mouse_settings();
    void send_mouse_motion(double dx, double dy);
    void sync_mouse_capture();
    void check_mouse_capture();
    void reconcile_mouse_buttons();
    void reset_absolute_mouse_position();
    void recenter_mouse();
    void mouse_button(XButtonEvent* event, bool released);
    void mouse_motion(XMotionEvent* event);
    void refresh_hotkey_modifiers();
    unsigned hotkey_modifiers(unsigned state) const;
    void send_ctrl_alt_delete(unsigned code);
    void reconcile_keyboard_releases(bool process_input = false);
    bool guest_key_held(u32 key, unsigned except = 256) const;
    void specific_init_impl(unsigned tilewidth, unsigned tileheight);
    void text_update_impl(u8* old_text, u8* new_text, unsigned long cursor_x,
                          unsigned long cursor_y, bx_vga_tminfo_t tm_info, unsigned rows,
                          const unsigned* palette_indices);
    void graphics_tile_update_impl(u8* snapshot, unsigned x, unsigned y);
    void graphics_frame_update_impl(const u32* pixels, unsigned width, unsigned height);
    void clear_screen_impl();
    bool palette_change_impl(unsigned index, unsigned red, unsigned green, unsigned blue);
    void dimension_update_impl(unsigned x, unsigned y, unsigned fheight,
                               unsigned fwidth, unsigned bpp, bool full_frame = false);
    void mouse_enabled_changed_impl(bool val);
    void exit_impl();
    bx_svga_tileinfo_t* graphics_tile_info_impl(bx_svga_tileinfo_t* info);
    u8* graphics_tile_get_impl(unsigned x, unsigned y, unsigned* w, unsigned* h);
    void graphics_tile_update_in_place_impl(unsigned x, unsigned y, unsigned w, unsigned h);
};

// declare one instance of the gui object and call macro to insert the
// plugin code
static bx_x11_gui_c*  theGui = NULL;
IMPLEMENT_GUI_PLUGIN_CODE(x11)
#define MAX_MAPPED_STRING_LENGTH  10

/* These are used as arguments to nearly every Xlib routine, so it saves
 * routine arguments to declare them global.  If there were
 * additional source files, they would be declared extern there. */
Display*              bx_x_display;
int                   bx_x_screen_num;
static Visual*        default_visual;
static Colormap       default_cmap;
static unsigned long  white_pixel = 0, black_pixel = 0;

static char           progname[] = "es40";

static unsigned int   text_rows = 25, text_cols = 80;
static u8             h_panning = 0, v_panning = 0;
static u16            line_compare = 1023;

static Window         win;
static GC             gc, gc_inv;
static unsigned       font_width, font_height;
static unsigned       dimension_x = 0, dimension_y = 0;
static unsigned       vga_bpp = 8;

static XImage*        ximage = NULL;
static unsigned       imDepth, imWide, imBPP;
static Cursor         hidden_cursor = None;
static Atom           wm_delete_window = None, wm_protocols = None;
static XErrorHandler  previous_x_error_handler = nullptr;
static Display*       error_display = nullptr;
static int            x_error = 0;

// Report Xlib errors on the caller; never throw from this callback.
static int record_x11_error(Display* display, XErrorEvent* event)
{
  if(display == error_display)
  {
    if(!x_error) x_error = event->error_code;
    return 0;
  }
  return previous_x_error_handler ? previous_x_error_handler(display, event) : 0;
}

static void restore_x11_error_handler()
{
  if(error_display)
    XSetErrorHandler(previous_x_error_handler);
  error_display = nullptr;
  previous_x_error_handler = nullptr;
  x_error = 0;
}

static void check_x11_error()
{
  if(x_error)
    FAILURE_1(Runtime, "X11 display request failed (X error %d)", x_error);
}

static bool           CTRL_pressed = 0;

static unsigned       prev_cursor_x = 0;
static unsigned       prev_cursor_y = 0;

static u32            convertStringToXKeysym(const char* string);

static bool           x_init_done = false;

static Pixmap         vgafont[256];

static bool           x_keymapping;
static bool           x_private_colormap;
static constexpr u32  x11_sdl_keymap_mask = 0x40000000;

void bx_x11_gui_c::load_keyboard_mapping()
{
  const bool enabled = myCfg->get_bool_value("keyboard.use_mapping", false);
  if(enabled)
  {
    auto loaded = std::make_unique<bx_keymap_c>(myCfg);
    loaded->loadKeymap(convertStringToXKeysym);
    const bool published = bx_keymap == keyboard_map.get();
    keyboard_map.swap(loaded);
    if(published) bx_keymap = keyboard_map.get();
  }
  x_keymapping = enabled;
}

// X11 split-image offsets must fit signed 16-bit coordinates.
static void validate_x11_dimensions(unsigned width, unsigned height)
{
  if(!width || !height || width > 32767 || height > 32767 ||
     (size_t)width > (std::numeric_limits<size_t>::max)() / sizeof(u32) / height)
    FAILURE(Configuration, "Invalid X11 display dimensions");
}

static XImage* create_x11_image(unsigned width, unsigned height)
{
  validate_x11_dimensions(width, height);
  XImage* image = XCreateImage(bx_x_display, default_visual,
      DefaultDepth(bx_x_display, bx_x_screen_num), ZPixmap, 0, nullptr,
      width, height, 32, 0);
  if(!image)
    FAILURE(OutOfMemory, "Unable to create X11 image");
  if(image->bytes_per_line <= 0 || image->bits_per_pixel < image->depth ||
     (size_t)image->bytes_per_line > (std::numeric_limits<size_t>::max)() / height)
  {
    XDestroyImage(image);
    FAILURE(Configuration, "Invalid X11 image layout");
  }
  image->data = (char*)calloc(height, (size_t)image->bytes_per_line);
  if(!image->data)
  {
    XDestroyImage(image);
    FAILURE(OutOfMemory, "Unable to allocate X11 image pixels");
  }
  return image;
}

static unsigned long x11_color_component(unsigned value, unsigned long mask)
{
  if(!mask) return 0;
  unsigned shift = 0;
  while(!(mask & 1)) { mask >>= 1; ++shift; }
  return (unsigned long)(((u64)value * mask + 127) / 255) << shift;
}

static unsigned long x11_rgb(unsigned red, unsigned green, unsigned blue)
{
  return x11_color_component(red, default_visual->red_mask) |
         x11_color_component(green, default_visual->green_mask) |
         x11_color_component(blue, default_visual->blue_mask);
}

u32                   ascii_to_key_event[0x5f] = {
  //  !"#$%&'
  BX_KEY_SPACE,
  BX_KEY_1,
  BX_KEY_SINGLE_QUOTE,
  BX_KEY_3,
  BX_KEY_4,
  BX_KEY_5,
  BX_KEY_7,
  BX_KEY_SINGLE_QUOTE,

  // ()*+,-./
  BX_KEY_9,
  BX_KEY_0,
  BX_KEY_8,
  BX_KEY_EQUALS,
  BX_KEY_COMMA,
  BX_KEY_MINUS,
  BX_KEY_PERIOD,
  BX_KEY_SLASH,

  // 01234567
  BX_KEY_0,
  BX_KEY_1,
  BX_KEY_2,
  BX_KEY_3,
  BX_KEY_4,
  BX_KEY_5,
  BX_KEY_6,
  BX_KEY_7,

  // 89:;<=>?
  BX_KEY_8,
  BX_KEY_9,
  BX_KEY_SEMICOLON,
  BX_KEY_SEMICOLON,
  BX_KEY_COMMA,
  BX_KEY_EQUALS,
  BX_KEY_PERIOD,
  BX_KEY_SLASH,

  // @ABCDEFG
  BX_KEY_2,
  BX_KEY_A,
  BX_KEY_B,
  BX_KEY_C,
  BX_KEY_D,
  BX_KEY_E,
  BX_KEY_F,
  BX_KEY_G,

  // HIJKLMNO
  BX_KEY_H,
  BX_KEY_I,
  BX_KEY_J,
  BX_KEY_K,
  BX_KEY_L,
  BX_KEY_M,
  BX_KEY_N,
  BX_KEY_O,

  // PQRSTUVW
  BX_KEY_P,
  BX_KEY_Q,
  BX_KEY_R,
  BX_KEY_S,
  BX_KEY_T,
  BX_KEY_U,
  BX_KEY_V,
  BX_KEY_W,

  // XYZ[\]^_
  BX_KEY_X,
  BX_KEY_Y,
  BX_KEY_Z,
  BX_KEY_LEFT_BRACKET,
  BX_KEY_BACKSLASH,
  BX_KEY_RIGHT_BRACKET,
  BX_KEY_6,
  BX_KEY_MINUS,

  // `abcdefg
  BX_KEY_GRAVE,
  BX_KEY_A,
  BX_KEY_B,
  BX_KEY_C,
  BX_KEY_D,
  BX_KEY_E,
  BX_KEY_F,
  BX_KEY_G,

  // hijklmno
  BX_KEY_H,
  BX_KEY_I,
  BX_KEY_J,
  BX_KEY_K,
  BX_KEY_L,
  BX_KEY_M,
  BX_KEY_N,
  BX_KEY_O,

  // pqrstuvw
  BX_KEY_P,
  BX_KEY_Q,
  BX_KEY_R,
  BX_KEY_S,
  BX_KEY_T,
  BX_KEY_U,
  BX_KEY_V,
  BX_KEY_W,

  // xyz{|}~
  BX_KEY_X,
  BX_KEY_Y,
  BX_KEY_Z,
  BX_KEY_LEFT_BRACKET,
  BX_KEY_BACKSLASH,
  BX_KEY_RIGHT_BRACKET,
  BX_KEY_GRAVE
};

extern u8             graphics_snapshot[32 * 1024];

static void           create_internal_vga_font(void);

// extern "C" void select_visual(void);
#define ROUNDUP(nbytes, pad)  ((((nbytes) + ((pad) - 1)) / (pad)) * ((pad) >> 3))
#define MAX_VGA_COLORS        256

unsigned long   col_vals[MAX_VGA_COLORS]; // 256 VGA colors
unsigned        curr_foreground, curr_background;

static unsigned x_tilesize, y_tilesize;

//BxEvent *x11_notify_callback (void *unused, BxEvent *event);
//bxevent_handler old_callback = NULL;
//void *old_callback_arg = NULL;

// Try to allocate NCOLORS at once in the colormap provided.  If it can
// be done, return true.  If not, return false.  (In either case, free
// up the color cells so that we don't add to the problem!)  This is used
// to determine whether Bochs should use a private colormap even when the

// user did not specify it.
static bool test_alloc_colors(Colormap cmap, u32 n_tries)
{
  XColor        color;
  unsigned long pixel[MAX_VGA_COLORS];
  bool          pixel_valid[MAX_VGA_COLORS];
  u32           n_allocated = 0;
  u32           i;
  color.flags = DoRed | DoGreen | DoBlue;
  for(i = 0; i < n_tries; i++)
  {

    // choose weird color values that are unlikely to already be in the
    // colormap.
    color.red = ((i + 41) % MAX_VGA_COLORS) << 8;
    color.green = ((i + 42) % MAX_VGA_COLORS) << 8;
    color.blue = ((i + 43) % MAX_VGA_COLORS) << 8;
    pixel_valid[i] = false;
    if(XAllocColor(bx_x_display, cmap, &color))
    {
      pixel[i] = color.pixel;
      pixel_valid[i] = true;
      n_allocated++;
    }
  }

  BX_INFO(("test_alloc_colors: %d colors available out of %d colors tried",
            n_allocated, n_tries));

  // now free them all
  for(i = 0; i < n_tries; i++)
  {
    if(pixel_valid[i])
      XFreeColors(bx_x_display, cmap, &pixel[i], 1, 0);
  }

  return(n_allocated == n_tries);
}

// Callers may hold guiMutex; never acquire it on the main thread.
void bx_x11_gui_c::invoke(std::function<void()> fn)
{
  if(!dispatcher.invoke(std::move(fn)))
    FAILURE(Runtime, "The X11 application thread is not running");
}

void bx_x11_gui_c::main_thread_init()
{
  if(retired || !dispatcher.start())
    FAILURE(Runtime, "Invalid X11 application initialization");
  guest_keyboard = theKeyboard;
  guest_system = theSystem;
}

void bx_x11_gui_c::main_thread_pump()
{
  dispatcher.pump([this] { pump_events(); });
}

void bx_x11_gui_c::pause_guest_input()
{
  invoke([this] {
    input_paused = true;
    mouse_enabled_changed_impl(false);
  });
}

void bx_x11_gui_c::resume_guest_input()
{
  invoke([this] {
    mouse_enabled_changed_impl(false);
    input_paused = false;
  });
}

void bx_x11_gui_c::detach_guest_input()
{
  invoke([this] {
    mouse_enabled_changed_impl(false);
    input_detached = true;
    guest_keyboard = nullptr;
    guest_system = nullptr;
    legacy_redraw_pending = false;
    for(auto& key : pressed_keys) key = {};
    CTRL_pressed = false;
  });
}

void bx_x11_gui_c::pump_events(bool process_input) noexcept
{
  if(event_error || retired) return;
  try
  {
    poll_events(process_input);
    if(bx_x_display) XFlush(bx_x_display);
    check_x11_error();
  }
  catch(...)
  {
    event_error = std::current_exception();
    try { mouse_enabled_changed_impl(false); } catch(...) {}
    if(guest_system) guest_system->RequestShutdown();
  }
}

void bx_x11_gui_c::handle_events()
{
  unsigned redraw_width = 0, redraw_height = 0;
  invoke([&] {
    // Only a live graphics worker may deliver guest input.
    pump_events(true);
    if(event_error) std::rethrow_exception(event_error);
    if(legacy_redraw_pending && !input_paused && !input_detached)
    {
      redraw_width = dimension_x;
      redraw_height = dimension_y;
      legacy_redraw_pending = false;
    }
  });
  // Keep guest VGA mutations on the graphics worker.
  if(redraw_width && redraw_height && theVGA)
    theVGA->redraw_area(0, 0, redraw_width, redraw_height);
}

void bx_x11_gui_c::specific_init(unsigned width, unsigned height)
{
  invoke([&] {
    if(retired) FAILURE(Runtime, "Cannot initialize a retired X11 display");
    specific_init_impl(width, height);
  });
}

void bx_x11_gui_c::text_update(u8* old_text, u8* new_text,
    unsigned long cursor_x, unsigned long cursor_y, bx_vga_tminfo_t tm_info, unsigned rows)
{
  // Read on the producer to avoid crossing an already-held device bus lock.
  if(!theVGA) FAILURE(Runtime, "X11 legacy text requires a VGA source");
  unsigned palette_indices[16];
  for(unsigned i = 0; i < 16; ++i)
    palette_indices[i] = theVGA->get_actl_palette_idx(i);
  invoke([&] { text_update_impl(old_text, new_text, cursor_x, cursor_y, tm_info,
                               rows, palette_indices); });
}

void bx_x11_gui_c::graphics_tile_update(u8* pixels, unsigned x, unsigned y)
{
  invoke([&] { graphics_tile_update_impl(pixels, x, y); });
}

void bx_x11_gui_c::graphics_frame_update(const u32* pixels, unsigned width, unsigned height)
{
  invoke([&] { graphics_frame_update_impl(pixels, width, height); });
}

void bx_x11_gui_c::clear_screen()
{
  invoke([this] { clear_screen_impl(); });
}

bool bx_x11_gui_c::palette_change(unsigned index, unsigned red, unsigned green, unsigned blue)
{
  bool changed = false;
  invoke([&] { changed = palette_change_impl(index, red, green, blue); });
  return changed;
}

void bx_x11_gui_c::dimension_update(unsigned x, unsigned y, unsigned fheight,
                                  unsigned fwidth, unsigned bpp)
{
  invoke([&] { dimension_update_impl(x, y, fheight, fwidth, bpp,
                                    frame_active && bpp == 32 && !fheight); });
}

void bx_x11_gui_c::mouse_enabled_changed_specific(bool val)
{
  invoke([&] { if(bx_x_display && !input_detached) mouse_enabled_changed_impl(val); });
}

bx_svga_tileinfo_t* bx_x11_gui_c::graphics_tile_info(bx_svga_tileinfo_t* info)
{
  bx_svga_tileinfo_t* result = nullptr;
  invoke([&] { result = graphics_tile_info_impl(info); });
  return result;
}

u8* bx_x11_gui_c::graphics_tile_get(unsigned x, unsigned y, unsigned* w, unsigned* h)
{
  u8* result = nullptr;
  // The producer owns this tile until update_in_place.
  invoke([&] { result = graphics_tile_get_impl(x, y, w, h); });
  return result;
}

void bx_x11_gui_c::graphics_tile_update_in_place(unsigned x, unsigned y,
                                               unsigned w, unsigned h)
{
  invoke([&] { graphics_tile_update_in_place_impl(x, y, w, h); });
}

void bx_x11_gui_c::exit()
{
  invoke([this] {
    retired = true;
    guest_keyboard = nullptr;
    guest_system = nullptr;
    input_detached = true;
    exit_impl();
  });
}

void bx_x11_gui_c::load_video_settings()
{
  const u64 scale = myCfg->get_num_value("video.scale_ratio", true, 0);
  if(scale > 10)
    printf("%%X11-W-SCALE: invalid video.scale_ratio %llu (valid: 0 - 10); using 1x.\n",
           (unsigned long long)scale);
  configured_scale = scale >= 1 && scale <= 10 ? (unsigned)scale : 1;
  video_linear = myCfg->get_bool_value("video.linear", true);
  scale_change_enable = myCfg->get_bool_value("video.scale_change_enable", false);
}

void bx_x11_gui_c::load_mouse_settings()
{
  const char* speed = myCfg->get_text_value("mouse.speed", "1.0");
  mouse_speed = std::atof(speed);
  if(!std::isfinite(mouse_speed) || mouse_speed <= 0.0 || mouse_speed > 10.0)
  {
    printf("%%X11-W-MOUSESPEED: invalid mouse.speed \"%s\" "
           "(valid: 0.0 < speed <= 10.0); using 1.0.\n", speed);
    mouse_speed = 1.0;
  }
  mouse_invert_x = myCfg->get_bool_value("mouse.invert_x", false);
  mouse_invert_y = myCfg->get_bool_value("mouse.invert_y", false);
  mouse_absolute = myCfg->get_bool_value("mouse.absolute", false);
}

void bx_x11_gui_c::specific_init_impl(unsigned tilewidth, unsigned tileheight)
try
{
  if(x_init_done) { new_gfx_api = 1; return; }
  validate_x11_dimensions(tilewidth, tileheight);
  load_video_settings();
  load_mouse_settings();
  unsigned              i;
  int                   x;
  int                   y;  /* window position */
  unsigned int          border_width = 4;         /* four pixels */
  const char*           icon_name = "ES40";
  XSizeHints            size_hints = {};
  char*                 display_name = NULL;

  /* create GC for text and drawing */
  unsigned long         valuemask = 0;            /* ignore XGCvalues and use defaults */
  XGCValues             values = {};
  int                   default_depth;
  XSetWindowAttributes  win_attr = {};
  unsigned long         plane_masks_return[1];
  XColor                color;

  x_tilesize = tilewidth;
  y_tilesize = tileheight;

  /* connect to X server */
  if((bx_x_display = XOpenDisplay(display_name)) == NULL)
  {
    FAILURE(Runtime, "Unable to connect to the X11 display");
  }
  error_display = bx_x_display;
  x_error = 0;
  previous_x_error_handler = XSetErrorHandler(record_x11_error);

  /* get screen size from display structure macro */
  bx_x_screen_num = DefaultScreen(bx_x_display);

  /* Note that in a real application, x and y would default to 0
   * but would be settable from the command line or resource database.
   */
  x = y = 0;

  // Temporary values so we can create the window
  font_width = 8;
  font_height = 16;
  text_cols = 80;
  text_rows = 25;

  dimension_x = text_cols * font_width;
  dimension_y = text_rows * font_height;
  client_width = dimension_x;
  client_height = dimension_y;

  /* create opaque window */
  win = XCreateSimpleWindow(bx_x_display,
                            RootWindow(bx_x_display, bx_x_screen_num), x, y,
                            dimension_x, dimension_y, border_width,
                            BlackPixel(bx_x_display, bx_x_screen_num),
                            BlackPixel(bx_x_display, bx_x_screen_num));
  if(!win) FAILURE(Runtime, "Unable to create the X11 window");

  // (attempt to) enable backing store
  win_attr.save_under = 1;
  win_attr.backing_store = Always;
  XChangeWindowAttributes(bx_x_display, win, CWSaveUnder | CWBackingStore,
                          &win_attr);

  default_depth = DefaultDepth(bx_x_display, bx_x_screen_num);
  default_visual = DefaultVisual(bx_x_display, bx_x_screen_num);

  // TrueColor pixels encode RGB directly and cannot have writable palette cells.
  const bool writable_visual = default_visual->c_class == PseudoColor ||
                               default_visual->c_class == GrayScale;
  x_private_colormap = myCfg->get_bool_value("private_colormap", true) && writable_visual;
  if(!x_private_colormap)
  {

    //if (!SIM->get_param_bool(BXPN_PRIVATE_COLORMAP)->get()) {
    default_cmap = DefaultColormap(bx_x_display, bx_x_screen_num);

    // try to use default colormap.  If not enough colors are available,
    // then switch to private colormap despite the user setting.  There
    // are too many cases when no colors are available and ES40 Emulator
    // simply draws everything in black on black.
    if(writable_visual && !test_alloc_colors(default_cmap, 16))
    {
      printf("xxx: I can't allocate 16 colors\n");
      x_private_colormap = true;
    }

    col_vals[0] = BlackPixel(bx_x_display, bx_x_screen_num);
    col_vals[15] = WhitePixel(bx_x_display, bx_x_screen_num);
    for(i = 1; i < MAX_VGA_COLORS; i++)
    {
      if(i == 15)
        continue;
      col_vals[i] = col_vals[0];
    }
  }

  if(x_private_colormap)
  {

    //if (SIM->get_param_bool(BXPN_PRIVATE_COLORMAP)->get()) {
    default_cmap = XCreateColormap(bx_x_display,
                                   DefaultRootWindow(bx_x_display),
                                   default_visual, AllocNone);
    if(XAllocColorCells(bx_x_display, default_cmap, False, plane_masks_return,
       0, col_vals, MAX_VGA_COLORS) == 0)
    {
      FAILURE(Runtime, "Unable to allocate the X11 private colormap");
    }

    win_attr.colormap = default_cmap;
    XChangeWindowAttributes(bx_x_display, win, CWColormap, &win_attr);

    color.flags = DoRed | DoGreen | DoBlue;

    for(i = 0; i < MAX_VGA_COLORS; i++)
    {
      color.pixel = col_vals[i];
      if(i == 15)
      {
        color.red = 0xffff;
        color.green = 0xffff;
        color.blue = 0xffff;
      }
      else
      {
        color.red = 0;
        color.green = 0;
        color.blue = 0;
      }

      XStoreColor(bx_x_display, default_cmap, &color);
    }
  }

  // convenience variables which hold the black & white color indeces
  black_pixel = col_vals[0];
  white_pixel = col_vals[15];
  XSetWindowBackground(bx_x_display, win, black_pixel);

  BX_INFO(("font %u wide x %u high, display depth = %d", (unsigned) font_width,
            (unsigned) font_height, default_depth));

  //select_visual();

  /* Set size hints for window manager.  The window manager may
   * override these settings.  Note that in a real
   * application if size or position were set by the user
   * the flags would be UPosition and USize, and these would
   * override the window manager's preferences for this window. 
   */

  /* x, y, width, and height hints are now taken from
   * the actual settings of the window when mapped. Note
   * that PPosition and PSize must be specified anyway. 
   */
  size_hints.flags = PPosition | PSize | PMinSize | PMaxSize;
  size_hints.max_width = size_hints.min_width = dimension_x;
  size_hints.max_height = size_hints.min_height = dimension_y;
  {
    XWMHints      wm_hints = {};
    XClassHint    class_hints = {};
    wm_hints.flags = StateHint | InputHint;
    wm_hints.initial_state = NormalState;
    wm_hints.input = True;
    class_hints.res_name = progname;
    class_hints.res_class = (char*) "ES40 Emulator";

    load_hotkeys();
    XSetIconName(bx_x_display, win, icon_name);
    XSetWMNormalHints(bx_x_display, win, &size_hints);
    XSetWMHints(bx_x_display, win, &wm_hints);
    XSetClassHint(bx_x_display, win, &class_hints);

    wm_delete_window = XInternAtom(bx_x_display, "WM_DELETE_WINDOW", False);
    wm_protocols = XInternAtom(bx_x_display, "WM_PROTOCOLS", False);
    if(!wm_delete_window || !wm_protocols ||
       !XSetWMProtocols(bx_x_display, win, &wm_delete_window, 1))
      FAILURE(Runtime, "Unable to register X11 window protocols");
    wm_state_atom = XInternAtom(bx_x_display, "WM_STATE", False);
    net_wm_state_atom = XInternAtom(bx_x_display, "_NET_WM_STATE", False);
    net_wm_max_horz = XInternAtom(bx_x_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    net_wm_max_vert = XInternAtom(bx_x_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    net_wm_fullscreen = XInternAtom(bx_x_display, "_NET_WM_STATE_FULLSCREEN", False);
    net_wm_hidden = XInternAtom(bx_x_display, "_NET_WM_STATE_HIDDEN", False);
    net_wm_supported = XInternAtom(bx_x_display, "_NET_SUPPORTED", False);
    if(!wm_state_atom || !net_wm_state_atom || !net_wm_max_horz || !net_wm_max_vert ||
       !net_wm_fullscreen || !net_wm_hidden || !net_wm_supported)
      FAILURE(Runtime, "Unable to register X11 window state atoms");
  }

  /* Select event types wanted */
  XSelectInput(bx_x_display, win, ExposureMask | KeyPressMask | KeyReleaseMask |
               ButtonPressMask | ButtonReleaseMask | StructureNotifyMask |
               PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask |
               PropertyChangeMask);
  Bool repeat_supported = False;
  detectable_repeat = XkbSetDetectableAutoRepeat(bx_x_display, True, &repeat_supported)
                      && repeat_supported;

  /* Create default Graphics Context */
  gc = XCreateGC(bx_x_display, win, valuemask, &values);
  gc_inv = XCreateGC(bx_x_display, win, valuemask, &values);
  if(!gc || !gc_inv) FAILURE(OutOfMemory, "Unable to create X11 graphics contexts");

  XSetState(bx_x_display, gc, white_pixel, black_pixel, GXcopy, AllPlanes);

  XSetState(bx_x_display, gc_inv, black_pixel, white_pixel, GXinvert, AllPlanes);

  // Create the VGA font
  create_internal_vga_font();
  ximage = create_x11_image(x_tilesize, y_tilesize);
  imDepth = default_depth;
  imWide = ximage->bytes_per_line;
  imBPP = ximage->bits_per_pixel;

  curr_background = 0;
  XSetBackground(bx_x_display, gc, col_vals[curr_background]);
  curr_foreground = 1;
  XSetForeground(bx_x_display, gc, col_vals[curr_foreground]);

  //XGrabPointer( bx_x_display, win, True, 0, GrabModeAsync, GrabModeAsync,
  //  win, None, CurrentTime );

  load_keyboard_mapping();

  XMapWindow(bx_x_display, win);
  XSync(bx_x_display, False);
  check_x11_error();
  new_gfx_api = 1;
  x_init_done = true;
}
catch(...)
{
  exit_impl();
  throw;
}

void bx_x11_gui_c::mouse_enabled_changed_impl(bool val)
{
  if(!val)
  {
    const bool was_captured = mouse_captured;
    mouse_captured = mouse_warp_pending = mouse_position_valid = false;
    mouse_buttons = ignored_mouse_buttons = 0;
    mouse_remainder_x = mouse_remainder_y = 0.0;
    mouse_sync_pending = true;
    if(bx_x_display)
    {
      if(keyboard_grabbed) XUngrabKeyboard(bx_x_display, CurrentTime);
      mouse_input_serial = XNextRequest(bx_x_display);
      XUngrabPointer(bx_x_display, CurrentTime);
      if(mouse_absolute && was_captured && win && !window_destroyed)
        XUndefineCursor(bx_x_display, win);
      XFlush(bx_x_display);
    }
    keyboard_grabbed = false;
    if(was_captured) update_mouse_title();
    return;
  }
  if(mouse_captured || !bx_x_display || !win || window_destroyed || retired ||
     input_paused || input_detached || !guest_keyboard || !keyboard_focused) return;
  mouse_remainder_x = mouse_remainder_y = 0.0;
  Window focus;
  int revert;
  XGetInputFocus(bx_x_display, &focus, &revert);
  XWindowAttributes attributes = {};
  if((focus != win && focus != PointerRoot) ||
     !XGetWindowAttributes(bx_x_display, win, &attributes) || attributes.map_state != IsViewable)
    return;
  if(hidden_cursor == None)
  {
    const char empty = 0;
    const Pixmap bitmap = XCreateBitmapFromData(bx_x_display, win, &empty, 1, 1);
    if(bitmap)
    {
      XColor color = {};
      hidden_cursor = XCreatePixmapCursor(bx_x_display, bitmap, bitmap, &color, &color, 0, 0);
      XFreePixmap(bx_x_display, bitmap);
    }
    if(!hidden_cursor)
    {
      printf("%%X11-W-MOUSE: Unable to create the capture cursor.\n");
      return;
    }
  }
  if(!mouse_absolute)
  {
    const int result = XGrabPointer(bx_x_display, win, False,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, win, hidden_cursor, CurrentTime);
    if(result != GrabSuccess)
    {
      printf("%%X11-W-MOUSE: Unable to capture the mouse (X grab status %d).\n", result);
      return;
    }
  }
  mouse_captured = true;
  mouse_sync_pending = true;
  try
  {
    if(mouse_absolute)
    {
      XUngrabPointer(bx_x_display, CurrentTime);
      XDefineCursor(bx_x_display, win, hidden_cursor);
    }
    const int keyboard_result = XGrabKeyboard(bx_x_display, win, False,
        GrabModeAsync, GrabModeAsync, CurrentTime);
    keyboard_grabbed = keyboard_result == GrabSuccess;
    if(!keyboard_grabbed)
      printf("%%X11-W-KEYBOARD: Unable to capture system shortcuts (X grab status %d).\n",
             keyboard_result);
    Window root, child;
    int root_x, root_y;
    unsigned buttons;
    const unsigned long pointer_serial = XNextRequest(bx_x_display);
    if(!XQueryPointer(bx_x_display, win, &root, &child, &root_x, &root_y,
                      &mouse_x, &mouse_y, &buttons) || x_error)
    {
      mouse_enabled_changed_impl(false);
      check_x11_error();
      return;
    }
    ignored_mouse_buttons = ((buttons & Button1Mask) ? 1 : 0) |
                            ((buttons & Button3Mask) ? 2 : 0) |
                            ((buttons & Button2Mask) ? 4 : 0);
    mouse_buttons = 0;
    mouse_position_valid = mouse_warp_pending = false;
    recenter_mouse();
    mouse_input_serial = mouse_motion_serial = mouse_absolute ? pointer_serial : mouse_warp_serial;
    update_mouse_title();
  }
  catch(...)
  {
    try { mouse_enabled_changed_impl(false); } catch(...) {}
    throw;
  }
}

void bx_x11_gui_c::sync_mouse_capture()
{
  if(!mouse_sync_pending || input_paused || input_detached || !guest_keyboard) return;
  // Deliver button release before the guest stops accepting mouse packets.
  if(guest_mouse_captured) guest_keyboard->mouse_motion(0, 0, 0, 0);
  guest_keyboard->set_mouse_capture(mouse_captured);
  guest_mouse_captured = mouse_captured;
  mouse_sync_pending = false;
}

void bx_x11_gui_c::check_mouse_capture()
{
  if(!mouse_captured || !bx_x_display) return;
  Window focus;
  int revert;
  XGetInputFocus(bx_x_display, &focus, &revert);
  XWindowAttributes attributes = {};
  if((focus != win && focus != PointerRoot) ||
     !XGetWindowAttributes(bx_x_display, win, &attributes) || attributes.map_state != IsViewable)
    mouse_enabled_changed_impl(false);
}

void bx_x11_gui_c::recenter_mouse()
{
  if(mouse_absolute || !mouse_captured || mouse_warp_pending || !client_width || !client_height) return;
  const int x = (int)(client_width / 2), y = (int)(client_height / 2);
  if(mouse_position_valid && mouse_x == x && mouse_y == y) return;
  mouse_warp_x = x;
  mouse_warp_y = y;
  mouse_warp_serial = XNextRequest(bx_x_display);
  mouse_warp_pending = true;
  XWarpPointer(bx_x_display, None, win, 0, 0, 0, 0, x, y);
}

void bx_x11_gui_c::reset_absolute_mouse_position()
{
  if(!mouse_absolute) return;
  mouse_position_valid = false;
  if(mouse_captured && bx_x_display)
  {
    mouse_motion_serial = XNextRequest(bx_x_display);
    XNoOp(bx_x_display);
  }
}

void bx_x11_gui_c::reconcile_mouse_buttons()
{
  if(!mouse_captured || input_paused || input_detached ||
     !guest_keyboard || !keyboard_focused || retired || XPending(bx_x_display)) return;
  Window root, child;
  int root_x, root_y, x, y;
  unsigned state;
  if(!XQueryPointer(bx_x_display, win, &root, &child, &root_x, &root_y, &x, &y, &state) ||
     x_error || XPending(bx_x_display)) return;
  const unsigned physical = ((state & Button1Mask) ? 1 : 0) |
                            ((state & Button3Mask) ? 2 : 0) |
                            ((state & Button2Mask) ? 4 : 0);
  ignored_mouse_buttons &= physical;
  const unsigned held = mouse_buttons & physical;
  if(held != mouse_buttons)
  {
    sync_mouse_capture();
    guest_keyboard->mouse_motion(0, 0, 0, held);
    mouse_buttons = held;
  }
}

void bx_x11_gui_c::mouse_button(XButtonEvent* event, bool released)
{
  const unsigned bit = event->button == Button1 ? 1 : event->button == Button3 ? 2 :
                       event->button == Button2 ? 4 : 0;
  if(event->serial < mouse_input_serial)
  {
    if(released) ignored_mouse_buttons &= ~bit;
    return;
  }
  if(input_paused || input_detached || !guest_keyboard || !keyboard_focused || retired) return;
  if(mouse_absolute && !released && (event->x < 0 || event->y < 0 ||
     (unsigned)event->x >= client_width || (unsigned)event->y >= client_height))
  {
    XUngrabPointer(bx_x_display, CurrentTime);
    return;
  }
  if(!mouse_captured)
  {
    if(!released && event->button == Button1)
    {
      mouse_enabled_changed_impl(true);
      if(mouse_captured) ignored_mouse_buttons |= bit;
    }
    return;
  }
  // Cancel X11's implicit button grab so absolute input can leave the window.
  if(mouse_absolute && !released) XUngrabPointer(bx_x_display, CurrentTime);
  sync_mouse_capture();
  if(ignored_mouse_buttons & bit)
  {
    if(released) ignored_mouse_buttons &= ~bit;
    return;
  }
  const unsigned buttons = released ? mouse_buttons & ~bit : mouse_buttons | bit;
  const int wheel = released ? 0 : event->button == Button4 ? 1 : event->button == Button5 ? -1 : 0;
  if(buttons != mouse_buttons || wheel)
    guest_keyboard->mouse_motion(0, 0, wheel, buttons);
  mouse_buttons = buttons;
}

void bx_x11_gui_c::mouse_motion(XMotionEvent* event)
{
  if(!mouse_captured || event->serial < mouse_input_serial ||
     event->serial < mouse_motion_serial || input_paused ||
     input_detached || !guest_keyboard || !keyboard_focused || retired) return;
  if(mouse_absolute && (event->x < 0 || event->y < 0 ||
     (unsigned)event->x >= client_width || (unsigned)event->y >= client_height))
  {
    mouse_position_valid = false;
    return;
  }
  if(mouse_warp_pending && event->serial >= mouse_warp_serial)
  {
    // Rebase even if the synthetic center event was coalesced.
    mouse_x = mouse_warp_x;
    mouse_y = mouse_warp_y;
    mouse_position_valid = true;
    mouse_warp_pending = false;
  }
  if(mouse_position_valid)
    send_mouse_motion((double)event->x - mouse_x, (double)mouse_y - event->y);
  mouse_x = event->x;
  mouse_y = event->y;
  mouse_position_valid = true;
}

void bx_x11_gui_c::send_mouse_motion(double dx, double dy)
{
  if(!mouse_captured || input_paused || input_detached || !guest_keyboard ||
     !keyboard_focused || retired) return;
  mouse_remainder_x += dx * (mouse_invert_x ? -mouse_speed : mouse_speed);
  mouse_remainder_y += dy * (mouse_invert_y ? -mouse_speed : mouse_speed);
  const double minimum = (std::numeric_limits<int>::min)();
  const double maximum = (std::numeric_limits<int>::max)();
  mouse_remainder_x = (std::max)(minimum, (std::min)(maximum, mouse_remainder_x));
  mouse_remainder_y = (std::max)(minimum, (std::min)(maximum, mouse_remainder_y));
  const int scaled_x = (int)mouse_remainder_x, scaled_y = (int)mouse_remainder_y;
  mouse_remainder_x -= scaled_x;
  mouse_remainder_y -= scaled_y;
  sync_mouse_capture();
  if(scaled_x || scaled_y) guest_keyboard->mouse_motion(scaled_x, scaled_y, 0, mouse_buttons);
}

/**
 * Create a bitmap for VGA font data
 **/
void create_internal_vga_font(void)
{

  // Default values
  font_width = 8;
  font_height = 16;

  for(int i = 0; i < 256; i++)
  {
    vgafont[i] = XCreateBitmapFromData(bx_x_display, win,
                                       (const char*) bx_vgafont[i].data,
                                       font_width, font_height);
    if(vgafont[i] == None)
      FAILURE(OutOfMemory, "Unable to create X11 VGA font");
  }
}

// Leave input queued during pause while allowing repaint and close events.
static Bool x11_lifecycle_event(Display*, XEvent* event, XPointer)
{
  switch(event->type)
  {
  case KeyPress: case KeyRelease:
  case ButtonPress: case ButtonRelease:
  case MotionNotify: case EnterNotify: case LeaveNotify:
  case FocusIn: case FocusOut: case MappingNotify:
    return False;
  default:
    return True;
  }
}

void bx_x11_gui_c::poll_events(bool process_input)
{
  if(!bx_x_display) return;
  check_x11_error();
  check_mouse_capture();
  if(process_input) sync_mouse_capture();
  XEvent                report;

  // Bound polling so worker calls and barriers cannot starve.
  for(unsigned count = 0; count < 256; ++count)
  {
    if(!process_input || input_paused || input_detached)
    {
      if(!XCheckIfEvent(bx_x_display, &report, x11_lifecycle_event, nullptr)) break;
    }
    else
    {
      if(!XPending(bx_x_display)) break;
      XNextEvent(bx_x_display, &report);
    }
    if(report.type == MappingNotify)
    {
      XRefreshKeyboardMapping(&report.xmapping);
      load_hotkeys();
      continue;
    }
    if(report.xany.window != win) continue;
    switch(report.type)
    {
    case Expose:
      if(frame_active)
      {
        if(report.xexpose.count == 0) present_frame();
        break;
      }
      legacy_redraw_pending = true;
      break;

    case ConfigureNotify:
      configure_window(report.xconfigure.width, report.xconfigure.height);
      reset_absolute_mouse_position();
      if(mouse_captured && !mouse_absolute)
      {
        mouse_position_valid = mouse_warp_pending = false;
        recenter_mouse();
        mouse_motion_serial = mouse_warp_serial;
      }
      if(reset_window_pending && reset_stage == reset_phase::unmaximize &&
         report.xconfigure.serial >= reset_configure_serial)
        reset_configure_seen = true;
      window_state_dirty = true;
      break;

    case PropertyNotify:
      if(report.xproperty.atom == wm_state_atom || report.xproperty.atom == net_wm_state_atom)
        window_state_dirty = true;
      break;

    case UnmapNotify:
      mouse_enabled_changed_impl(false);
      window_state_dirty = true;
      break;

    case ButtonPress:
    case ButtonRelease:
      mouse_button(&report.xbutton, report.type == ButtonRelease);
      break;

    case KeyPress:
      dispatch_keypress(&report.xkey);
      break;

    case KeyRelease:
      if(!detectable_repeat && XEventsQueued(bx_x_display, QueuedAfterReading))
      {
        XEvent next;
        XPeekEvent(bx_x_display, &next);
        if(next.type == KeyPress && next.xkey.window == report.xkey.window &&
           next.xkey.keycode == report.xkey.keycode && next.xkey.time == report.xkey.time)
          break;
      }
      keypress(&report.xkey, true);
      break;

    case FocusOut:
      if(report.xfocus.mode != NotifyGrab && report.xfocus.mode != NotifyUngrab &&
         report.xfocus.detail != NotifyInferior)
      {
        keyboard_focused = false;
        mouse_enabled_changed_impl(false);
        release_keys();
      }
      break;

    case FocusIn:
      if(report.xfocus.mode != NotifyGrab && report.xfocus.mode != NotifyUngrab)
        keyboard_focused = true;
      break;

    case MotionNotify:
      mouse_motion(&report.xmotion);
      break;

    case EnterNotify:
    case LeaveNotify:
      if(mouse_absolute && report.xcrossing.mode == NotifyNormal)
        mouse_position_valid = false;
      break;

    case MapNotify:
      window_state_dirty = true;
      if(frame_active) present_frame();
      else legacy_redraw_pending = true;

      /* screen needs redraw, since X would have tossed previous
       * requests before window mapped
       */

      //retval = 1;
      break;

    case ClientMessage:
      if(report.xclient.message_type == wm_protocols && report.xclient.format == 32 &&
         (Atom)report.xclient.data.l[0] == wm_delete_window)
      {
        mouse_enabled_changed_impl(false);
        if(guest_system) guest_system->RequestShutdown();
      }
      break;

    case DestroyNotify:
      if(report.xdestroywindow.window == win)
      {
        window_destroyed = true;
        mouse_enabled_changed_impl(false);
        if(guest_system) guest_system->RequestShutdown();
      }
      break;

    default:

      // (mch) Ignore...
      BX_DEBUG(("XXX: default Xevent type"));

      /* all events selected by StructureNotifyMask are thrown away here,
       * since nothing is done with them */
      break;
    }           /* end switch */
  }             /* end while */

  recenter_mouse();
  reconcile_keyboard_releases(process_input);
  if(window_state_dirty || pending_guest_resize || pending_window_hints ||
     fullscreen_transition_pending || reset_window_pending) update_window_state();
  if(process_input)
  {
    sync_mouse_capture();
    reconcile_mouse_buttons();
  }
  check_x11_error();
}

void bx_x11_gui_c::flush(void)
{
  invoke([this] {
    if(event_error) std::rethrow_exception(event_error);
    if(bx_x_display) XFlush(bx_x_display);
    check_x11_error();
  });
}

static unsigned x11_host_modifier(KeySym symbol)
{
  switch(symbol)
  {
  case XK_Control_L: case XK_Control_R: return X11_HOTKEY_CTRL;
  case XK_Alt_L: case XK_Alt_R: return X11_HOTKEY_ALT;
  case XK_Shift_L: case XK_Shift_R: return X11_HOTKEY_SHIFT;
  case XK_Meta_L: case XK_Meta_R: case XK_Super_L: case XK_Super_R:
  case XK_Hyper_L: case XK_Hyper_R: return X11_HOTKEY_GUI;
  case XK_Mode_switch: case XK_ISO_Level3_Shift: case XK_ISO_Level5_Shift:
    return X11_HOTKEY_UNSUPPORTED;
  default: return 0;
  }
}

static KeySym x11_key_symbol(unsigned code, unsigned group, unsigned level)
{
  const KeySym symbol = XkbKeycodeToKeysym(bx_x_display, (KeyCode)code, group, level);
  if(symbol != NoSymbol || group >= 2 || level >= 2) return symbol;
  XKeyEvent event = {};
  event.display = bx_x_display;
  event.keycode = code;
  return XLookupKeysym(&event, (int)(group * 2 + level));
}

static KeySym x11_hotkey_symbol(unsigned code, unsigned group = 0)
{
  KeySym symbol = x11_key_symbol(code, group, 0);
  if(symbol == NoSymbol)
  {
    group = 0;
    symbol = x11_key_symbol(code, group, 0);
  }
  const KeySym shifted = x11_key_symbol(code, group, 1);
  if(shifted >= XK_0 && shifted <= XK_9) return shifted;
  if(symbol >= XK_A && symbol <= XK_Z) return symbol - XK_A + XK_a;
  switch(symbol)
  {
  case XK_KP_Insert: return XK_KP_0;
  case XK_KP_End: return XK_KP_1;
  case XK_KP_Down: return XK_KP_2;
  case XK_KP_Page_Down: return XK_KP_3;
  case XK_KP_Left: return XK_KP_4;
  case XK_KP_Begin: return XK_KP_5;
  case XK_KP_Right: return XK_KP_6;
  case XK_KP_Home: return XK_KP_7;
  case XK_KP_Up: return XK_KP_8;
  case XK_KP_Page_Up: return XK_KP_9;
  case XK_KP_Delete: return XK_KP_Decimal;
  case XK_ISO_Left_Tab: return XK_Tab;
  case XK_Sys_Req: return XK_Print;
  case XK_Break: return XK_Pause;
  default: return symbol;
  }
}

static x11_hotkey_key resolve_x11_hotkey(const std::string& configured)
{
  std::string name = configured;
  const struct { const char* alias; const char* name; } aliases[] = {
    { "Enter", "Return" }, { "Esc", "Escape" }, { "Del", "Delete" },
    { "Ins", "Insert" }, { "PgUp", "PageUp" }, { "PgDn", "PageDown" },
    { "PageDn", "PageDown" }, { "KpPlus", "Keypad +" },
    { "KeypadPlus", "Keypad +" }, { "KpMinus", "Keypad -" },
    { "KeypadMinus", "Keypad -" }
  };
  for(const auto& alias : aliases)
    if(x11_hotkey_equal(name, alias.alias)) { name = alias.name; break; }
  const struct { KeySym key; const char* name; } names[] = {
    { XK_Return, "Return" }, { XK_Escape, "Escape" }, { XK_BackSpace, "Backspace" },
    { XK_Tab, "Tab" }, { XK_space, "Space" }, { XK_Caps_Lock, "CapsLock" },
    { XK_Print, "PrintScreen" }, { XK_Scroll_Lock, "ScrollLock" }, { XK_Pause, "Pause" },
    { XK_Insert, "Insert" }, { XK_Home, "Home" }, { XK_Page_Up, "PageUp" },
    { XK_Delete, "Delete" }, { XK_End, "End" }, { XK_Page_Down, "PageDown" },
    { XK_Right, "Right" }, { XK_Left, "Left" }, { XK_Down, "Down" }, { XK_Up, "Up" },
    { XK_Num_Lock, "Numlock" }, { XK_KP_Divide, "Keypad /" },
    { XK_KP_Multiply, "Keypad *" }, { XK_KP_Subtract, "Keypad -" },
    { XK_KP_Add, "Keypad +" }, { XK_KP_Enter, "Keypad Enter" },
    { XK_KP_Decimal, "Keypad ." }, { XK_KP_Equal, "Keypad =" }
  };
  for(const auto& named : names)
    if(x11_hotkey_equal(name, named.name)) return { (u32)named.key, named.name };
  for(unsigned i = 0; i < 24; ++i)
  {
    const std::string label = "F" + std::to_string(i + 1);
    if(x11_hotkey_equal(name, label.c_str())) return { XK_F1 + i, label };
  }
  for(unsigned i = 0; i < 10; ++i)
  {
    const std::string label = "Keypad " + std::to_string(i);
    if(x11_hotkey_equal(name, label.c_str())) return { XK_KP_0 + i, label };
  }
  if(name.size() != 1 || (unsigned char)name[0] < 33 || (unsigned char)name[0] > 126)
    return {};
  KeySym symbol = (unsigned char)std::tolower((unsigned char)name[0]);
  const KeyCode code = XKeysymToKeycode(bx_x_display, symbol);
  if(code)
    for(unsigned group = 0; group < 4; ++group)
    {
      const KeySym base = x11_key_symbol(code, group, 0);
      const KeySym shifted = x11_key_symbol(code, group, 1);
      if(base == symbol || shifted == symbol)
      {
        symbol = x11_hotkey_symbol(code, group);
        break;
      }
    }
  if(symbol < XK_space || symbol > XK_asciitilde) return {};
  return { (u32)symbol, std::string(1, (char)std::toupper((unsigned char)symbol)) };
}

void bx_x11_gui_c::refresh_hotkey_modifiers()
{
  XModifierKeymap* mapping = XGetModifierMapping(bx_x_display);
  if(!mapping) FAILURE(Runtime, "Unable to read X11 keyboard modifiers");
  for(unsigned index = 0; index < 8; ++index)
  {
    unsigned modifiers = 0;
    bool lock = index == 1;
    for(int slot = 0; slot < mapping->max_keypermod; ++slot)
    {
      const KeyCode code = mapping->modifiermap[index * mapping->max_keypermod + slot];
      if(!code) continue;
      for(unsigned level = 0; level < 4; ++level)
      {
        const KeySym symbol = x11_key_symbol(code, 0, level);
        modifiers |= x11_host_modifier(symbol);
        lock = lock || symbol == XK_Caps_Lock || symbol == XK_Shift_Lock ||
                       symbol == XK_Num_Lock || symbol == XK_Scroll_Lock;
      }
    }
    if(modifiers & X11_HOTKEY_ALT) modifiers &= ~X11_HOTKEY_GUI;
    hotkey_modifier_masks[index] = modifiers ? modifiers : lock ? 0 : X11_HOTKEY_UNSUPPORTED;
  }
  hotkey_modifier_masks[0] = X11_HOTKEY_SHIFT;
  hotkey_modifier_masks[2] = X11_HOTKEY_CTRL;
  for(auto& modifiers : hotkey_key_modifiers) modifiers = 0;
  for(unsigned index = 0; index < 8; ++index)
    for(int slot = 0; slot < mapping->max_keypermod; ++slot)
    {
      const KeyCode code = mapping->modifiermap[index * mapping->max_keypermod + slot];
      if(code) hotkey_key_modifiers[code] |= hotkey_modifier_masks[index];
    }
  XFreeModifiermap(mapping);
}

unsigned bx_x11_gui_c::hotkey_modifiers(unsigned state) const
{
  unsigned modifiers = 0;
  for(unsigned index = 0; index < 8; ++index)
    if(state & (1u << index)) modifiers |= hotkey_modifier_masks[index];
  return modifiers;
}

void bx_x11_gui_c::load_hotkeys()
{
  refresh_hotkey_modifiers();
  auto load = [this](const char* name, const char* fallback) {
    const char* configured = myCfg->get_text_value(name, fallback);
    const auto binding = parse_x11_hotkey(configured, resolve_x11_hotkey);
    if(binding.invalid)
      printf("%%X11-W-HOTKEY: %s has invalid binding \"%s\"; action disabled.\n",
             name, configured ? configured : "");
    return binding;
  };
  hotkey_ctrl_alt_delete = load("hotkey.ctrl_alt_delete", "Ctrl+Alt+End");
  hotkey_reset_window = load("hotkey.reset_window", "Ctrl+Alt+Home");
  hotkey_scale_up = load("hotkey.scale_up", "Ctrl+PageUp");
  hotkey_scale_down = load("hotkey.scale_down", "Ctrl+PageDown");
  hotkey_mouse_capture = load("hotkey.mouse_capture", "Ctrl+F10");
  x11_hotkey_binding* bindings[] = {
    &hotkey_ctrl_alt_delete, &hotkey_reset_window, &hotkey_scale_up, &hotkey_scale_down,
    &hotkey_mouse_capture
  };
  const char* names[] = {
    "hotkey.ctrl_alt_delete", "hotkey.reset_window", "hotkey.scale_up", "hotkey.scale_down",
    "hotkey.mouse_capture"
  };
  const bool active[] = {
    hotkey_ctrl_alt_delete.enabled, hotkey_reset_window.enabled,
    hotkey_scale_up.enabled && scale_change_enable, hotkey_scale_down.enabled && scale_change_enable,
    hotkey_mouse_capture.enabled
  };
  constexpr size_t count = sizeof(bindings) / sizeof(bindings[0]);
  bool duplicate[count] = {};
  for(size_t i = 0; i < count; ++i)
    for(size_t j = i + 1; j < count; ++j)
      if(active[i] && active[j] && bindings[i]->key == bindings[j]->key &&
         bindings[i]->modifiers == bindings[j]->modifiers)
      {
        printf("%%X11-W-HOTKEY: %s and %s both use \"%s\"; both actions disabled.\n",
               names[i], names[j], bindings[i]->display.c_str());
        duplicate[i] = duplicate[j] = true;
      }
  for(size_t i = 0; i < count; ++i)
    if(duplicate[i]) bindings[i]->enabled = false;
  update_mouse_title();
}

void bx_x11_gui_c::update_mouse_title()
{
  if(!bx_x_display || !win || window_destroyed || retired) return;
  std::string title = "ES40 Emulator";
  if(!mouse_captured) title += " - Click to capture mouse";
  if(hotkey_mouse_capture.enabled)
    title += " - " + hotkey_mouse_capture.display +
             (mouse_captured ? " releases mouse" : " captures mouse");
  if(hotkey_ctrl_alt_delete.enabled)
    title += " - " + hotkey_ctrl_alt_delete.display + " sends C+A+Del";
  if(hotkey_reset_window.enabled)
    title += " - " + hotkey_reset_window.display + " resets window";
  if(scale_change_enable)
  {
    if(hotkey_scale_up.enabled)
      title += " - " + hotkey_scale_up.display + " increases scale";
    if(hotkey_scale_down.enabled)
      title += " - " + hotkey_scale_down.display + " decreases scale";
  }
  title += " - Alt+Enter fullscreen";
  XStoreName(bx_x_display, win, title.c_str());
}

static bool x11_modifier_key(KeySym symbol, u32 guest)
{
  return symbol == XK_Control_L || symbol == XK_Control_R ||
         symbol == XK_Shift_L || symbol == XK_Shift_R ||
         symbol == XK_Alt_L || symbol == XK_Alt_R ||
         symbol == XK_Meta_L || symbol == XK_Meta_R ||
         symbol == XK_Super_L || symbol == XK_Super_R ||
         guest == BX_KEY_CTRL_L || guest == BX_KEY_CTRL_R ||
         guest == BX_KEY_SHIFT_L || guest == BX_KEY_SHIFT_R ||
         guest == BX_KEY_ALT_L || guest == BX_KEY_ALT_R ||
         guest == BX_KEY_WIN_L || guest == BX_KEY_WIN_R;
}

bool bx_x11_gui_c::guest_key_held(u32 key, unsigned except) const
{
  for(unsigned i = 0; i < 256; ++i)
    if(i != except && pressed_keys[i].down && pressed_keys[i].guest == key)
      return true;
  return false;
}

void bx_x11_gui_c::send_ctrl_alt_delete(unsigned code)
{
  pressed_keys[code].down = true;
  pressed_keys[code].suppressed = true;
  for(unsigned i = 8; i < 256; ++i)
  {
    auto& held = pressed_keys[i];
    if(!held.down) continue;
    if(!(held.host_modifiers & hotkey_ctrl_alt_delete.modifiers) &&
       held.guest != BX_KEY_CTRL_L && held.guest != BX_KEY_CTRL_R &&
       held.guest != BX_KEY_ALT_L && held.guest != BX_KEY_ALT_R &&
       held.guest != BX_KEY_DELETE) continue;
    if(held.guest != BX_KEY_UNHANDLED && !guest_key_held(held.guest, i))
      guest_keyboard->gen_scancode(held.guest | BX_KEY_RELEASED);
    held.guest = BX_KEY_UNHANDLED;
    held.suppressed = true;
  }
  const u32 chord[] = { BX_KEY_CTRL_L, BX_KEY_ALT_L, BX_KEY_DELETE,
                       BX_KEY_DELETE | BX_KEY_RELEASED, BX_KEY_ALT_L | BX_KEY_RELEASED,
                       BX_KEY_CTRL_L | BX_KEY_RELEASED };
  for(u32 key : chord)
    guest_keyboard->gen_scancode(key);
}

void bx_x11_gui_c::reconcile_keyboard_releases(bool process_input)
{
  if(!bx_x_display || input_detached || retired) return;
  const bool live = process_input && !input_paused && guest_keyboard;
  bool pending = false;
  for(const auto& key : pressed_keys)
    pending = pending || (key.down && (live || key.suppressed));
  if(!pending || XPending(bx_x_display)) return;
  char physical[32] = {};
  const unsigned long serial = XNextRequest(bx_x_display);
  if(!XQueryKeymap(bx_x_display, physical) || x_error || XPending(bx_x_display)) return;
  for(unsigned code = 8; code < 256; ++code)
  {
    auto& key = pressed_keys[code];
    if(!key.down || (!live && !key.suppressed) ||
       (physical[code / 8] & (1u << (code % 8)))) continue;
    if(key.guest != BX_KEY_UNHANDLED)
    {
      if(!live) continue;
      if(!guest_key_held(key.guest, code))
        guest_keyboard->gen_scancode(key.guest | BX_KEY_RELEASED);
    }
    key = {};
    key_release_serial[code] = serial;
  }
  CTRL_pressed = false;
  for(const auto& key : pressed_keys)
    if(key.down && key.control) CTRL_pressed = true;
}

void bx_x11_gui_c::dispatch_keypress(XKeyEvent* event)
{
  const bool escape = keyboard_grabbed && keyboard_focused && !input_paused &&
      !input_detached && !retired && guest_keyboard && event->keycode >= 8 &&
      event->keycode < 256 && event->serial >= key_release_serial[event->keycode] &&
      !pressed_keys[event->keycode].down &&
      x11_hotkey_symbol(event->keycode, XkbGroupForCoreState(event->state)) == XK_Tab &&
      (hotkey_modifiers(event->state) & X11_HOTKEY_ALT);
  keypress(event, false);
  if(!escape || reset_window_pending ||
     (fullscreen_transition_pending && !fullscreen_requested) ||
     !read_window_state() || !window_fullscreen) return;
  suppress_guest_keys();
  mouse_enabled_changed_impl(false);
  sync_mouse_capture();
  if(!XIconifyWindow(bx_x_display, win, bx_x_screen_num))
    printf("%%X11-W-FULLSCREEN: Unable to request fullscreen minimization.\n");
  window_state_dirty = true;
}

void bx_x11_gui_c::suppress_guest_keys()
{
  if(!guest_keyboard || input_paused || input_detached) return;
  for(unsigned code = 8; code < 256; ++code)
  {
    auto& key = pressed_keys[code];
    if(!key.down) continue;
    if(key.guest != BX_KEY_UNHANDLED && !guest_key_held(key.guest, code))
      guest_keyboard->gen_scancode(key.guest | BX_KEY_RELEASED);
    key.guest = BX_KEY_UNHANDLED;
    key.suppressed = true;
    key.scale_delta = 0;
  }
}

void bx_x11_gui_c::keypress(XKeyEvent* event, bool released)
{
  if(!guest_keyboard || input_paused || input_detached ||
     event->keycode < 8 || event->keycode >= 256) return;
  if(event->serial < key_release_serial[event->keycode]) return;
  auto& key = pressed_keys[event->keycode];
  if(released)
  {
    if(!key.down) return;
    if(key.guest != BX_KEY_UNHANDLED && !guest_key_held(key.guest, event->keycode))
      guest_keyboard->gen_scancode(key.guest | BX_KEY_RELEASED);
    key = {};
    CTRL_pressed = false;
    for(const auto& held : pressed_keys)
      if(held.down && held.control) CTRL_pressed = true;
    return;
  }
  if(!keyboard_focused) return;
  if(key.down)
  {
    if(key.scale_delta && scale_change_enable)
    {
      const auto& binding = key.scale_delta > 0 ? hotkey_scale_up : hotkey_scale_down;
      if(binding.matches((u32)x11_hotkey_symbol(event->keycode, XkbGroupForCoreState(event->state)),
                         hotkey_modifiers(event->state)))
        adjust_window_scale(key.scale_delta);
    }
    if(key.guest != BX_KEY_UNHANDLED && !key.modifier)
      guest_keyboard->gen_scancode(key.guest);
    return;
  }

  const u32 host_key = (u32)x11_hotkey_symbol(event->keycode, XkbGroupForCoreState(event->state));
  const unsigned modifiers = hotkey_modifiers(event->state);
  if(hotkey_ctrl_alt_delete.matches(host_key, modifiers))
  {
    send_ctrl_alt_delete(event->keycode);
    return;
  }
  if(hotkey_reset_window.matches(host_key, modifiers))
  {
    key.down = key.suppressed = true;
    reset_window_size();
    return;
  }
  if(scale_change_enable && (hotkey_scale_up.matches(host_key, modifiers) ||
                            hotkey_scale_down.matches(host_key, modifiers)))
  {
    key.down = key.suppressed = true;
    key.scale_delta = hotkey_scale_up.matches(host_key, modifiers) ? 1 : -1;
    adjust_window_scale(key.scale_delta);
    return;
  }
  if(hotkey_mouse_capture.matches(host_key, modifiers))
  {
    key.down = key.suppressed = true;
    for(unsigned code = 8; code < 256; ++code)
    {
      auto& held = pressed_keys[code];
      if(!held.down || !(held.host_modifiers & hotkey_mouse_capture.modifiers)) continue;
      if(held.guest != BX_KEY_UNHANDLED && !guest_key_held(held.guest, code))
        guest_keyboard->gen_scancode(held.guest | BX_KEY_RELEASED);
      held.guest = BX_KEY_UNHANDLED;
      held.suppressed = true;
    }
    mouse_enabled_changed_impl(!mouse_captured);
    return;
  }
  if((host_key == XK_Return || host_key == XK_KP_Enter) && (modifiers & X11_HOTKEY_ALT))
  {
    key.down = key.suppressed = true;
    toggle_fullscreen();
    return;
  }

  KeySym symbol = NoSymbol;
  char buffer[MAX_MAPPED_STRING_LENGTH];
  XLookupString(event, buffer, sizeof(buffer), &symbol, nullptr);
  const u32 guest = translate_key(symbol, host_key);
  if(guest != BX_KEY_UNHANDLED && !guest_key_held(guest))
    guest_keyboard->gen_scancode(guest);
  key.guest = guest;
  key.down = true;
  key.host_modifiers = hotkey_key_modifiers[event->keycode];
  key.modifier = key.host_modifiers != 0 || x11_modifier_key(symbol, guest);
  key.control = symbol == XK_Control_L || symbol == XK_Control_R;
  CTRL_pressed = CTRL_pressed || key.control;
}

void bx_x11_gui_c::release_keys()
{
  for(unsigned code = 8; code < 256; ++code)
  {
    pressed_keys[code].scale_delta = 0;
    if(pressed_keys[code].down && !pressed_keys[code].suppressed)
    {
      XKeyEvent event = {};
      event.keycode = code;
      event.serial = key_release_serial[code];
      keypress(&event, true);
    }
  }
}

u32 bx_x11_gui_c::translate_key(KeySym keysym, KeySym sdl_symbol)
{
  u32 key_event;

  /* Old (no mapping) behavior */
  if(!x_keymapping)
  {

    //if (!SIM->get_param_bool(BXPN_KBD_USEMAPPING)->get()) {
    // this depends on the fact that the X11 keysyms which
    // correspond to the ascii characters space .. tilde
    // are in consequtive order.
    if((keysym >= XK_space) && (keysym <= XK_asciitilde))
    {
      key_event = ascii_to_key_event[keysym - XK_space];
    }
    else
    {
      switch(keysym)
      {
      case XK_KP_1:
#ifdef XK_KP_End

      case XK_KP_End:
#endif
        key_event = BX_KEY_KP_END;
        break;

      case XK_KP_2:
#ifdef XK_KP_Down

      case XK_KP_Down:
#endif
        key_event = BX_KEY_KP_DOWN;
        break;

      case XK_KP_3:
#ifdef XK_KP_Page_Down

      case XK_KP_Page_Down:
#endif
        key_event = BX_KEY_KP_PAGE_DOWN;
        break;

      case XK_KP_4:
#ifdef XK_KP_Left

      case XK_KP_Left:
#endif
        key_event = BX_KEY_KP_LEFT;
        break;

      case XK_KP_5:
#ifdef XK_KP_Begin

      case XK_KP_Begin:
#endif
        key_event = BX_KEY_KP_5;
        break;

      case XK_KP_6:
#ifdef XK_KP_Right

      case XK_KP_Right:
#endif
        key_event = BX_KEY_KP_RIGHT;
        break;

      case XK_KP_7:
#ifdef XK_KP_Home

      case XK_KP_Home:
#endif
        key_event = BX_KEY_KP_HOME;
        break;

      case XK_KP_8:
#ifdef XK_KP_Up

      case XK_KP_Up:
#endif
        key_event = BX_KEY_KP_UP;
        break;

      case XK_KP_9:
#ifdef XK_KP_Page_Up

      case XK_KP_Page_Up:
#endif
        key_event = BX_KEY_KP_PAGE_UP;
        break;

      case XK_KP_0:
#ifdef XK_KP_Insert

      case XK_KP_Insert:
#endif
        key_event = BX_KEY_KP_INSERT;
        break;

      case XK_KP_Decimal:
#ifdef XK_KP_Delete

      case XK_KP_Delete:
#endif
        key_event = BX_KEY_KP_DELETE;
        break;

#ifdef XK_KP_Enter

      case XK_KP_Enter:
        key_event = BX_KEY_KP_ENTER;
        break;
#endif

      case XK_KP_Subtract:
        key_event = BX_KEY_KP_SUBTRACT;
        break;

      case XK_KP_Add:
        key_event = BX_KEY_KP_ADD;
        break;

      case XK_KP_Multiply:
        key_event = BX_KEY_KP_MULTIPLY;
        break;

      case XK_KP_Divide:
        key_event = BX_KEY_KP_DIVIDE;
        break;

      case XK_Up:
        key_event = BX_KEY_UP;
        break;

      case XK_Down:
        key_event = BX_KEY_DOWN;
        break;

      case XK_Left:
        key_event = BX_KEY_LEFT;
        break;

      case XK_Right:
        key_event = BX_KEY_RIGHT;
        break;

      case XK_Delete:
        key_event = BX_KEY_DELETE;
        break;

      case XK_BackSpace:
        key_event = BX_KEY_BACKSPACE;
        break;

      case XK_Tab:
        key_event = BX_KEY_TAB;
        break;
#ifdef XK_ISO_Left_Tab

      case XK_ISO_Left_Tab:
        key_event = BX_KEY_TAB;
        break;
#endif

      case XK_Return:
        key_event = BX_KEY_ENTER;
        break;

      case XK_Escape:
        key_event = BX_KEY_ESC;
        break;

      case XK_F1:
        key_event = BX_KEY_F1;
        break;

      case XK_F2:
        key_event = BX_KEY_F2;
        break;

      case XK_F3:
        key_event = BX_KEY_F3;
        break;

      case XK_F4:
        key_event = BX_KEY_F4;
        break;

      case XK_F5:
        key_event = BX_KEY_F5;
        break;

      case XK_F6:
        key_event = BX_KEY_F6;
        break;

      case XK_F7:
        key_event = BX_KEY_F7;
        break;

      case XK_F8:
        key_event = BX_KEY_F8;
        break;

      case XK_F9:
        key_event = BX_KEY_F9;
        break;

      case XK_F10:
        key_event = BX_KEY_F10;
        break;

      case XK_F11:
        key_event = BX_KEY_F11;
        break;

      case XK_F12:
        key_event = BX_KEY_F12;
        break;

      case XK_Control_L:
        key_event = BX_KEY_CTRL_L;
        break;
#ifdef XK_Control_R

      case XK_Control_R:
        key_event = BX_KEY_CTRL_R;
        break;
#endif

      case XK_Shift_L:
        key_event = BX_KEY_SHIFT_L;
        break;

      case XK_Shift_R:
        key_event = BX_KEY_SHIFT_R;
        break;

      case XK_Alt_L:
        key_event = BX_KEY_ALT_L;
        break;
#ifdef XK_Alt_R

      case XK_Alt_R:
        key_event = BX_KEY_ALT_R;
        break;
#endif

      case XK_Super_L:
        key_event = BX_KEY_WIN_L;
        break;

      case XK_Super_R:
        key_event = BX_KEY_WIN_R;
        break;

      case XK_Menu:
        key_event = BX_KEY_MENU;
        break;

      case XK_Caps_Lock:
        key_event = BX_KEY_CAPS_LOCK;
        break;

      case XK_Num_Lock:
        key_event = BX_KEY_NUM_LOCK;
        break;
#ifdef XK_Scroll_Lock

      case XK_Scroll_Lock:
        key_event = BX_KEY_SCRL_LOCK;
        break;
#endif
#ifdef XK_Print

      case XK_Print:
      case XK_Sys_Req:
        key_event = BX_KEY_PRINT;
        break;
#endif
#ifdef XK_Pause

      case XK_Pause:
      case XK_Break:
        key_event = BX_KEY_PAUSE;
        break;
#endif

      case XK_Insert:
        key_event = BX_KEY_INSERT;
        break;

      case XK_Home:
        key_event = BX_KEY_HOME;
        break;

      case XK_End:
        key_event = BX_KEY_END;
        break;

      case XK_Page_Up:
        key_event = BX_KEY_PAGE_UP;
        break;

      case XK_Page_Down:
        key_event = BX_KEY_PAGE_DOWN;
        break;

      default:
        BX_ERROR(("xkeypress(): keysym %x unhandled!", (unsigned) keysym));
        return BX_KEY_UNHANDLED;
      }
    }
  }
  else
  {

    // Explicit X11 mappings take precedence over normalized SDL-style entries.
    BXKeyEntry* entry = keyboard_map->findHostKey((u32)keysym);
    if(!entry && sdl_symbol != NoSymbol)
      entry = keyboard_map->findHostKey(x11_sdl_keymap_mask | (u32)sdl_symbol);
    if(!entry)
    {
      BX_ERROR(("xkeypress(): keysym %x unhandled!", (unsigned) keysym));
      return BX_KEY_UNHANDLED;
    }

    key_event = entry->baseKey;
  }

  return key_event < BX_KEY_NBKEYS ? key_event : BX_KEY_UNHANDLED;
}

void bx_x11_gui_c::clear_screen_impl(void)
{
  if(!bx_x_display) return;
  if(frame_active && frame_image)
  {
    std::fill(frame_pixels.begin(), frame_pixels.end(), 0);
    present_frame();
    return;
  }
  XClearArea(bx_x_display, win, 0, 0, dimension_x, dimension_y, 0);
}

void bx_x11_gui_c::text_update_impl(u8*  old_text, u8*  new_text,
                               unsigned long cursor_x, unsigned long cursor_y,
                               bx_vga_tminfo_t tm_info, unsigned nrows,
                               const unsigned* palette_indices)
{
  use_legacy_presentation();
  u8*           old_line;

  u8 *new_line;

  u8 *text_base;
  u8            cChar;
  unsigned int  curs;
  unsigned int  hchars;
  unsigned int  i;
  unsigned int  j;
  unsigned int  offset;
  unsigned int  rows;
  unsigned int  x;
  unsigned int  y;
  unsigned int  xc;
  unsigned int  yc;
  unsigned int  yc2;
  unsigned int  cs_y;
  unsigned      new_foreground;
  unsigned      new_background;
  u8            cfwidth;
  u8            cfheight;
  u8            cfheight2;
  u8            font_col;
  u8            font_row;
  u8            font_row2;
  u8            split_textrow;
  u8            split_fontrows;
  bool          forceUpdate = 0;
  bool          split_screen;
  unsigned char cell[64];
  unsigned long text_palette[16];

  if(charmap_updated)
  {
    BX_INFO(("charmap update. Font Height is %d", font_height));
    for(unsigned c = 0; c < 256; c++)
    {
      if(char_changed[c])
      {
        XFreePixmap(bx_x_display, vgafont[c]);

        bool  gfxchar = tm_info.line_graphics && ((c & 0xE0) == 0xC0);
        j = 0;
        memset(cell, 0, sizeof(cell));
        for(i = 0; i < font_height * 2; i += 2)
        {
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x01) << 7);
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x02) << 5);
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x04) << 3);
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x08) << 1);
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x10) >> 1);
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x20) >> 3);
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x40) >> 5);
          cell[i] |= ((vga_charmap[(c << 5) + j] & 0x80) >> 7);
          if(gfxchar)
          {
            cell[i + 1] = (vga_charmap[(c << 5) + j] & 0x01);
          }

          j++;
        }

        vgafont[c] = XCreateBitmapFromData(bx_x_display, win, (const char*) cell,
                                           9, font_height);
        if(vgafont[c] == None)
          BX_PANIC(("Can't create vga font [%d]", c));
        char_changed[c] = 0;
      }
    }

    forceUpdate = 1;
    charmap_updated = 0;
  }

  for(i = 0; i < 16; i++)
  {
    text_palette[i] = col_vals[palette_indices[i]];
  }

  if((tm_info.h_panning != h_panning) || (tm_info.v_panning != v_panning))
  {
    forceUpdate = 1;
    h_panning = tm_info.h_panning;
    v_panning = tm_info.v_panning;
  }

  if(tm_info.line_compare != line_compare)
  {
    forceUpdate = 1;
    line_compare = tm_info.line_compare;
  }

  // first invalidate character at previous and new cursor location
  if((prev_cursor_y < text_rows) && (prev_cursor_x < text_cols))
  {
    curs = prev_cursor_y * tm_info.line_offset + prev_cursor_x * 2;
    old_text[curs] = ~new_text[curs];
  }

  if((tm_info.cs_start <= tm_info.cs_end) && (tm_info.cs_start < font_height)
   && (cursor_y < text_rows) && (cursor_x < text_cols))
  {
    curs = cursor_y * tm_info.line_offset + cursor_x * 2;
    old_text[curs] = ~new_text[curs];
  }
  else
  {
    curs = 0xffff;
  }

  rows = text_rows;
  if(v_panning)
    rows++;
  y = 0;
  cs_y = 0;
  text_base = new_text - tm_info.start_address;
  split_textrow = (line_compare + v_panning) / font_height;
  split_fontrows = ((line_compare + v_panning) % font_height) + 1;
  split_screen = 0;
  do
  {
    hchars = text_cols;
    if(h_panning)
      hchars++;
    if(split_screen)
    {
      yc = line_compare + cs_y * font_height + 1;
      font_row = 0;
      if(rows == 1)
      {
        cfheight = (dimension_y - line_compare - 1) % font_height;
        if(cfheight == 0)
          cfheight = font_height;
      }
      else
      {
        cfheight = font_height;
      }
    }
    else if(v_panning)
    {
      if(y == 0)
      {
        yc = 0;
        font_row = v_panning;
        cfheight = font_height - v_panning;
      }
      else
      {
        yc = y * font_height - v_panning;
        font_row = 0;
        if(rows == 1)
        {
          cfheight = v_panning;
        }
        else
        {
          cfheight = font_height;
        }
      }
    }
    else
    {
      yc = y * font_height;
      font_row = 0;
      cfheight = font_height;
    }

    if(!split_screen && (y == split_textrow))
    {
      if(split_fontrows < cfheight)
        cfheight = split_fontrows;
    }

    new_line = new_text;
    old_line = old_text;
    x = 0;
    offset = cs_y * tm_info.line_offset;
    do
    {
      if(h_panning)
      {
        if(hchars > text_cols)
        {
          xc = 0;
          font_col = h_panning;
          cfwidth = font_width - h_panning;
        }
        else
        {
          xc = x * font_width - h_panning;
          font_col = 0;
          if(hchars == 1)
          {
            cfwidth = h_panning;
          }
          else
          {
            cfwidth = font_width;
          }
        }
      }
      else
      {
        xc = x * font_width;
        font_col = 0;
        cfwidth = font_width;
      }

      if(forceUpdate || (old_text[0] != new_text[0])
       || (old_text[1] != new_text[1]))
      {
        cChar = new_text[0];
        new_foreground = new_text[1] & 0x0f;
        new_background = (new_text[1] & 0xf0) >> 4;

        XSetForeground(bx_x_display, gc, text_palette[new_foreground]);
        XSetBackground(bx_x_display, gc, text_palette[new_background]);

        XCopyPlane(bx_x_display, vgafont[cChar], win, gc, font_col, font_row,
                   cfwidth, cfheight, xc, yc, 1);
        if(offset == curs)
        {
          XSetForeground(bx_x_display, gc, text_palette[new_background]);
          XSetBackground(bx_x_display, gc, text_palette[new_foreground]);
          if(font_row == 0)
          {
            yc2 = yc + tm_info.cs_start;
            font_row2 = tm_info.cs_start;
            cfheight2 = tm_info.cs_end - tm_info.cs_start + 1;
            if((yc2 + cfheight2) > (dimension_y))
            {
              cfheight2 = dimension_y - yc2;
            }
          }
          else
          {
            if(v_panning > tm_info.cs_start)
            {
              yc2 = yc;
              font_row2 = font_row;
              cfheight2 = tm_info.cs_end - v_panning + 1;
            }
            else
            {
              yc2 = yc + tm_info.cs_start - v_panning;
              font_row2 = tm_info.cs_start;
              cfheight2 = tm_info.cs_end - tm_info.cs_start + 1;
            }
          }

          if(yc2 < (dimension_y))
          {
            XCopyPlane(bx_x_display, vgafont[cChar], win, gc, font_col,
                       font_row2, cfwidth, cfheight2, xc, yc2, 1);
          }
        }
      }

      x++;
      new_text += 2;
      old_text += 2;
      offset += 2;
    } while(--hchars);
    if(!split_screen && (y == split_textrow))
    {
      new_text = text_base;
      forceUpdate = 1;
      cs_y = 0;
      if(tm_info.split_hpanning)
        h_panning = 0;
      rows = ((dimension_y - line_compare + font_height - 2) / font_height) + 1;
      split_screen = 1;
    }
    else
    {
      y++;
      cs_y++;
      new_text = new_line + tm_info.line_offset;
      old_text = old_line + tm_info.line_offset;
    }
  } while(--rows);

  h_panning = tm_info.h_panning;
  prev_cursor_x = cursor_x;
  prev_cursor_y = cursor_y;

  XFlush(bx_x_display);
}

void bx_x11_gui_c::graphics_tile_update_impl(u8* tile, unsigned x0, unsigned y0)
{
  use_legacy_presentation();
  unsigned  x;

  unsigned  y;

  unsigned  y_size;
  unsigned  color;
  unsigned  offset;
  u8        b0;
  u8        b1;
  u8        b2;
  u8        b3;

  if((y0 + y_tilesize) > dimension_y)
  {
    y_size = dimension_y - y0;
  }
  else
  {
    y_size = y_tilesize;
  }

  switch(vga_bpp)
  {
  case 8:       // 8 bits per pixel
    for(y = 0; y < y_size; y++)
    {
      for(x = 0; x < x_tilesize; x++)
      {
        color = col_vals[tile[y * x_tilesize + x]];
        switch(imBPP)
        {
        case 8: // 8 bits per pixel
          ximage->data[imWide * y + x] = color;
          break;

        case 16:  // 16 bits per pixel
          offset = imWide * y + 2 * x;
          b0 = color >> 0;
          b1 = color >> 8;
          if(ximage->byte_order == LSBFirst)
          {
            ximage->data[offset + 0] = b0;
            ximage->data[offset + 1] = b1;
          }
          else
          {       // MSBFirst
            ximage->data[offset + 0] = b1;
            ximage->data[offset + 1] = b0;
          }
          break;

        case 24:  // 24 bits per pixel
          offset = imWide * y + 3 * x;
          b0 = color >> 0;
          b1 = color >> 8;
          b2 = color >> 16;
          if(ximage->byte_order == LSBFirst)
          {
            ximage->data[offset + 0] = b0;
            ximage->data[offset + 1] = b1;
            ximage->data[offset + 2] = b2;
          }
          else
          {       // MSBFirst
            ximage->data[offset + 0] = b2;
            ximage->data[offset + 1] = b1;
            ximage->data[offset + 2] = b0;
          }
          break;

        case 32:  // 32 bits per pixel
          offset = imWide * y + 4 * x;
          b0 = color >> 0;
          b1 = color >> 8;
          b2 = color >> 16;
          b3 = color >> 24;
          if(ximage->byte_order == LSBFirst)
          {
            ximage->data[offset + 0] = b0;
            ximage->data[offset + 1] = b1;
            ximage->data[offset + 2] = b2;
            ximage->data[offset + 3] = b3;
          }
          else
          {       // MSBFirst
            ximage->data[offset + 0] = b3;
            ximage->data[offset + 1] = b2;
            ximage->data[offset + 2] = b1;
            ximage->data[offset + 3] = b0;
          }
          break;

        default:
          BX_PANIC((
                     "X_graphics_tile_update: bits_per_pixel %u not implemented",
                     (unsigned) imBPP));
          return;
        }
      }
    }
    break;

  default:
    BX_PANIC((
               "X_graphics_tile_update: bits_per_pixel %u handled by new graphics API",
           (unsigned) vga_bpp));
    return;
  }

  XPutImage(bx_x_display, win, gc, ximage, 0, 0, x0, y0, x_tilesize, y_size);
}

void bx_x11_gui_c::present_frame()
{
  if(!bx_x_display || !frame_active || frame_pixels.empty()) return;
  prepare_frame_image(client_width, client_height);
  if(!x11_compose_frame(frame_pixels.data(), dimension_x, dimension_y,
                       client_width, client_height, video_linear,
      [this](unsigned x, unsigned y, u32 pixel) {
        XPutPixel(frame_image, x, y, x11_rgb((pixel >> 16) & 255,
                                           (pixel >> 8) & 255, pixel & 255));
      }))
    FAILURE(Configuration, "Invalid X11 presentation dimensions");
  XPutImage(bx_x_display, win, gc, frame_image, 0, 0, 0, 0,
            client_width, client_height);
}

void bx_x11_gui_c::prepare_frame_image(unsigned width, unsigned height)
{
  if(frame_image && frame_image->width == (int)width && frame_image->height == (int)height)
    return;
  XImage* replacement = create_x11_image(width, height);
  if(frame_image) XDestroyImage(frame_image);
  frame_image = replacement;
}

static bool read_x11_property(Atom property, Atom type, unsigned long* values,
                              unsigned capacity, unsigned long& count, unsigned minimum = 0,
                              Window target = win)
{
  Atom actual_type = None;
  int format = 0;
  unsigned long remaining = 0;
  unsigned char* data = nullptr;
  count = 0;
  const int status = XGetWindowProperty(bx_x_display, target, property, 0, capacity,
      False, type, &actual_type, &format, &count, &remaining, &data);
  const bool valid = status == Success && (actual_type == None ||
      (actual_type == type && format == 32 && remaining == 0 &&
       count >= minimum && count <= capacity &&
       (!count || data)));
  if(valid && actual_type != None && count)
    std::copy_n(reinterpret_cast<unsigned long*>(data), count, values);
  if(actual_type == None) count = 0;
  if(data) XFree(data);
  return valid;
}

bool bx_x11_gui_c::read_window_state()
{
  if(!bx_x_display || !win || window_destroyed) return false;
  XWindowAttributes attributes = {};
  unsigned long states[64] = {}, wm_state[2] = {}, count = 0, wm_count = 0;
  if(!XGetWindowAttributes(bx_x_display, win, &attributes) ||
     !read_x11_property(net_wm_state_atom, XA_ATOM, states, 64, count) ||
     !read_x11_property(wm_state_atom, wm_state_atom, wm_state, 2, wm_count, 2) ||
     (wm_count && wm_state[0] != NormalState && wm_state[0] != IconicState &&
      wm_state[0] != WithdrawnState))
  {
    window_state_dirty = true;
    return false;
  }
  bool maximized = false, fullscreen = false;
  bool minimized = attributes.map_state != IsViewable ||
                   (wm_count && wm_state[0] != NormalState);
  for(unsigned long i = 0; i < count; ++i)
  {
    maximized = maximized || states[i] == net_wm_max_horz || states[i] == net_wm_max_vert;
    fullscreen = fullscreen || states[i] == net_wm_fullscreen;
    minimized = minimized || states[i] == net_wm_hidden;
  }
  if(!fullscreen_transition_pending)
  {
    if(fullscreen && !window_fullscreen)
    {
      fullscreen_restore_maximized = window_maximized || maximized;
      fullscreen_restore_minimized = window_minimized || minimized;
      fullscreen_windowed_deferred = fullscreen_restore_maximized || fullscreen_restore_minimized;
      fullscreen_restore_width = fullscreen_restore_height = 0;
    }
    if(!fullscreen) fullscreen_windowed_deferred = false;
    fullscreen_requested = fullscreen;
    if(fullscreen_entry_resize)
    {
      if(!fullscreen) pending_guest_resize = true;
      fullscreen_entry_resize = false;
    }
  }
  window_maximized = maximized;
  window_minimized = minimized;
  window_fullscreen = fullscreen;
  window_state_dirty = false;
  return true;
}

bool bx_x11_gui_c::fullscreen_supported()
{
  unsigned long supported[256] = {}, count = 0;
  if(!read_x11_property(net_wm_supported, XA_ATOM, supported, 256, count, 0,
                        RootWindow(bx_x_display, bx_x_screen_num))) return false;
  bool state = false, fullscreen = false;
  for(unsigned long i = 0; i < count; ++i)
  {
    state = state || supported[i] == net_wm_state_atom;
    fullscreen = fullscreen || supported[i] == net_wm_fullscreen;
  }
  return state && fullscreen;
}

bool bx_x11_gui_c::send_fullscreen_request(bool enabled)
{
  if(enabled && !fullscreen_supported())
  {
    printf("%%X11-W-FULLSCREEN: The window manager does not advertise fullscreen support.\n");
    return false;
  }
  if(enabled)
  {
    XSizeHints hints = {};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.min_height = 1;
    hints.max_width = hints.max_height = 32767;
    XSetWMNormalHints(bx_x_display, win, &hints);
    pending_window_hints = true;
  }
  XEvent event = {};
  event.xclient.type = ClientMessage;
  event.xclient.window = win;
  event.xclient.message_type = net_wm_state_atom;
  event.xclient.format = 32;
  event.xclient.data.l[0] = enabled ? 1 : 0;
  event.xclient.data.l[1] = (long)net_wm_fullscreen;
  event.xclient.data.l[3] = 1;
  if(!XSendEvent(bx_x_display, RootWindow(bx_x_display, bx_x_screen_num), False,
                 SubstructureRedirectMask | SubstructureNotifyMask, &event))
  {
    printf("%%X11-W-FULLSCREEN: Unable to request %s fullscreen.\n", enabled ? "entering" : "leaving");
    return false;
  }
  if(enabled && !window_fullscreen)
  {
    fullscreen_restore_maximized = window_maximized;
    fullscreen_restore_minimized = window_minimized;
    fullscreen_restore_width = client_width;
    fullscreen_restore_height = client_height;
    fullscreen_windowed_deferred = window_maximized || window_minimized;
    fullscreen_entry_resize = false;
  }
  fullscreen_requested = fullscreen_target = enabled;
  fullscreen_transition_pending = true;
  fullscreen_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  window_state_dirty = true;
  return true;
}

void bx_x11_gui_c::sync_fullscreen(bool state_known)
{
  if(!fullscreen_transition_pending) return;
  const bool restored = (!fullscreen_restore_maximized || window_maximized) &&
      (!fullscreen_restore_minimized || window_minimized) &&
      (fullscreen_restore_maximized || fullscreen_restore_minimized ||
       !fullscreen_restore_width || (client_width == fullscreen_restore_width &&
                                    client_height == fullscreen_restore_height));
  if(state_known && window_fullscreen == fullscreen_target && (fullscreen_target || restored))
  {
    fullscreen_transition_pending = false;
    fullscreen_entry_resize = false;
    if(fullscreen_requested != window_fullscreen && !send_fullscreen_request(fullscreen_requested))
      fullscreen_requested = window_fullscreen;
  }
  else if(std::chrono::steady_clock::now() >= fullscreen_deadline)
  {
    if(state_known)
    {
      if(fullscreen_target && !window_fullscreen && fullscreen_entry_resize)
        pending_guest_resize = true;
      fullscreen_entry_resize = false;
    }
    fullscreen_transition_pending = false;
    fullscreen_requested = window_fullscreen;
    printf("%%X11-W-FULLSCREEN: The window manager did not complete the fullscreen request.\n");
  }
  if(!fullscreen_transition_pending && !window_fullscreen)
  {
    fullscreen_windowed_deferred = false;
    fullscreen_restore_maximized = fullscreen_restore_minimized = false;
    fullscreen_restore_width = fullscreen_restore_height = 0;
  }
}

void bx_x11_gui_c::toggle_fullscreen()
{
  if(!bx_x_display || !win || retired || window_destroyed) return;
  reset_window_pending = false;
  reset_stage = reset_phase::idle;
  if(!read_window_state())
  {
    printf("%%X11-W-FULLSCREEN: Unable to read the window state.\n");
    return;
  }
  sync_fullscreen();
  fullscreen_requested = !(fullscreen_transition_pending ? fullscreen_requested : window_fullscreen);
  if(!fullscreen_transition_pending && !send_fullscreen_request(fullscreen_requested))
    fullscreen_requested = window_fullscreen;
  apply_guest_size();
}

unsigned bx_x11_gui_c::window_scale() const
{
  return runtime_scale_override ? runtime_scale_override : configured_scale;
}

void bx_x11_gui_c::adjust_window_scale(int delta)
{
  if(!scale_change_enable || !bx_x_display || !win || retired || window_destroyed ||
     !dimension_x || !dimension_y) return;
  const unsigned next = (unsigned)(std::max)(1, (std::min)(8, (int)window_scale() + delta));
  if(next == runtime_scale_override) return;
  if(frame_active && (dimension_x > 32767 / next || dimension_y > 32767 / next))
  {
    printf("%%X11-W-SCALE: %ux exceeds the X11 window dimension limit; scale unchanged.\n", next);
    return;
  }
  runtime_scale_override = next;
  reset_window_size();
}

void bx_x11_gui_c::reset_window_size()
{
  if(!bx_x_display || !win || retired || window_destroyed || !dimension_x || !dimension_y) return;
  reset_absolute_mouse_position();
  pending_guest_resize = pending_window_hints = true;
  if(!reset_window_pending)
  {
    reset_window_pending = true;
    reset_stage = reset_phase::idle;
    reset_configure_seen = reset_map_maximized = false;
    reset_configure_serial = 0;
    reset_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  }
  if(fullscreen_transition_pending) fullscreen_requested = false;
  update_window_state();
}

void bx_x11_gui_c::sync_reset_window(bool state_known)
{
  if(!reset_window_pending) return;
  const auto now = std::chrono::steady_clock::now();
  auto failed = [this] {
    reset_window_pending = false;
    reset_stage = reset_phase::idle;
    printf("%%X11-W-RESET: The window manager did not restore the window.\n");
  };
  if(window_destroyed || retired)
  {
    reset_window_pending = false;
    reset_stage = reset_phase::idle;
    return;
  }
  if(!state_known)
  {
    if(now >= reset_deadline) failed();
    return;
  }
  if(reset_stage == reset_phase::fullscreen)
  {
    if(fullscreen_transition_pending)
    {
      fullscreen_requested = false;
      return;
    }
    if(window_fullscreen) { failed(); return; }
    reset_stage = reset_phase::idle;
  }
  if(reset_stage == reset_phase::map)
  {
    if(window_minimized || (reset_map_maximized && !window_maximized && now < reset_deadline))
    {
      if(now >= reset_deadline) failed();
      return;
    }
    reset_stage = reset_phase::idle;
  }
  if(reset_stage == reset_phase::unmaximize)
  {
    if(window_maximized || window_minimized || window_fullscreen)
    {
      if(now >= reset_deadline) failed();
      return;
    }
    if(!reset_configure_seen && now < reset_deadline) return;
    reset_stage = reset_phase::idle;
  }
  if(fullscreen_transition_pending || window_fullscreen)
  {
    fullscreen_requested = false;
    if(!fullscreen_transition_pending && !send_fullscreen_request(false))
    {
      fullscreen_requested = window_fullscreen;
      failed();
      return;
    }
    reset_stage = reset_phase::fullscreen;
    reset_deadline = now + std::chrono::seconds(4);
    return;
  }
  if(window_minimized)
  {
    reset_map_maximized = window_maximized;
    XMapWindow(bx_x_display, win);
    reset_stage = reset_phase::map;
    reset_deadline = now + std::chrono::seconds(2);
    return;
  }
  if(window_maximized)
  {
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.window = win;
    event.xclient.message_type = net_wm_state_atom;
    event.xclient.format = 32;
    event.xclient.data.l[1] = (long)net_wm_max_horz;
    event.xclient.data.l[2] = (long)net_wm_max_vert;
    event.xclient.data.l[3] = 1;
    reset_configure_seen = false;
    reset_configure_serial = XNextRequest(bx_x_display);
    if(!XSendEvent(bx_x_display, RootWindow(bx_x_display, bx_x_screen_num), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event))
    {
      failed();
      return;
    }
    reset_stage = reset_phase::unmaximize;
    reset_deadline = now + std::chrono::seconds(2);
    return;
  }
  reset_window_pending = false;
}

void bx_x11_gui_c::apply_guest_size()
{
  if(window_destroyed || window_minimized || window_maximized || window_fullscreen ||
     fullscreen_requested || fullscreen_transition_pending || reset_window_pending) return;
  if(pending_window_hints)
  {
    XSizeHints hints = {};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = frame_active ? 1 : (int)dimension_x;
    hints.min_height = frame_active ? 1 : (int)dimension_y;
    hints.max_width = frame_active ? 32767 : (int)dimension_x;
    hints.max_height = frame_active ? 32767 : (int)dimension_y;
    XSetWMNormalHints(bx_x_display, win, &hints);
  }
  if(pending_guest_resize)
  {
    const unsigned scale = frame_active ? window_scale() : 1;
    XResizeWindow(bx_x_display, win, dimension_x * scale, dimension_y * scale);
  }
  check_x11_error();
  pending_window_hints = pending_guest_resize = false;
}

void bx_x11_gui_c::update_window_state()
{
  if(read_window_state())
  {
    sync_fullscreen();
    sync_reset_window();
    apply_guest_size();
  }
  else
  {
    sync_fullscreen(false);
    sync_reset_window(false);
  }
}

void bx_x11_gui_c::configure_window(int width, int height)
{
  if(width <= 0 || height <= 0 || width > 32767 || height > 32767) return;
  if(client_width == (unsigned)width && client_height == (unsigned)height) return;
  if(frame_active) prepare_frame_image((unsigned)width, (unsigned)height);
  client_width = (unsigned)width;
  client_height = (unsigned)height;
  if(frame_active) present_frame();
}

void bx_x11_gui_c::graphics_frame_update_impl(const u32* pixels,
                                        unsigned width, unsigned height)
{
  if(!x_init_done) FAILURE(Runtime, "X11 display is not initialized");
  if(!pixels) FAILURE(Configuration, "Missing X11 framebuffer pixels");
  dimension_update_impl(width, height, 0, 0, 32, true);
  std::copy_n(pixels, (size_t)width * height, frame_pixels.data());
  frame_active = true;
  present_frame();
}

bx_svga_tileinfo_t* bx_x11_gui_c::graphics_tile_info_impl(bx_svga_tileinfo_t* info)
{
  if(!info)
  {
    info = (bx_svga_tileinfo_t*) malloc(sizeof(bx_svga_tileinfo_t));
    if(!info)
    {
      return NULL;
    }
  }

  info->bpp = ximage->bits_per_pixel;
  info->pitch = ximage->bytes_per_line;
  info->red_shift = 0;
  info->green_shift = 0;
  info->blue_shift = 0;
  info->red_mask = ximage->red_mask;
  info->green_mask = ximage->green_mask;
  info->blue_mask = ximage->blue_mask;

  int           i;

  int           rf;

  int           gf;

  int           bf;
  unsigned long red;
  unsigned long green;
  unsigned long blue;

  i = rf = gf = bf = 0;
  red = ximage->red_mask;
  green = ximage->green_mask;
  blue = ximage->blue_mask;

  while(red || rf || green || gf || blue || bf)
  {
    if(rf)
    {
      if(!(red & 1))
      {
        info->red_shift = i;
        rf = 0;
      }
    }
    else
    {
      if(red & 1)
      {
        rf = 1;
      }
    }

    if(gf)
    {
      if(!(green & 1))
      {
        info->green_shift = i;
        gf = 0;
      }
    }
    else
    {
      if(green & 1)
      {
        gf = 1;
      }
    }

    if(bf)
    {
      if(!(blue & 1))
      {
        info->blue_shift = i;
        bf = 0;
      }
    }
    else
    {
      if(blue & 1)
      {
        bf = 1;
      }
    }

    i++;
    red >>= 1;
    green >>= 1;
    blue >>= 1;
  }

  info->is_indexed = (default_visual->c_class != TrueColor)
&& (default_visual->c_class != DirectColor);
  info->is_little_endian = (ximage->byte_order == LSBFirst);

  return info;
}

u8* bx_x11_gui_c::graphics_tile_get_impl(unsigned x0, unsigned y0, unsigned*  w,
                                    unsigned*  h)
{
  if(x0 + x_tilesize > dimension_x)
  {
    *w = dimension_x - x0;
  }
  else
  {
    *w = x_tilesize;
  }

  if(y0 + y_tilesize > dimension_y)
  {
    *h = dimension_y - y0;
  }
  else
  {
    *h = y_tilesize;
  }

  return (u8*) ximage->data + ximage->xoffset * ximage->bits_per_pixel / 8;
}

void bx_x11_gui_c::graphics_tile_update_in_place_impl(unsigned x0, unsigned y0,
                                                 unsigned w, unsigned h)
{
  use_legacy_presentation();
  XPutImage(bx_x_display, win, gc, ximage, 0, 0, x0, y0, w, h);
}

bool bx_x11_gui_c::palette_change_impl(unsigned index, unsigned red, unsigned green,
                                  unsigned blue)
{
  if(index >= MAX_VGA_COLORS || red > 255 || green > 255 || blue > 255)
    FAILURE(Configuration, "Invalid X11 palette color");
  if(default_visual->c_class == TrueColor)
  {
    col_vals[index] = x11_rgb(red, green, blue);
    return true;
  }

  // returns: 0=no screen update needed (color map change has direct effect)
  //          1=screen updated needed (redraw using current colormap)
  XColor  color;

  color.flags = DoRed | DoGreen | DoBlue;
  color.red = red << 8;
  color.green = green << 8;
  color.blue = blue << 8;

  if(x_private_colormap)
  {

    //if (SIM->get_param_bool(BXPN_PRIVATE_COLORMAP)->get()) {
    color.pixel = col_vals[index];
    XStoreColor(bx_x_display, default_cmap, &color);
    return(0);    // no screen update needed
  }
  else
  {
    if(!XAllocColor(bx_x_display, default_cmap, &color))
      FAILURE(Runtime, "Unable to allocate X11 palette color");
    col_vals[index] = color.pixel;
    return(1);    // screen update needed
  }
}

void bx_x11_gui_c::use_legacy_presentation()
{
  if(frame_active) dimension_update_impl(dimension_x, dimension_y, 0, 0, vga_bpp);
}

void bx_x11_gui_c::dimension_update_impl(unsigned x, unsigned y, unsigned fheight,
                                    unsigned fwidth, unsigned bpp, bool full_frame)
{
  if(!x_init_done) FAILURE(Runtime, "X11 display is not initialized");
  validate_x11_dimensions(x, y);
  if((bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) ||
     (fheight && !fwidth))
    FAILURE(Configuration, "Invalid X11 display mode");

  const unsigned scale = full_frame ? window_scale() : 1;
  if(x > 32767 / scale || y > 32767 / scale)
    FAILURE(Configuration, "Scaled X11 display dimensions are out of range");
  const bool resize = x != dimension_x || y != dimension_y || full_frame != frame_active;
  const bool mode_changed = resize || bpp != vga_bpp ||
      (fheight && (fheight != font_height || fwidth != font_width));
  std::vector<u32> replacement;
  const bool replace_pixels = full_frame &&
      (x != dimension_x || y != dimension_y || !frame_active || frame_pixels.empty());
  if(full_frame)
  {
    if(default_visual->c_class != TrueColor)
      FAILURE(Configuration, "X11 full-frame display requires a TrueColor visual");
    if(replace_pixels)
    {
      if((size_t)x > replacement.max_size() / y)
        FAILURE(Configuration, "X11 framebuffer dimensions are out of range");
      try { replacement.resize((size_t)x * y); }
      catch(const std::bad_alloc&) { FAILURE(OutOfMemory, "Unable to allocate X11 framebuffer"); }
    }
    prepare_frame_image(client_width, client_height);
  }
  const bool state_known = resize && read_window_state();
  if(mode_changed) reset_absolute_mouse_position();
  if(replace_pixels) frame_pixels.swap(replacement);
  frame_active = full_frame;
  vga_bpp = bpp;

  if(fheight > 0)
  {
    font_height = fheight;
    font_width = fwidth;
    text_cols = x / font_width;
    text_rows = y / font_height;
  }

  if(resize)
  {
    if(full_frame) XSetWindowBackgroundPixmap(bx_x_display, win, None);
    else XSetWindowBackground(bx_x_display, win, black_pixel);
    pending_window_hints = true;
    const bool entering_fullscreen = fullscreen_transition_pending && fullscreen_target;
    if((!state_known && !fullscreen_transition_pending) ||
       (!window_fullscreen && !fullscreen_requested && !entering_fullscreen) ||
       fullscreen_windowed_deferred)
      pending_guest_resize = true;
    if(fullscreen_transition_pending && fullscreen_target && !window_fullscreen)
      fullscreen_entry_resize = true;
  }
  dimension_x = x;
  dimension_y = y;
  if(state_known) apply_guest_size();
}

void bx_x11_gui_c::exit_impl(void)
{
  mouse_enabled_changed_impl(false);
  guest_mouse_captured = false;
  mouse_sync_pending = true;
  mouse_input_serial = mouse_motion_serial = mouse_warp_serial = 0;
  if(frame_image) { XDestroyImage(frame_image); frame_image = nullptr; }
  std::vector<u32>().swap(frame_pixels);
  client_width = client_height = 0;
  runtime_scale_override = 0;
  wm_state_atom = net_wm_state_atom = None;
  net_wm_max_horz = net_wm_max_vert = net_wm_fullscreen = net_wm_hidden = None;
  net_wm_supported = None;
  window_state_dirty = true;
  window_destroyed = window_maximized = window_minimized = window_fullscreen = false;
  fullscreen_windowed_deferred = pending_guest_resize = pending_window_hints = false;
  fullscreen_requested = fullscreen_transition_pending = fullscreen_target = false;
  fullscreen_entry_resize = fullscreen_restore_maximized = fullscreen_restore_minimized = false;
  fullscreen_restore_width = fullscreen_restore_height = 0;
  fullscreen_deadline = {};
  reset_stage = reset_phase::idle;
  reset_window_pending = reset_configure_seen = reset_map_maximized = false;
  reset_configure_serial = 0;
  reset_deadline = {};
  if(ximage) { XDestroyImage(ximage); ximage = nullptr; }
  // XCloseDisplay frees server resources, but GCs also need client cleanup.
  if(bx_x_display)
  {
    if(gc) XFreeGC(bx_x_display, gc);
    if(gc_inv) XFreeGC(bx_x_display, gc_inv);
    XCloseDisplay(bx_x_display);
  }
  restore_x11_error_handler();
  bx_x_display = nullptr;
  default_visual = nullptr;
  default_cmap = None;
  win = None;
  gc = gc_inv = nullptr;
  hidden_cursor = None;
  wm_delete_window = wm_protocols = None;
  memset(vgafont, 0, sizeof(vgafont));
  x_init_done = frame_active = false;
  dimension_x = dimension_y = 0;
  imDepth = imWide = imBPP = 0;
  vga_bpp = 8;
  CTRL_pressed = false;
  legacy_redraw_pending = false;
  for(auto& key : pressed_keys) key = {};
  std::fill_n(key_release_serial, 256, 0);
  keyboard_focused = true;
  detectable_repeat = false;
}

static u32 convertStringToXKeysym(const char* string)
{
  if(!string) return BX_KEYMAP_UNKNOWN;
  if(strncmp(string, "XK_", 3) == 0)
  {
    const KeySym symbol = XStringToKeysym(string + 3);
    return symbol != NoSymbol ? (u32)symbol : BX_KEYMAP_UNKNOWN;
  }
  if(strncmp(string, "SDLK_", 5) != 0) return BX_KEYMAP_UNKNOWN;
  const std::string key = string + 5;
  if(key.size() == 1)
  {
    const unsigned char ch = (unsigned char)key[0];
    if(ch >= 'A' && ch <= 'Z') return x11_sdl_keymap_mask | (ch - 'A' + XK_a);
    if((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
      return x11_sdl_keymap_mask | ch;
  }
  if(key.size() == 4 && key.compare(0, 3, "KP_") == 0 &&
     key[3] >= '0' && key[3] <= '9')
    return x11_sdl_keymap_mask | (XK_KP_0 + key[3] - '0');
  for(unsigned i = 0; i < 24; ++i)
    if(key == "F" + std::to_string(i + 1)) return x11_sdl_keymap_mask | (XK_F1 + i);

  static const struct { const char* name; KeySym symbol; } names[] = {
    { "RETURN", XK_Return }, { "ESCAPE", XK_Escape },
    { "BACKSPACE", XK_BackSpace }, { "TAB", XK_Tab }, { "SPACE", XK_space },
    { "CAPSLOCK", XK_Caps_Lock }, { "PRINTSCREEN", XK_Print },
    { "SCROLLLOCK", XK_Scroll_Lock }, { "PAUSE", XK_Pause },
    { "INSERT", XK_Insert }, { "HOME", XK_Home }, { "PAGEUP", XK_Page_Up },
    { "DELETE", XK_Delete }, { "END", XK_End }, { "PAGEDOWN", XK_Page_Down },
    { "RIGHT", XK_Right }, { "LEFT", XK_Left }, { "DOWN", XK_Down }, { "UP", XK_Up },
    { "NUMLOCK", XK_Num_Lock }, { "NUMLOCKCLEAR", XK_Num_Lock }, { "CLEAR", XK_Clear },
    { "LSHIFT", XK_Shift_L }, { "RSHIFT", XK_Shift_R },
    { "LCTRL", XK_Control_L }, { "RCTRL", XK_Control_R },
    { "LALT", XK_Alt_L }, { "RALT", XK_Alt_R },
    { "LGUI", XK_Super_L }, { "RGUI", XK_Super_R },
    { "MENU", XK_Menu }, { "HELP", XK_Help },
    { "KP_ENTER", XK_KP_Enter }, { "KP_PERIOD", XK_KP_Decimal },
    { "KP_DIVIDE", XK_KP_Divide }, { "KP_MULTIPLY", XK_KP_Multiply },
    { "KP_MINUS", XK_KP_Subtract }, { "KP_PLUS", XK_KP_Add }, { "KP_EQUALS", XK_KP_Equal },
    { "EXCLAIM", XK_exclam }, { "DBLAPOSTROPHE", XK_quotedbl }, { "HASH", XK_numbersign },
    { "DOLLAR", XK_dollar }, { "AMPERSAND", XK_ampersand }, { "APOSTROPHE", XK_apostrophe },
    { "LEFTPAREN", XK_parenleft }, { "RIGHTPAREN", XK_parenright }, { "ASTERISK", XK_asterisk },
    { "PLUS", XK_plus }, { "COMMA", XK_comma }, { "MINUS", XK_minus }, { "PERIOD", XK_period },
    { "SLASH", XK_slash }, { "COLON", XK_colon }, { "SEMICOLON", XK_semicolon },
    { "LESS", XK_less }, { "EQUALS", XK_equal }, { "GREATER", XK_greater },
    { "QUESTION", XK_question }, { "AT", XK_at }, { "LEFTBRACKET", XK_bracketleft },
    { "BACKSLASH", XK_backslash }, { "RIGHTBRACKET", XK_bracketright }, { "CARET", XK_asciicircum },
    { "UNDERSCORE", XK_underscore }, { "GRAVE", XK_grave }
  };
  for(const auto& named : names)
    if(x11_hotkey_equal(key, named.name)) return x11_sdl_keymap_mask | (u32)named.symbol;
  return BX_KEYMAP_UNKNOWN;
}

void bx_x11_gui_c::get_capabilities(u16* xres, u16* yres, u16* bpp)
{
  *xres = 1024;
  *yres = 768;
  *bpp = 32;
}
#endif /* if BX_WITH_X11 */
