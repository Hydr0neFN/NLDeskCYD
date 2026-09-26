// Light-bar detail: on/off, brightness and colour-temp steppers over
// 15-segment bars (the bar has 15 hardware levels of each), four presets.
// Steppers, not sliders: resistive touch jitters and drops drags.

#include <Arduino.h>
#include <math.h>

#include "ui_common.h"

// lightbar2mqtt maps mireds linearly onto the bar's 15 colour levels, so the
// steps are equal in mireds (153-370), not in kelvin.
static constexpr int LEVELS = 15;
static constexpr float MIRED_WARM = 370.0f, MIRED_COOL = 153.0f;
static constexpr float MIRED_STEP = (MIRED_WARM - MIRED_COOL) / (LEVELS - 1);

static int ct_idx_from_kelvin(int k) {
  float mired = 1e6f / (float)(k > 0 ? k : 4000);
  return constrain((int)lroundf((MIRED_WARM - mired) / MIRED_STEP), 0, LEVELS - 1);
}
static int kelvin_from_ct_idx(int i) { return (int)lroundf(1e6f / (MIRED_WARM - i * MIRED_STEP)); }

// --- Optimistic state --------------------------------------------------------
// Commands take 0.2-2 s to show up in HA's state. Local taps repaint at once;
// the pending value wins until HA agrees or it expires. Stepper taps are
// coalesced and sent 350 ms after the last one, so a burst of taps costs one
// radio command instead of one per tap.
struct Pending {
  bool active = false;
  int value = 0;
  unsigned long expire = 0;
};
static Pending p_on, p_level, p_ct;
static bool s_dirty_level = false, s_dirty_ct = false;
static unsigned long s_send_due = 0;
static constexpr unsigned long SEND_DELAY_MS = 350;
static constexpr unsigned long PENDING_MS = 3000;

static Model s_m;  // last model seen, for event handlers

bool light_disp_on(const Model &m) { return p_on.active ? p_on.value : m.lb_on; }
int light_disp_level(const Model &m) { return p_level.active ? p_level.value : m.lb_level; }
static int disp_ct(const Model &m) { return p_ct.active ? p_ct.value : ct_idx_from_kelvin(m.lb_kelvin); }
int light_disp_kelvin(const Model &m) { return (kelvin_from_ct_idx(disp_ct(m)) + 50) / 100 * 100; }

static void pend(Pending &p, int v) {
  p.active = true;
  p.value = v;
  p.expire = millis() + PENDING_MS;
}

static void send_on(bool on) {
  if (!on) {
    // An unsent stepper change would otherwise go out after this and turn the
    // bar straight back on (any level command is light.turn_on).
    s_dirty_level = s_dirty_ct = false;
    p_level.active = p_ct.active = false;
  }
  char j[32];
  snprintf(j, sizeof(j), "{\"on\":%s}", on ? "true" : "false");
  net_send(CmdTarget::LIGHTBAR, j);
  pend(p_on, on);
}

void light_toggle(const Model &m) { send_on(!light_disp_on(m)); }

static void queue_level(int level) {
  pend(p_level, constrain(level, 1, LEVELS));
  pend(p_on, 1);
  s_dirty_level = true;
  s_send_due = millis() + SEND_DELAY_MS;
}

static void queue_ct(int idx) {
  pend(p_ct, constrain(idx, 0, LEVELS - 1));
  pend(p_on, 1);
  s_dirty_ct = true;
  s_send_due = millis() + SEND_DELAY_MS;
}

