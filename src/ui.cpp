// Page dispatcher. Pages (instant switch only -- no TE pin on this board, any
// animated transition tears):
//
//   HOME ─┬─ tap room card   ─> ROOM   (24 h temp / humidity / CO2 chart)
//         ├─ tap light card  ─> LIGHT  (light-bar steppers, presets)
//         ├─ tap power card  ─> POWER  (24 h W chart, 7-day kWh bars)
//         ├─ tap basil card  ─> BASIL  (48 h soil / tank chart, controls)
//         ├─ long-press clock ─> SWATCH (panel colour check)
//         └─ long-press weather ─> FORECAST (next 12 hours)
//
// Layout and palette follow two agy design reviews (2026-09-26).

#include "ui.h"

#include <Arduino.h>
#include <time.h>

#include "config.h"
#include "ui_common.h"

static lv_obj_t *s_screens[(int)Page::COUNT];
static Page s_page = Page::HOME;
static lv_obj_t *o_ota, *o_ota_lbl;

void ui_show(Page p) {
  s_page = p;
  lv_screen_load(s_screens[(int)p]);
}

Page ui_page() { return s_page; }

void ui_go_home() {
  if (s_page != Page::HOME) ui_show(Page::HOME);
}

void ui_init() {
  s_screens[(int)Page::HOME] = home_build();
  s_screens[(int)Page::LIGHT] = light_build();
  s_screens[(int)Page::ROOM] = room_build();
  s_screens[(int)Page::POWER] = power_build();
  s_screens[(int)Page::BASIL] = basil_build();
  s_screens[(int)Page::SWATCH] = swatch_build();
  s_screens[(int)Page::FORECAST] = forecast_build();

  // OTA overlay on the top layer. Clickable, so touches during a flash are
  // absorbed instead of reaching the (invisible) page underneath.
  o_ota = mk_box(lv_layer_top(), 0, 0, 320, 240, C_BG);
  lv_obj_add_flag(o_ota, LV_OBJ_FLAG_CLICKABLE);
  o_ota_lbl = mk_label(o_ota, &font_noto_20, C_TEXT, "");
  lv_obj_center(o_ota_lbl);
  lv_obj_add_flag(o_ota, LV_OBJ_FLAG_HIDDEN);

  ui_show(Page::HOME);
}

bool ui_is_night() {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // SNTP not synced yet
  struct tm tm;
  localtime_r(&now, &tm);
  int m = tm.tm_hour * 60 + tm.tm_min;
  return NIGHT_START_MIN < NIGHT_END_MIN ? (m >= NIGHT_START_MIN && m < NIGHT_END_MIN)
                                         : (m >= NIGHT_START_MIN || m < NIGHT_END_MIN);
}

bool ui_has_alert(const Model &m) {
  return m.drain_fault || m.tank_empty || (!isnan(m.co2) && m.co2 >= CO2_ALERT);
}

void ui_refresh(const Model &m) {
  light_tick(m);

  if (m.ota_active) {
    char buf[24];
    snprintf(buf, sizeof(buf), "更新中 %d%%", m.ota_percent);
    set_text(o_ota_lbl, buf);
    set_hidden(o_ota, false);
    return;
  }

  bool live = m.mqtt_connected;
  switch (s_page) {
    case Page::HOME: home_refresh(m, live); break;
    case Page::LIGHT: light_refresh(m); break;
    case Page::ROOM: room_refresh(m, live); break;
    case Page::POWER: power_refresh(m, live); break;
    case Page::BASIL: basil_refresh(m, live); break;
    case Page::SWATCH: swatch_refresh(); break;
    case Page::FORECAST: forecast_refresh(); break;
    case Page::COUNT: break;
  }
}
