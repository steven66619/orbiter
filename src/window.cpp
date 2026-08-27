#include "window.h"
#include "config.h"
#include "theme.h"
#include "launch.h"
#include "metrics.h"
#include "desktop.h"
#include "icons.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/timerfd.h>

#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client.h"
#include <xkbcommon/xkbcommon.h>
#include <cairo.h>
#include <pango/pangocairo.h>

namespace orbiter {

static const char *APP_NAME = "orbiter";

static uint64_t timestamp_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()
  ).count();
}

static void set_source_rgba(cairo_t *cr, const Rgba &c) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  cairo_move_to(cr, x + r, y);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

// ── Wayland listeners ────────────────────────────────────────────────

// Forward declarations of listener structs (defined below) so the
// registry/seat handlers can attach them.
extern const wl_output_listener output_listener;
extern const wl_pointer_listener pointer_listener;
extern const wl_keyboard_listener keyboard_listener;

void registry_handle_global(void *data, wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    ws->compositor_ = static_cast<wl_compositor *>(
      wl_registry_bind(registry, name, &wl_compositor_interface, 4));
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    ws->shm_ = static_cast<wl_shm *>(
      wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    ws->seat_ = static_cast<wl_seat *>(
      wl_registry_bind(registry, name, &wl_seat_interface, 7));
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    ws->output_ = static_cast<wl_output *>(
      wl_registry_bind(registry, name, &wl_output_interface, 3));
    wl_output_add_listener(ws->output_, &output_listener, ws);
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    ws->layer_shell_ = static_cast<zwlr_layer_shell_v1 *>(
      wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1));
  } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
    ws->data_device_manager_ = static_cast<wl_data_device_manager *>(
      wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3));
  }
}

static void registry_handle_global_remove(void *data, wl_registry *registry,
                                          uint32_t name) {}

const wl_registry_listener registry_listener = {
  registry_handle_global,
  registry_handle_global_remove
};

static void output_handle_geometry(void *data, wl_output *output, int32_t x, int32_t y,
                                   int32_t physical_width, int32_t physical_height,
                                   int32_t subpixel, const char *make, const char *model,
                                   int32_t transform) {}

void output_handle_mode(void *data, wl_output *output, uint32_t flags,
                               int32_t width, int32_t height, int32_t refresh) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (flags & WL_OUTPUT_MODE_CURRENT) {
    ws->screen_width_ = width;
    ws->screen_height_ = height;
  }
}

static void output_handle_done(void *data, wl_output *output) {}
void output_handle_scale(void *data, wl_output *output, int32_t factor) {
  auto *ws = static_cast<LauncherWindow *>(data);
  ws->scale_ = factor;
}

const wl_output_listener output_listener = {
  output_handle_geometry,
  output_handle_mode,
  output_handle_done,
  output_handle_scale
};

void layer_surface_configure(void *data, zwlr_layer_surface_v1 *ls,
                                    uint32_t serial, uint32_t w, uint32_t h) {
  auto *ws = static_cast<LauncherWindow *>(data);
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  if (w != 0 && h != 0) {
    ws->width_ = w;
    ws->height_ = h;
  }
  ws->configured_ = true;
  ws->dirty_ = true;
}

void layer_surface_closed(void *data, zwlr_layer_surface_v1 *ls) {
  auto *ws = static_cast<LauncherWindow *>(data);
  ws->running_ = false;
}

const zwlr_layer_surface_v1_listener layer_surface_listener = {
  layer_surface_configure,
  layer_surface_closed
};

static void buffer_release(void *data, wl_buffer *buffer) {
  auto *buf = static_cast<ShmBuffer *>(data);
  buf->busy = false;
}

const wl_buffer_listener buffer_listener = { buffer_release };

void seat_capabilities(void *data, wl_seat *seat, uint32_t caps) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ws->pointer_) {
    ws->pointer_ = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(ws->pointer_, &pointer_listener, ws);
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ws->keyboard_) {
    ws->keyboard_ = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(ws->keyboard_, &keyboard_listener, ws);
  }
}

static void seat_name(void *data, wl_seat *seat, const char *name) {}

const wl_seat_listener seat_listener = { seat_capabilities, seat_name };

