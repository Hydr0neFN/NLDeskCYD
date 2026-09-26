// Basil detail: status row, 48 h soil / tank chart with the device's own
// watering thresholds, plant-light toggle, and two guarded actions.
//
// "Water now" bypasses the device's closed loop and "clear drain fault" must
// only follow the user emptying the tray, so both are two-tap: the first tap
// arms the button for 5 s (amber, countdown), the second fires. HA also refuses
// "water" while the drain fault is latched or the tank is empty.

#include <Arduino.h>

#include "ui_common.h"

static constexpr int CX = 40, CY = 84, CW = 268, CH = 80;
static constexpr unsigned long ARM_MS = 5000, SENT_LOCK_MS = 30000, PENDING_MS = 3000;

static Series s_soil, s_tank;
static uint32_t s_seen_soil, s_seen_tank;
static float s_lines_below = NAN, s_lines_stop = NAN;

static lv_obj_t *light_btn, *light_lbl, *st_soil, *st_tank, *st_box, *chart, *line_below, *line_stop;
static lv_obj_t *banner, *clear_btn, *clear_lbl, *water_btn, *water_lbl, *auto_lbl, *empty_lbl;
static lv_chart_series_t *ser_soil, *ser_tank;

static Model s_m;
static unsigned long water_armed_until = 0, water_sent_until = 0, clear_armed_until = 0;
static bool light_pending = false, light_pending_val = false;
static unsigned long light_pending_until = 0;

static bool armed(unsigned long until) { return (long)(until - millis()) > 0; }
static bool water_allowed(const Model &m) {
  return m.mqtt_connected && m.basil_online && !m.drain_fault && !m.tank_empty;
}
static bool disp_light(const Model &m) { return light_pending ? light_pending_val : m.grow_on; }

static void on_light(lv_event_t *) {
  bool on = !disp_light(s_m);
  net_send(CmdTarget::BASIL, on ? "{\"light\":true}" : "{\"light\":false}");
  light_pending = true;
  light_pending_val = on;
  light_pending_until = millis() + PENDING_MS;
}

static void on_water(lv_event_t *) {
  if (!water_allowed(s_m) || armed(water_sent_until)) return;
  if (armed(water_armed_until)) {
    net_send(CmdTarget::BASIL, "{\"water\":true}");
    water_armed_until = 0;
    water_sent_until = millis() + SENT_LOCK_MS;
  } else {
    water_armed_until = millis() + ARM_MS;
  }
}

static void on_clear(lv_event_t *) {
  if (armed(clear_armed_until)) {
    net_send(CmdTarget::BASIL, "{\"clear_fault\":true}");
    clear_armed_until = 0;
  } else {
    clear_armed_until = millis() + ARM_MS;
  }
}

lv_obj_t *basil_build() {
  lv_obj_t *scr = mk_screen();
  mk_header(scr, "羅勒");
  light_btn = mk_button(scr, 200, 2, 116, 32, on_light, nullptr);
  light_lbl = mk_label(light_btn, &font_noto_16, C_TEXT, "");
  lv_obj_center(light_lbl);

  lv_obj_t *c = mk_card(scr, 4, 38, 312, 32, nullptr);
  lv_obj_set_style_pad_ver(c, 0, 0);
  st_soil = mk_label(c, &font_noto_16, C_TEXT, "");
  lv_obj_align(st_soil, LV_ALIGN_LEFT_MID, 0, 0);
  st_tank = mk_label(c, &font_noto_16, C_TEXT, "");
  lv_obj_align(st_tank, LV_ALIGN_CENTER, -8, 0);
  st_box = mk_label(c, &font_noto_16, C_DIM, "");
  lv_obj_align(st_box, LV_ALIGN_RIGHT_MID, 0, 0);

  mk_card(scr, 4, 74, 312, 114, nullptr);
  line_below = mk_box(scr, CX, CY, CW, 1, C_WARN);
  lv_obj_set_style_bg_opa(line_below, LV_OPA_50, 0);
  line_stop = mk_box(scr, CX, CY, CW, 1, C_OK);
  lv_obj_set_style_bg_opa(line_stop, LV_OPA_50, 0);
  chart = mk_chart(scr, CX, CY, CW, CH, HIST_MAX);
  lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  ser_soil = lv_chart_add_series(chart, C_OK, LV_CHART_AXIS_PRIMARY_Y);
  ser_tank = lv_chart_add_series(chart, C_BLUE, LV_CHART_AXIS_PRIMARY_Y);
  lv_obj_t *t = mk_label(scr, &lv_font_montserrat_14, C_DIM, "100");
  lv_obj_set_pos(t, 10, CY - 4);
  t = mk_label(scr, &lv_font_montserrat_14, C_DIM, "0");
  lv_obj_set_pos(t, 10, CY + CH - 12);
  t = mk_label(scr, &lv_font_montserrat_14, C_DIM, "-48h");
  lv_obj_set_pos(t, CX, CY + CH + 4);
  t = mk_label(scr, &font_noto_16, C_OK, "土壤");
  lv_obj_set_pos(t, CX + 90, CY + CH + 2);
  t = mk_label(scr, &font_noto_16, C_BLUE, "水箱");
  lv_obj_set_pos(t, CX + 142, CY + CH + 2);
  t = mk_label(scr, &lv_font_montserrat_14, C_DIM, "now");
  lv_obj_set_pos(t, CX + CW - 26, CY + CH + 4);
  empty_lbl = mk_label(scr, &font_noto_16, C_DIM, "--");
  lv_obj_set_pos(empty_lbl, CX + CW / 2 - 8, CY + CH / 2 - 10);

  // Drain-fault banner: covers the top of the chart only while latched.
  banner = mk_box(scr, 4, 74, 312, 40, C_ALERT);
  lv_obj_set_style_radius(banner, 8, 0);
  t = mk_label(banner, &font_noto_16, C_BG, "排水異常 已停機");
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
  clear_btn = mk_button(banner, 172, 4, 134, 32, on_clear, nullptr);
  clear_lbl = mk_label(clear_btn, &font_noto_16, C_TEXT, "");
  lv_obj_center(clear_lbl);
  lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);

  water_btn = mk_button(scr, 4, 192, 154, 44, on_water, nullptr);
  water_lbl = mk_label(water_btn, &font_noto_20, C_TEXT, "");
  lv_obj_center(water_lbl);
  auto_lbl = mk_label(scr, &font_noto_16, C_DIM, "");
  lv_obj_set_pos(auto_lbl, 168, 204);
  return scr;
}