void light_tick(const Model &m) {
  unsigned long now = millis();
  if ((s_dirty_level || s_dirty_ct) && (long)(now - s_send_due) >= 0) {
    char j[64];
    if (s_dirty_level && s_dirty_ct) {
      snprintf(j, sizeof(j), "{\"level\":%d,\"kelvin\":%d}", p_level.value, kelvin_from_ct_idx(p_ct.value));
    } else if (s_dirty_level) {
      snprintf(j, sizeof(j), "{\"level\":%d}", p_level.value);
    } else {
      snprintf(j, sizeof(j), "{\"kelvin\":%d}", kelvin_from_ct_idx(p_ct.value));
    }
    net_send(CmdTarget::LIGHTBAR, j);
    // Pending values stay authoritative for PENDING_MS after the send, not the
    // tap, so a slow radio round-trip does not flash the old value back.
    unsigned long exp = now + PENDING_MS;
    if (s_dirty_level) p_level.expire = exp;
    if (s_dirty_ct) p_ct.expire = exp;
    p_on.expire = exp;
    s_dirty_level = s_dirty_ct = false;
  }

  bool waiting = s_dirty_level || s_dirty_ct;
  if (p_on.active && ((!waiting && m.lb_on == (bool)p_on.value) || (long)(now - p_on.expire) >= 0))
    p_on.active = false;
  if (p_level.active && !s_dirty_level && (m.lb_level == p_level.value || (long)(now - p_level.expire) >= 0))
    p_level.active = false;
  if (p_ct.active && !s_dirty_ct &&
      (ct_idx_from_kelvin(m.lb_kelvin) == p_ct.value || (long)(now - p_ct.expire) >= 0))
    p_ct.active = false;
}

// --- Widgets ---------------------------------------------------------------------
static lv_obj_t *d_toggle, *d_toggle_lbl, *d_level_val, *d_ct_val;
static lv_obj_t *d_bri_seg[LEVELS], *d_ct_seg[LEVELS];

static void on_toggle(lv_event_t *) { light_toggle(s_m); }
static void on_bri_step(lv_event_t *e) { queue_level(light_disp_level(s_m) + (int)(intptr_t)lv_event_get_user_data(e)); }
static void on_ct_step(lv_event_t *e) { queue_ct(disp_ct(s_m) + (int)(intptr_t)lv_event_get_user_data(e)); }

// Tap on a segmented bar jumps to that segment.
static int seg_from_touch(lv_event_t *e) {
  lv_indev_t *indev = lv_event_get_indev(e);
  if (!indev) indev = lv_indev_active();
  if (!indev) return -1;
  lv_obj_t *bar = (lv_obj_t *)lv_event_get_current_target(e);
  lv_point_t pt;
  lv_indev_get_point(indev, &pt);
  lv_area_t a;
  lv_obj_get_coords(bar, &a);
  int w = lv_area_get_width(&a);
  return constrain((pt.x - a.x1) * LEVELS / (w > 0 ? w : 1), 0, LEVELS - 1);
}
static void on_bri_bar(lv_event_t *e) {
  int s = seg_from_touch(e);
  if (s >= 0) queue_level(s + 1);
}
static void on_ct_bar(lv_event_t *e) {
  int s = seg_from_touch(e);
  if (s >= 0) queue_ct(s);
}

struct Preset {
  const char *name;
  int level;
  int kelvin;
};
static const Preset PRESETS[] = {
    {"閱讀", 15, 5000},
    {"工作", 11, 4000},
    {"放鬆", 5, 2700},
    {"夜燈", 1, 2700},
};
static void on_preset(lv_event_t *e) {
  const Preset &p = PRESETS[(intptr_t)lv_event_get_user_data(e)];
  pend(p_level, p.level);
  pend(p_ct, ct_idx_from_kelvin(p.kelvin));
  pend(p_on, 1);
  s_dirty_level = s_dirty_ct = true;
  s_send_due = millis();  // presets are one deliberate tap, send now
}

static void mk_seg_bar(lv_obj_t *parent, int x, int y, lv_obj_t **segs, lv_event_cb_t cb) {
  // 15 x 11 px segments, 2 px gaps = 193 px
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, 193, 28);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bar, cb, LV_EVENT_CLICKED, nullptr);
  for (int i = 0; i < LEVELS; i++) {
    segs[i] = mk_box(bar, i * 13, 0, 11, 28, C_SEG_OFF);
    lv_obj_set_style_radius(segs[i], 2, 0);
  }
}