void keyboard_keymap(void *data, wl_keyboard *kb, uint32_t format,
                            int fd, uint32_t size) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
  char *map_str = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (map_str == MAP_FAILED) { close(fd); return; }
  if (ws->xkb_keymap_) xkb_keymap_unref(ws->xkb_keymap_);
  if (ws->xkb_state_) xkb_state_unref(ws->xkb_state_);
  ws->xkb_keymap_ = xkb_keymap_new_from_string(ws->xkb_ctx_, map_str,
    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, size);
  close(fd);
  if (!ws->xkb_keymap_) return;
  ws->xkb_state_ = xkb_state_new(ws->xkb_keymap_);
  ws->mod_ctrl_ = xkb_keymap_mod_get_index(ws->xkb_keymap_, XKB_MOD_NAME_CTRL);
}

void keyboard_enter(void *data, wl_keyboard *kb, uint32_t serial,
                           wl_surface *surface, wl_array *keys) {
  auto *ws = static_cast<LauncherWindow *>(data);
  ws->keyboard_entered_ = true;
  ws->has_focus_ = true;
  ws->last_serial_ = serial;
}

void keyboard_leave(void *data, wl_keyboard *kb, uint32_t serial,
                           wl_surface *surface) {
  auto *ws = static_cast<LauncherWindow *>(data);
  ws->keyboard_entered_ = false;
  ws->has_focus_ = false;
  ws->running_ = false; // dismiss when focus is lost
}

void keyboard_key(void *data, wl_keyboard *kb, uint32_t serial, uint32_t time,
                         uint32_t key, uint32_t state) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
  if (!ws->xkb_state_) return;
  ws->last_serial_ = serial;
  // wl_keyboard.key carries evdev keycodes, but the keymap sway sends is in
  // the X11 keycode space (evdev + 8). Add 8 so keysyms/UTF-8 resolve
  // correctly (wlroots does the same internally).
  xkb_keysym_t sym = xkb_state_key_get_one_sym(ws->xkb_state_, key + 8);
  ws->handle_key(sym, key + 8);
}

void keyboard_modifiers(void *data, wl_keyboard *kb, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (ws->xkb_state_)
    xkb_state_update_mask(ws->xkb_state_, mods_depressed, mods_latched,
                          mods_locked, 0, 0, group);
}

static void keyboard_repeat_info(void *data, wl_keyboard *kb, int32_t rate,
                                 int32_t delay) {}

const wl_keyboard_listener keyboard_listener = {
  keyboard_keymap,
  keyboard_enter,
  keyboard_leave,
  keyboard_key,
  keyboard_modifiers,
  keyboard_repeat_info
};

void pointer_enter(void *data, wl_pointer *ptr, uint32_t serial,
                          wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
  auto *ws = static_cast<LauncherWindow *>(data);
  ws->pointer_entered_ = true;
  ws->pointer_x_ = wl_fixed_to_int(sx);
  ws->pointer_y_ = wl_fixed_to_int(sy);
  ws->last_serial_ = serial;
}

void pointer_leave(void *data, wl_pointer *ptr, uint32_t serial,
                          wl_surface *surface) {
  auto *ws = static_cast<LauncherWindow *>(data);
  ws->pointer_entered_ = false;
}

void pointer_motion(void *data, wl_pointer *ptr, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
  auto *ws = static_cast<LauncherWindow *>(data);
  ws->pointer_x_ = wl_fixed_to_int(sx);
  ws->pointer_y_ = wl_fixed_to_int(sy);
  ws->update_hover();
}

void pointer_button(void *data, wl_pointer *ptr, uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
  ws->last_serial_ = serial;
  if (button == 0x110) /* BTN_LEFT */
    ws->handle_button_press(ws->pointer_x_, ws->pointer_y_);
}

void pointer_axis(void *data, wl_pointer *ptr, uint32_t time, uint32_t axis,
                         wl_fixed_t value) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  ws->handle_axis(wl_fixed_to_double(value));
}

static void pointer_frame(void *data, wl_pointer *ptr) {}
static void pointer_axis_source(void *data, wl_pointer *ptr, uint32_t axis_source) {}
static void pointer_axis_stop(void *data, wl_pointer *ptr, uint32_t time, uint32_t axis) {}
static void pointer_axis_discrete(void *data, wl_pointer *ptr, uint32_t axis,
                                  int32_t discrete) {}

const wl_pointer_listener pointer_listener = {
  pointer_enter,
  pointer_leave,
  pointer_motion,
  pointer_button,
  pointer_axis,
  pointer_frame,
  pointer_axis_source,
  pointer_axis_stop,
  pointer_axis_discrete
};

