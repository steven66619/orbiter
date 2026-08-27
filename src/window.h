#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client.h"
#include <xkbcommon/xkbcommon.h>
#include <cairo.h>
#include <pango/pango.h>

typedef struct _PangoContext PangoContext;

namespace orbiter {

struct Theme;
struct DesktopEntry;
struct Config;

// A single wl_shm buffer backing the on-screen surface.
struct ShmBuffer {
  wl_buffer *buffer = nullptr;
  void *data = nullptr;
  size_t size = 0;
  int width = 0;
  int height = 0;
  bool busy = false;
};

class LauncherWindow {
public:
  LauncherWindow();
  ~LauncherWindow();

  bool init();
  void run();

private:
  // Wayland listener callbacks (file-scope statics in window.cpp)
  friend void registry_handle_global(void *, wl_registry *, uint32_t, const char *, uint32_t);
  friend void output_handle_mode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t);
  friend void output_handle_scale(void *, wl_output *, int32_t);
  friend void layer_surface_configure(void *, zwlr_layer_surface_v1 *, uint32_t, uint32_t, uint32_t);
  friend void layer_surface_closed(void *, zwlr_layer_surface_v1 *);
  friend void seat_capabilities(void *, wl_seat *, uint32_t);
  friend void keyboard_keymap(void *, wl_keyboard *, uint32_t, int, uint32_t);
  friend void keyboard_enter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *);
  friend void keyboard_leave(void *, wl_keyboard *, uint32_t, wl_surface *);
  friend void keyboard_key(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t);
  friend void keyboard_modifiers(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
  friend void pointer_enter(void *, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t, wl_fixed_t);
  friend void pointer_leave(void *, wl_pointer *, uint32_t, wl_surface *);
  friend void pointer_motion(void *, wl_pointer *, uint32_t, wl_fixed_t, wl_fixed_t);
  friend void pointer_button(void *, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t);
  friend void pointer_axis(void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t);
  friend void data_source_send(void *, wl_data_source *, const char *, int);
  friend void data_source_cancelled(void *, wl_data_source *);

  wl_display *display_ = nullptr;
  wl_registry *registry_ = nullptr;
  wl_compositor *compositor_ = nullptr;
  wl_shm *shm_ = nullptr;
  wl_seat *seat_ = nullptr;
  wl_pointer *pointer_ = nullptr;
  wl_keyboard *keyboard_ = nullptr;
  wl_output *output_ = nullptr;
  zwlr_layer_shell_v1 *layer_shell_ = nullptr;
  zwlr_layer_surface_v1 *layer_surface_ = nullptr;
  wl_surface *surface_ = nullptr;
  wl_callback *frame_callback_ = nullptr;
  wl_data_device_manager *data_device_manager_ = nullptr;
  wl_data_device *data_device_ = nullptr;
  wl_data_source *data_source_ = nullptr;

  // xkbcommon keyboard state
  xkb_context *xkb_ctx_ = nullptr;
  xkb_keymap *xkb_keymap_ = nullptr;
  xkb_state *xkb_state_ = nullptr;
  xkb_mod_index_t mod_ctrl_ = XKB_MOD_INVALID;

  // Backbuffer (off-screen, eliminates flicker)
  cairo_surface_t *backbuf_ = nullptr;
  cairo_t *back_cr_ = nullptr;

  PangoContext *pango_ctx_ = nullptr;

  int width_ = 520;
  int height_ = 360;
  int screen_width_ = 0;
  int screen_height_ = 0;
  int scale_ = 1;

  std::string input_;
  std::vector<DesktopEntry> apps_;
  std::vector<DesktopEntry> filtered_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<Config> config_;
  int selection_ = 0;
  int scroll_offset_ = 0;
  double scroll_visual_ = 0.0;
  bool running_ = true;
  bool dirty_ = true;
  bool show_metrics_ = false;

  std::vector<std::string> recent_apps_;
  std::string pending_copy_;

  uint64_t last_frame_ = 0;
  bool cursor_visible_ = true;
  bool has_focus_ = false;
  uint64_t cursor_toggle_time_ = 0;
  uint64_t metrics_update_time_ = 0;

  // Layer-shell / input state
  bool configured_ = false;
  bool keyboard_entered_ = false;
  bool pointer_entered_ = false;
  int pointer_x_ = 0;
  int pointer_y_ = 0;
  uint32_t last_serial_ = 0;

  // Double-buffered shm pool
  ShmBuffer buffers_[2];
  int current_buffer_ = 0;

  void setup_layer_surface();
  void setup_rendering();
  void setup_keyboard();
  void setup_clipboard();
  void center_window();

  void handle_key(xkb_keysym_t sym, uint32_t keycode);
  void handle_button_press(int x, int y);
  void handle_axis(double value);
  void update_hover();

  void compose();
  void flip();
  void compose_input_field();
  void compose_results();
  void compose_metrics();
  void compose_entry(int index, int y, bool hovered);
  void compose_entry_ptr(const DesktopEntry &entry, int y, bool hovered);

  void launch_selected();
  void update_filter();
  void save_recent(const std::string &exec);

  void copy_to_clipboard(const std::string &text);
  void destroy_buffers();
  bool create_buffer(ShmBuffer &buf, int width, int height);
  void render_frame();
};

} // namespace orbiter