lv_obj_t *light_build() {
  lv_obj_t *scr = mk_screen();
  mk_header(scr, "螢幕掛燈");
  d_toggle = mk_button(scr, 244, 2, 72, 32, on_toggle, nullptr);
  d_toggle_lbl = mk_label(d_toggle, &font_noto_20, C_TEXT, "");
  lv_obj_center(d_toggle_lbl);

  // brightness
  lv_obj_t *t = mk_label(scr, &font_noto_16, C_DIM, "亮度");
  lv_obj_set_pos(t, 8, 40);
  d_level_val = mk_label(scr, &lv_font_montserrat_14, C_DIM, "");
  lv_obj_align(d_level_val, LV_ALIGN_TOP_RIGHT, -8, 42);
  lv_obj_t *b = mk_button(scr, 8, 60, 48, 44, on_bri_step, (void *)(intptr_t)-1);
  t = mk_label(b, &lv_font_montserrat_20, C_TEXT, LV_SYMBOL_MINUS);
  lv_obj_center(t);
  mk_seg_bar(scr, 63, 68, d_bri_seg, on_bri_bar);
  b = mk_button(scr, 264, 60, 48, 44, on_bri_step, (void *)(intptr_t)1);
  t = mk_label(b, &lv_font_montserrat_20, C_TEXT, LV_SYMBOL_PLUS);
  lv_obj_center(t);

  // colour temperature
  t = mk_label(scr, &font_noto_16, C_DIM, "色溫");
  lv_obj_set_pos(t, 8, 116);
  d_ct_val = mk_label(scr, &lv_font_montserrat_14, C_DIM, "");
  lv_obj_align(d_ct_val, LV_ALIGN_TOP_RIGHT, -8, 118);
  b = mk_button(scr, 8, 136, 48, 44, on_ct_step, (void *)(intptr_t)-1);
  t = mk_label(b, &font_noto_20, C_WARM, "暖");
  lv_obj_center(t);
  mk_seg_bar(scr, 63, 144, d_ct_seg, on_ct_bar);
  for (int i = 0; i < LEVELS; i++) {
    lv_obj_set_style_bg_color(d_ct_seg[i], lv_color_mix(C_COOL, C_WARM, i * 255 / (LEVELS - 1)), 0);
    lv_obj_set_style_border_color(d_ct_seg[i], C_TEXT, 0);
  }
  b = mk_button(scr, 264, 136, 48, 44, on_ct_step, (void *)(intptr_t)1);
  t = mk_label(b, &font_noto_20, C_COOL, "冷");
  lv_obj_center(t);

  // presets
  for (int i = 0; i < 4; i++) {
    b = mk_button(scr, 6 + i * 78, 192, 74, 42, on_preset, (void *)(intptr_t)i);
    t = mk_label(b, &font_noto_20, C_TEXT, PRESETS[i].name);
    lv_obj_center(t);
  }
  return scr;
}

void light_refresh(const Model &m) {
  s_m = m;
  bool on = light_disp_on(m);
  int lvl = light_disp_level(m);
  int ct = disp_ct(m);
  char buf[24];

  set_bg(d_toggle, on ? C_ACCENT : C_CARD);
  set_text_color(d_toggle_lbl, on ? C_BG : C_TEXT);
  set_text(d_toggle_lbl, on ? "開" : "關");

  snprintf(buf, sizeof(buf), "%d / 15", lvl);
  set_text(d_level_val, buf);
  snprintf(buf, sizeof(buf), "%d K", light_disp_kelvin(m));
  set_text(d_ct_val, buf);

  for (int i = 0; i < LEVELS; i++) {
    set_bg(d_bri_seg[i], i < lvl ? (on ? C_ACCENT : C_STALE) : C_SEG_OFF);
    bool sel = i == ct;
    set_bg_opa(d_ct_seg[i], sel ? LV_OPA_COVER : LV_OPA_40);
    set_border_w(d_ct_seg[i], sel ? 2 : 0);
  }
}