static void data_source_target(void *data, wl_data_source *src, const char *mime) {}
void data_source_send(void *data, wl_data_source *src, const char *mime, int fd) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (strcmp(mime, "text/plain;charset=utf-8") == 0 ||
      strcmp(mime, "text/plain") == 0) {
    const std::string &text = ws->pending_copy_;
    size_t off = 0;
    while (off < text.size()) {
      ssize_t n = write(fd, text.data() + off, text.size() - off);
      if (n <= 0) break;
      off += (size_t)n;
    }
  }
  close(fd);
}
void data_source_cancelled(void *data, wl_data_source *src) {
  auto *ws = static_cast<LauncherWindow *>(data);
  if (ws->data_source_ == src) {
    wl_data_source_destroy(src);
    ws->data_source_ = nullptr;
  }
}
static void data_source_dnd_drop_performed(void *data, wl_data_source *src) {}
static void data_source_dnd_finished(void *data, wl_data_source *src) {}
static void data_source_action(void *data, wl_data_source *src, uint32_t dnd_action) {}

const wl_data_source_listener data_source_listener = {
  data_source_target,
  data_source_send,
  data_source_cancelled,
  data_source_dnd_drop_performed,
  data_source_dnd_finished,
  data_source_action
};

// ── Construction / Destruction ──────────────────────────────────────

LauncherWindow::LauncherWindow()
  : config_(std::make_unique<Config>(load_config()))
  , theme_(std::make_unique<Theme>(load_theme())) {
  apps_ = load_applications_cached();
  filtered_ = apps_;
  show_metrics_ = config_->show_metrics;
  recent_apps_ = load_recent_apps();

  // Pre-warm all app icons so first render is instant
  std::vector<std::string> icon_names;
  for (auto &app : apps_) {
    if (!app.icon.empty()) icon_names.push_back(app.icon);
  }
  prewarm_all_icons(icon_names, 28);
}

LauncherWindow::~LauncherWindow() {
  destroy_buffers();
  if (data_source_) wl_data_source_destroy(data_source_);
  if (data_device_) wl_data_device_release(data_device_);
  if (keyboard_) wl_keyboard_release(keyboard_);
  if (pointer_) wl_pointer_release(pointer_);
  if (seat_) wl_seat_release(seat_);
  if (layer_surface_) zwlr_layer_surface_v1_destroy(layer_surface_);
  if (surface_) wl_surface_destroy(surface_);
  if (layer_shell_) zwlr_layer_shell_v1_destroy(layer_shell_);
  if (data_device_manager_) wl_data_device_manager_destroy(data_device_manager_);
  if (output_) wl_output_release(output_);
  if (compositor_) wl_compositor_destroy(compositor_);
  if (shm_) wl_shm_destroy(shm_);
  if (registry_) wl_registry_destroy(registry_);
  if (xkb_state_) xkb_state_unref(xkb_state_);
  if (xkb_keymap_) xkb_keymap_unref(xkb_keymap_);
  if (xkb_ctx_) xkb_context_unref(xkb_ctx_);
  if (pango_ctx_) g_object_unref(pango_ctx_);
  if (back_cr_) cairo_destroy(back_cr_);
  if (backbuf_) cairo_surface_destroy(backbuf_);
  if (display_) wl_display_disconnect(display_);
}

// ── Initialization ──────────────────────────────────────────────────

bool LauncherWindow::init() {
  display_ = wl_display_connect(nullptr);
  if (!display_) {
    std::cerr << "Failed to connect to Wayland display" << std::endl;
    return false;
  }

  xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkb_ctx_) {
    std::cerr << "Failed to create xkb context" << std::endl;
    return false;
  }

  registry_ = wl_display_get_registry(display_);
  wl_registry_add_listener(registry_, &registry_listener, this);
  wl_display_roundtrip(display_);

  if (!compositor_ || !shm_ || !layer_shell_ || !seat_) {
    std::cerr << "Missing required Wayland globals (need compositor, shm, "
                 "layer-shell, seat)" << std::endl;
    return false;
  }

  wl_seat_add_listener(seat_, &seat_listener, this);
  wl_display_roundtrip(display_);

  setup_layer_surface();
  setup_rendering();
  setup_clipboard();

  // Wait for the compositor to configure the layer surface
  wl_display_roundtrip(display_);
  if (!configured_) {
    std::cerr << "Layer surface was not configured" << std::endl;
    return false;
  }

  return true;
}