void basil_refresh(const Model &m, bool live) {
  s_m = m;
  unsigned long now = millis();
  char buf[32], num[12];

  bool soil_new = net_hist(H_SOIL, s_soil, s_seen_soil);
  bool tank_new = net_hist(H_TANK, s_tank, s_seen_tank);
  if (soil_new || tank_new) {
    int lo, hi;
    bool any = (s_soil.n && series_range(s_soil, lo, hi)) || (s_tank.n && series_range(s_tank, lo, hi));
    set_hidden(empty_lbl, any);
    if (s_soil.n) chart_load(chart, ser_soil, s_soil);
    if (s_tank.n) chart_load(chart, ser_tank, s_tank);
  }
  // The device's own thresholds: waters below the amber line, stops at the green.
  if (m.water_below != s_lines_below || m.stop_at != s_lines_stop) {
    s_lines_below = m.water_below;
    s_lines_stop = m.stop_at;
    chart_hline(line_below, CX, CY, CW, CH, isnan(m.water_below) ? -1 : (int)m.water_below, 0, 100);
    chart_hline(line_stop, CX, CY, CW, CH, isnan(m.stop_at) ? -1 : (int)m.stop_at, 0, 100);
  }

  // plant light (the box's only light source)
  if (light_pending && (m.grow_on == light_pending_val || (long)(now - light_pending_until) >= 0))
    light_pending = false;
  bool lon = disp_light(m);
  set_text(light_lbl, lon ? "植物燈 開" : "植物燈 關");
  set_bg(light_btn, lon ? C_ACCENT : C_CARD);
  set_text_color(light_lbl, lon ? C_BG : (m.basil_online ? C_TEXT : C_STALE));

  // status row
  bool b_live = live && m.basil_online;
  fmt(num, sizeof(num), m.soil, 0);
  snprintf(buf, sizeof(buf), "土壤 %s%%", num);
  set_text(st_soil, buf);
  set_text_color(st_soil, !b_live ? C_STALE : (m.need_water ? C_WARN : C_OK));
  fmt(num, sizeof(num), m.tank, 0);
  snprintf(buf, sizeof(buf), "水箱 %s%%", num);
  set_text(st_tank, buf);
  set_text_color(st_tank, !b_live ? C_STALE : (m.tank_empty ? C_ALERT : (m.tank < 20 ? C_WARN : C_BLUE)));
  if (!m.basil_online) {
    set_text(st_box, "離線");
  } else {
    char t2[8];
    fmt(num, sizeof(num), m.box_temp, 1);
    fmt(t2, sizeof(t2), m.box_hum, 0);
    snprintf(buf, sizeof(buf), "箱內 %s° %s%%", num, t2);
    set_text(st_box, buf);
  }

  // drain fault banner + two-tap clear
  set_hidden(banner, !(live && m.drain_fault));
  bool carm = armed(clear_armed_until);
  if (carm) snprintf(buf, sizeof(buf), "確認已排空? %lu", (clear_armed_until - now) / 1000 + 1);
  set_text(clear_lbl, carm ? buf : "清除異常");
  set_bg(clear_btn, carm ? C_ACCENT : C_CARD);
  set_text_color(clear_lbl, carm ? C_BG : C_TEXT);

  // water now: disabled -> armed (countdown) -> sent (30 s lock)
  bool ok = water_allowed(m);
  bool warm = ok && armed(water_armed_until);
  if (!ok) {
    set_text(water_lbl, m.tank_empty ? "水箱缺水" : (m.drain_fault ? "排水異常" : "無法澆水"));
    set_text_color(water_lbl, C_STALE);
    set_bg(water_btn, C_CARD);
  } else if (armed(water_sent_until)) {
    set_text(water_lbl, "已送出");
    set_text_color(water_lbl, C_DIM);
    set_bg(water_btn, C_CARD);
  } else if (warm) {
    snprintf(buf, sizeof(buf), "確認澆水? %lu", (water_armed_until - now) / 1000 + 1);
    set_text(water_lbl, buf);
    set_text_color(water_lbl, C_BG);
    set_bg(water_btn, C_ACCENT);
  } else {
    set_text(water_lbl, "立即澆水");
    set_text_color(water_lbl, C_TEXT);
    set_bg(water_btn, C_CARD);
  }

  if (isnan(m.water_below) || isnan(m.stop_at)) {
    set_text(auto_lbl, "自動澆水");
  } else {
    snprintf(buf, sizeof(buf), "自動澆水 %.0f-%.0f%%", m.water_below, m.stop_at);
    set_text(auto_lbl, buf);
  }
}