void LauncherWindow::setup_layer_surface() {
  surface_ = wl_compositor_create_surface(compositor_);
  layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(layer_shell_, surface_,
    nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, APP_NAME);
  zwlr_layer_surface_v1_set_anchor(layer_surface_,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_, true);
  zwlr_layer_surface_v1_set_size(layer_surface_, width_, height_);
  int top_margin = screen_height_ > 0 ? (screen_height_ - height_) / 3 : 0;
  zwlr_layer_surface_v1_set_margin(layer_surface_, std::max(0, top_margin), 0, 0, 0);
  zwlr_layer_surface_v1_add_listener(layer_surface_, &layer_surface_listener, this);
  wl_surface_commit(surface_);
}

void LauncherWindow::setup_rendering() {
  // Backbuffer (off-screen image surface)
  backbuf_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width_, height_);
  back_cr_ = cairo_create(backbuf_);

  pango_ctx_ = pango_cairo_create_context(back_cr_);

  // Pre-warm Pango fontconfig — force font loading before first render
  auto warmup_layout = [&](const char *desc, const char *text) {
    auto *l = pango_cairo_create_layout(back_cr_);
    auto *fd = pango_font_description_from_string(desc);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, text, -1);
    pango_cairo_show_layout(back_cr_, l);
    g_object_unref(l);
  };
  warmup_layout("Sans 14", "w");
  warmup_layout("Sans Bold 12", "w");
  warmup_layout("Sans 10", "w");
}

void LauncherWindow::setup_clipboard() {
  if (data_device_manager_ && seat_)
    data_device_ = wl_data_device_manager_get_data_device(data_device_manager_, seat_);
}

// ── Shm buffers ─────────────────────────────────────────────────────

static int create_shm_fd(size_t size) {
  char name[64];
  for (int i = 0; i < 100; i++) {
    snprintf(name, sizeof(name), "/wl_shm-%d-%d", getpid(), i);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      shm_unlink(name);
      if (ftruncate(fd, (off_t)size) == 0) return fd;
      close(fd);
      return -1;
    }
  }
  return -1;
}

bool LauncherWindow::create_buffer(ShmBuffer &buf, int width, int height) {
  int stride = width * 4;
  size_t size = (size_t)stride * (size_t)height;
  int fd = create_shm_fd(size);
  if (fd < 0) return false;

  void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) { close(fd); return false; }

  wl_shm_pool *pool = wl_shm_create_pool(shm_, fd, (int32_t)size);
  wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
    stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  buf.buffer = buffer;
  buf.data = data;
  buf.size = size;
  buf.width = width;
  buf.height = height;
  buf.busy = false;
  wl_buffer_add_listener(buffer, &buffer_listener, &buf);
  return true;
}

void LauncherWindow::destroy_buffers() {
  for (auto &buf : buffers_) {
    if (buf.buffer) wl_buffer_destroy(buf.buffer);
    if (buf.data) munmap(buf.data, buf.size);
    buf = ShmBuffer{};
  }
}

// ── Main Loop ───────────────────────────────────────────────────────

void LauncherWindow::run() {
  last_frame_ = timestamp_ms();
  cursor_toggle_time_ = last_frame_;
  metrics_update_time_ = last_frame_;

  // Initial render
  compose();
  render_frame();

  int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (timer_fd < 0) { running_ = false; return; }
  struct itimerspec its = {};
  its.it_interval.tv_nsec = 8000000; // 8 ms tick
  its.it_value.tv_nsec = 8000000;
  timerfd_settime(timer_fd, 0, &its, nullptr);

  while (running_) {
    int wl_fd = wl_display_get_fd(display_);

    // Prepare read: flush pending requests, then drain any events that
    // arrived before poll() so we never block on a stale fd.
    while (wl_display_prepare_read(display_) != 0)
      wl_display_dispatch_pending(display_);
    wl_display_flush(display_);

    struct pollfd fds[2] = {
      {wl_fd, POLLIN, 0},
      {timer_fd, POLLIN, 0},
    };
    int ret = poll(fds, 2, -1);
    if (ret < 0) {
      if (errno == EINTR) { wl_display_cancel_read(display_); continue; }
      wl_display_cancel_read(display_);
      break;
    }

    if (fds[0].revents & POLLIN)
      wl_display_read_events(display_);
    else
      wl_display_cancel_read(display_);
    wl_display_dispatch_pending(display_);

    if (fds[1].revents & POLLIN) {
      uint64_t exp;
      read(timer_fd, &exp, sizeof(exp));
      auto now = timestamp_ms();
      bool need_update = false;

      if (now - cursor_toggle_time_ > 500) {
        cursor_visible_ = !cursor_visible_;
        cursor_toggle_time_ = now;
        need_update = true;
      }

      if (show_metrics_ && now - metrics_update_time_ > 1000) {
        metrics_update_time_ = now;
        need_update = true;
      }

      // Smooth scroll animation
      if (std::abs(scroll_visual_ - scroll_offset_) > 0.5) {
        scroll_visual_ += (scroll_offset_ - scroll_visual_) * 0.3;
        need_update = true;
      } else if (scroll_visual_ != scroll_offset_) {
        scroll_visual_ = scroll_offset_;
        need_update = true;
      }

      if (need_update) {
        compose();
        render_frame();
      }
    }
  }

  close(timer_fd);
}

// ── Rendering ───────────────────────────────────────────────────────

void LauncherWindow::render_frame() {
  if (!configured_ || !surface_) return;

  // Pick a free buffer; skip the frame if both are still busy.
  ShmBuffer *buf = nullptr;
  for (int i = 0; i < 2; i++) {
    if (!buffers_[i].busy) { buf = &buffers_[i]; break; }
  }
  if (!buf) return;

  if (buf->width != width_ || buf->height != height_) {
    if (buf->buffer) wl_buffer_destroy(buf->buffer);
    if (buf->data) munmap(buf->data, buf->size);
    *buf = ShmBuffer{};
    if (!create_buffer(*buf, width_, height_)) return;
  }

  // Blit the backbuffer into the shm buffer
  cairo_surface_t *shm_surf = cairo_image_surface_create_for_data(
    static_cast<unsigned char *>(buf->data), CAIRO_FORMAT_ARGB32,
    width_, height_, width_ * 4);
  cairo_t *cr = cairo_create(shm_surf);
  cairo_set_source_surface(cr, backbuf_, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(shm_surf);

  buf->busy = true;
  wl_surface_attach(surface_, buf->buffer, 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
  wl_surface_commit(surface_);
}

void LauncherWindow::compose() {
  set_source_rgba(back_cr_, theme_->bg);
  cairo_paint(back_cr_);

  set_source_rgba(back_cr_, theme_->border);
  cairo_set_line_width(back_cr_, theme_->border_width);
  rounded_rect(back_cr_, 0.5, 0.5, width_ - 1, height_ - 1, theme_->border_radius);
  cairo_stroke(back_cr_);

  compose_input_field();
  compose_results();
  if (show_metrics_) compose_metrics();
}

// ── Input Handling ──────────────────────────────────────────────────

void LauncherWindow::handle_key(xkb_keysym_t sym, uint32_t keycode) {
  bool ctrl = xkb_state_ && xkb_state_mod_index_is_active(
    xkb_state_, mod_ctrl_, XKB_STATE_MODS_EFFECTIVE);

  switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
      launch_selected();
      running_ = false;
      break;

    case XKB_KEY_Escape:
      running_ = false;
      break;

    case XKB_KEY_BackSpace:
      if (!input_.empty()) {
        if (ctrl) {
          auto pos = input_.find_last_not_of(" ");
          if (pos != std::string::npos) {
            auto word_start = input_.rfind(' ', pos);
            if (word_start == std::string::npos)
              input_.clear();
            else
              input_ = input_.substr(0, word_start);
          }
        } else {
          input_.pop_back();
        }
        update_filter();
      }
      break;

    case XKB_KEY_Up:
    case XKB_KEY_KP_Up:
      if (selection_ > 0) --selection_;
      break;

    case XKB_KEY_Down:
    case XKB_KEY_KP_Down:
      if (selection_ < (int)filtered_.size() - 1) ++selection_;
      break;

    case XKB_KEY_Page_Up:
    case XKB_KEY_KP_Page_Up: {
      int page = (height_ - 50) / 42;
      selection_ = std::max(0, selection_ - page);
      break;
    }
    case XKB_KEY_Page_Down:
    case XKB_KEY_KP_Page_Down: {
      int page = (height_ - 50) / 42;
      selection_ = std::min((int)filtered_.size() - 1, selection_ + page);
      break;
    }

    case XKB_KEY_Home:
    case XKB_KEY_KP_Home:
      selection_ = 0;
      break;

    case XKB_KEY_End:
    case XKB_KEY_KP_End:
      selection_ = (int)filtered_.size() - 1;
      break;

    case XKB_KEY_Tab:
      if (!filtered_.empty()) {
        input_ = filtered_[0].display_name();
        update_filter();
      }
      break;

    case XKB_KEY_c:
      if (ctrl && !filtered_.empty()) {
        auto &entry = filtered_[std::min(selection_, (int)filtered_.size() - 1)];
        copy_to_clipboard(entry.exec);
        break;
      }
      [[fallthrough]];

    default: {
      if (!xkb_state_) break;
      char buf[64];
      int len = xkb_state_key_get_utf8(xkb_state_, keycode, buf, sizeof(buf));
      if (len > 0) {
        input_ += std::string(buf, len);
        update_filter();
      }
      break;
    }
  }

  // Keep selection visible
  int ih = 42, sy = 46;
  int max_visible = (height_ - sy - 4) / ih;
  if (selection_ < scroll_offset_)
    scroll_offset_ = selection_;
  else if (selection_ >= scroll_offset_ + max_visible)
    scroll_offset_ = std::max(0, selection_ - max_visible + 1);

  dirty_ = true;
}

void LauncherWindow::handle_button_press(int x, int y) {
  int input_height = 44;
  int item_height = 42;
  int start_y = input_height + 4;

  if (y < input_height) return;

  int slot = (y - start_y) / item_height;
  int index = scroll_offset_ + slot;
  if (slot >= 0 && index >= 0 && index < (int)filtered_.size()) {
    selection_ = index;
    launch_selected();
    running_ = false;
  }
}

void LauncherWindow::handle_axis(double value) {
  if (value > 0)
    scroll_offset_ = std::min((int)filtered_.size() - 1, scroll_offset_ + 3);
  else if (value < 0)
    scroll_offset_ = std::max(0, scroll_offset_ - 3);
  dirty_ = true;
}

void LauncherWindow::update_hover() {
  int input_height = 44;
  int item_height = 42;
  int start_y = input_height + 4;

  if (pointer_y_ < start_y) return;
  int slot = (pointer_y_ - start_y) / item_height;
  int index = scroll_offset_ + slot;
  if (slot >= 0 && index >= 0 && index < (int)filtered_.size()) {
    if (selection_ != index) {
      selection_ = index;
      dirty_ = true;
    }
  }
}

void LauncherWindow::launch_selected() {
  if (filtered_.empty()) return;
  auto &entry = filtered_[std::min(selection_, (int)filtered_.size() - 1)];

  std::string cleaned;
  for (size_t i = 0; i < entry.exec.size(); ++i) {
    if (entry.exec[i] == '%' && i + 1 < entry.exec.size()) {
      switch (entry.exec[i + 1]) {
        case 'f': case 'F': case 'u': case 'U':
        case 'd': case 'D': case 'n': case 'N':
        case 'i': case 'c': case 'k': case 'm':
          i++;
          continue;
        case '%':
          cleaned += '%';
          i++;
          continue;
      }
    }
    cleaned += entry.exec[i];
  }

  launch_background(cleaned, entry.stratum);
  save_recent(cleaned);
}

void LauncherWindow::update_filter() {
  filtered_ = search_applications(apps_, input_);
  selection_ = 0;
  scroll_offset_ = 0;
  scroll_visual_ = 0;
  if ((int)filtered_.size() > config_->max_results)
    filtered_.resize(config_->max_results);
}

void LauncherWindow::save_recent(const std::string &exec) {
  // Remove if already in recent
  recent_apps_.erase(
    std::remove(recent_apps_.begin(), recent_apps_.end(), exec),
    recent_apps_.end());
  // Add to front
  recent_apps_.insert(recent_apps_.begin(), exec);
  // Keep max 5
  if ((int)recent_apps_.size() > 5)
    recent_apps_.resize(5);
  save_recent_apps(recent_apps_);
}

void LauncherWindow::copy_to_clipboard(const std::string &text) {
  if (!data_device_manager_ || !data_device_) return;
  if (data_source_) wl_data_source_destroy(data_source_);
  data_source_ = wl_data_device_manager_create_data_source(data_device_manager_);
  wl_data_source_offer(data_source_, "text/plain;charset=utf-8");
  wl_data_source_offer(data_source_, "text/plain");
  wl_data_source_add_listener(data_source_, &data_source_listener, this);
  pending_copy_ = text;
  wl_data_device_set_selection(data_device_, data_source_, last_serial_);
}

// ── Composition (backbuffer) ────────────────────────────────────────

void LauncherWindow::compose_input_field() {
  int fx = 10, fy = 6, fw = width_ - 20, fh = 34;

  set_source_rgba(back_cr_, theme_->input_bg);
  rounded_rect(back_cr_, fx, fy, fw, fh, theme_->border_radius);
  cairo_fill(back_cr_);

  // Accent line
  set_source_rgba(back_cr_, theme_->accent);
  cairo_rectangle(back_cr_, fx + 2, fy + fh - 2, fw - 4, 2);
  cairo_fill(back_cr_);

  // Text layout
  auto layout = pango_cairo_create_layout(back_cr_);
  pango_layout_set_text(layout, input_.c_str(), input_.size());
  auto fd = pango_font_description_from_string("Sans 14");
  pango_layout_set_font_description(layout, fd);
  pango_font_description_free(fd);

  set_source_rgba(back_cr_, theme_->text);
  cairo_move_to(back_cr_, fx + 8, fy + (fh - 20) / 2);
  pango_cairo_show_layout(back_cr_, layout);

  // Cursor
  if (cursor_visible_) {
    PangoRectangle extents;
    pango_layout_get_cursor_pos(layout, input_.size(), &extents, nullptr);
    int cx = fx + 8 + extents.x / PANGO_SCALE;
    int cy = fy + (fh - 20) / 2 + extents.y / PANGO_SCALE;
    set_source_rgba(back_cr_, theme_->accent);
    cairo_rectangle(back_cr_, cx, cy, 2, extents.height / PANGO_SCALE);
    cairo_fill(back_cr_);
  }
  g_object_unref(layout);
}

void LauncherWindow::compose_results() {
  int ih = 42, sy = 46;
  int max_visible = (height_ - sy - 4) / ih;

  // Build display list: recent (if input empty) → pinned → filtered
  std::vector<const DesktopEntry*> display;
  if (input_.empty()) {
    // Recent apps first
    for (auto &r : recent_apps_) {
      for (auto &app : apps_) {
        if (app.exec == r) { display.push_back(&app); break; }
      }
    }
    // Pinned apps
    for (auto &pin : config_->pinned_apps) {
      bool found = false;
      for (auto *e : display) if (e->display_name() == pin) { found = true; break; }
      if (!found) {
        for (auto &app : apps_) {
          if (app.display_name() == pin) { display.push_back(&app); break; }
        }
      }
    }
    if (display.empty()) {
      for (auto &app : apps_) display.push_back(&app);
    }
  } else {
    for (auto &e : filtered_) display.push_back(&e);
  }

  // Clamp scroll
  int n = (int)display.size();
  int max_offset = std::max(0, n - max_visible);
  scroll_offset_ = std::clamp(scroll_offset_, 0, max_offset);

  int vis = std::min(n - scroll_offset_, max_visible);
  int end_y = sy + vis * ih;

  for (int i = 0; i < vis; ++i) {
    int idx = scroll_offset_ + i;
    if (display[idx]) {
      compose_entry_ptr(*display[idx], sy + i * ih, idx == selection_);
    }
  }

  // Scroll arrows
  auto draw_arrow = [&](int cx, int cy, bool up) {
    set_source_rgba(back_cr_, theme_->accent);
    cairo_set_line_width(back_cr_, 2);
    if (up) {
      cairo_move_to(back_cr_, cx - 4, cy + 3);
      cairo_line_to(back_cr_, cx, cy - 2);
      cairo_line_to(back_cr_, cx + 4, cy + 3);
    } else {
      cairo_move_to(back_cr_, cx - 4, cy - 3);
      cairo_line_to(back_cr_, cx, cy + 2);
      cairo_line_to(back_cr_, cx + 4, cy - 3);
    }
    cairo_stroke(back_cr_);
  };
  if (scroll_offset_ > 0)
    draw_arrow(width_ - 14, sy + 3, true);
  if (scroll_offset_ + vis < n)
    draw_arrow(width_ - 14, end_y - 3, false);
}

void LauncherWindow::compose_entry_ptr(const DesktopEntry &entry, int y, bool hovered) {
  int ix = 8, iw = width_ - 16, ih = 40;

  set_source_rgba(back_cr_, hovered ? theme_->hover_bg : theme_->alt_bg);
  rounded_rect(back_cr_, ix, y, iw, ih, 5);
  cairo_fill(back_cr_);

  // Icon
  int is = 28;
  int iix = ix + 8, iiy = y + (ih - is) / 2;
  cairo_surface_t *icon = load_icon(entry.icon, is);

  if (icon) {
    cairo_save(back_cr_);
    rounded_rect(back_cr_, iix, iiy, is, is, 4);
    cairo_clip(back_cr_);
    cairo_set_source_surface(back_cr_, icon, iix, iiy);
    cairo_paint(back_cr_);
    cairo_restore(back_cr_);
    cairo_surface_destroy(icon);
  } else {
    set_source_rgba(back_cr_, theme_->accent);
    rounded_rect(back_cr_, iix, iiy, is, is, 4);
    cairo_fill(back_cr_);
    set_source_rgba(back_cr_, theme_->bg);
    cairo_rectangle(back_cr_, iix + 6, iiy + 7, is - 12, is - 14);
    cairo_fill(back_cr_);
    set_source_rgba(back_cr_, theme_->accent);
    cairo_set_line_width(back_cr_, 1.5);
    cairo_rectangle(back_cr_, iix + 6, iiy + 7, is - 12, is - 14);
    cairo_stroke(back_cr_);
  }

  // Name
  auto layout = pango_cairo_create_layout(back_cr_);
  pango_layout_set_text(layout, entry.display_name().c_str(), entry.display_name().size());
  auto fd = pango_font_description_from_string("Sans Bold 12");
  pango_layout_set_font_description(layout, fd);
  pango_font_description_free(fd);

  set_source_rgba(back_cr_, hovered ? theme_->accent : theme_->text);
  cairo_move_to(back_cr_, iix + is + 10, y + 5);
  pango_cairo_show_layout(back_cr_, layout);

  // Subtext
  auto sub = entry.generic_name.empty() ? entry.comment : entry.generic_name;
  if (!sub.empty() && (int)sub.size() < 60) {
    pango_layout_set_text(layout, sub.c_str(), sub.size());
    auto fd2 = pango_font_description_from_string("Sans 10");
    pango_layout_set_font_description(layout, fd2);
    pango_font_description_free(fd2);
    Rgba muted = theme_->text;
    muted.a = 0.55;
    set_source_rgba(back_cr_, muted);
    cairo_move_to(back_cr_, iix + is + 10, y + 22);
    pango_cairo_show_layout(back_cr_, layout);
  }
  g_object_unref(layout);
}

void LauncherWindow::compose_metrics() {
  static NetworkSpeedometer speedo;
  static auto last_update = timestamp_ms();
  static double rx = 0, tx = 0;
  static double cpu = 0;
  static uint32_t cap = 0;
  static bool charging = false;

  auto now = timestamp_ms();
  if (now - last_update > 2000) {
    auto s = speedo.calculate_speeds();
    rx = s.first;
    tx = s.second;
    cpu = get_cpu_usage();
    auto p = get_power_status();
    cap = p.first;
    charging = p.second;
    last_update = now;
  }

  int mx = width_ - 165, my = height_ - 58, mw = 155, mh = 52;

  set_source_rgba(back_cr_, theme_->alt_bg);
  rounded_rect(back_cr_, mx, my, mw, mh, 5);
  cairo_fill(back_cr_);

  auto layout = pango_cairo_create_layout(back_cr_);
  auto fd = pango_font_description_from_string("Sans Mono 9");
  pango_layout_set_font_description(layout, fd);
  pango_font_description_free(fd);

  char netbuf[64];
  snprintf(netbuf, sizeof(netbuf), "\xe2\x86\x93 %.1f \xe2\x86\x91 %.1f KB/s", rx, tx);
  pango_layout_set_text(layout, netbuf, -1);
  set_source_rgba(back_cr_, theme_->accent);
  cairo_move_to(back_cr_, mx + 6, my + 2);
  pango_cairo_show_layout(back_cr_, layout);

  char cpubuf[24];
  snprintf(cpubuf, sizeof(cpubuf), "CPU %.0f%%", cpu);
  pango_layout_set_text(layout, cpubuf, -1);
  set_source_rgba(back_cr_, theme_->text);
  cairo_move_to(back_cr_, mx + 6, my + 20);
  pango_cairo_show_layout(back_cr_, layout);

  char battbuf[24];
  snprintf(battbuf, sizeof(battbuf), "%u%%%s", cap, charging ? " +" : "");
  pango_layout_set_text(layout, battbuf, -1);
  Rgba bc = theme_->text;
  if (cap < 20) bc = {1.0, 0.2, 0.2, 1.0};
  else if (charging) bc = {0.2, 1.0, 0.2, 1.0};
  set_source_rgba(back_cr_, bc);
  cairo_move_to(back_cr_, mx + 6, my + 38);
  pango_cairo_show_layout(back_cr_, layout);

  // App count
  char appbuf[32];
  snprintf(appbuf, sizeof(appbuf), "%d apps", (int)apps_.size());
  pango_layout_set_text(layout, appbuf, -1);
  set_source_rgba(back_cr_, theme_->text);
  cairo_move_to(back_cr_, mx + 60, my + 38);
  pango_cairo_show_layout(back_cr_, layout);

  g_object_unref(layout);
}

} // namespace orbiter