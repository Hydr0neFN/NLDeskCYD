// Home: animated weather sky (ui_sky.cpp) under a header (link dot, time,
// date, outdoor weather or alert pill) and a 2x2 grid of cards, each opening
// its detail page.

#include <Arduino.h>
#include <time.h>

#include "config.h"
#include "ui_common.h"

static lv_obj_t *h_time, *h_date, *h_dot, *h_pill, *h_pill_lbl, *h_wx;
static lv_obj_t *r_temp, *r_hum, *r_co2;
static lv_obj_t *l_state, *l_level, *l_kelvin, *l_bar_fill;
static lv_obj_t *p_today, *p_cost, *p_house, *p_pc;
static lv_obj_t *b_status, *b_soil, *b_tank;
static lv_obj_t *s_ui, *s_idle, *i_alert, *i_alert_lbl, *i_time, *i_date, *i_in, *i_in_sub, *i_out, *i_out_sub, *i_div;
static bool s_idle_on = false;
static void build_idle(lv_obj_t *root);

static const char *const WEEKDAY[7] = {"日", "一", "二", "三", "四", "五", "六"};

static void on_open(lv_event_t *e) { ui_show((Page)(intptr_t)lv_event_get_user_data(e)); }

// Card-shaped button with a dim title; tapping opens `page`.
static lv_obj_t *nav_card(lv_obj_t *scr, int x, int y, int w, int h, const char *title, Page page) {
  lv_obj_t *c = mk_button(scr, x, y, w, h, on_open, (void *)(intptr_t)page);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_80, 0);  // the sky shows through, text stays legible
  lv_obj_t *t = mk_label(c, &font_noto_16, C_DIM, title);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, -3);
  return c;
}

// One "label ... big number unit" row inside a card.
static lv_obj_t *value_row(lv_obj_t *card, int y, const char *label, const char *unit) {
  lv_obj_t *t = mk_label(card, &font_noto_16, C_DIM, label);
  lv_obj_set_pos(t, 0, y + 8);
  lv_obj_t *v = mk_label(card, &lv_font_montserrat_28, C_TEXT, "--");
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -18, y);
  t = mk_label(card, &lv_font_montserrat_14, C_DIM, unit);
  lv_obj_align(t, LV_ALIGN_TOP_RIGHT, 0, y + 12);
  return v;
}

lv_obj_t *home_build() {
  lv_obj_t *scr = mk_screen();

  sky_build(scr);
  lv_obj_t *root = scr;
  // Everything interactive (header + cards) lives in s_ui so the idle layer
  // can swap it out while the sky keeps running underneath.
  s_ui = lv_obj_create(root);
  lv_obj_remove_style_all(s_ui);
  lv_obj_set_size(s_ui, 320, 240);
  lv_obj_remove_flag(s_ui, LV_OBJ_FLAG_SCROLLABLE);
  scr = s_ui;
  // Dark band under the header so white text stays readable on a bright sky.
  lv_obj_t *scrim = mk_box(scr, 0, 0, 320, 36, lv_color_black());
  lv_obj_set_style_bg_opa(scrim, LV_OPA_30, 0);

  // Header text is bold and centred on the clock's midline. 16 px, not 20:
  // clock + date + "13° 晴時多雲" do not fit 320 px at 20 px.
  const int mid = 2 + lv_font_get_line_height(&lv_font_montserrat_28) / 2;
  const int y16 = mid - lv_font_get_line_height(&font_noto_16_bold) / 2;
  h_time = mk_label(scr, &lv_font_montserrat_28, C_TEXT, "--:--");
  lv_obj_set_pos(h_time, 8, 2);
  h_date = mk_label(scr, &font_noto_16_bold, C_TEXT, "");
  lv_obj_set_pos(h_date, 110, y16);  // re-placed beside the clock on every change
  // Link dot, top-right corner, only shown when something is wrong.
  h_dot = mk_box(scr, 312, 2, 6, 6, C_ALERT);
  lv_obj_set_style_radius(h_dot, LV_RADIUS_CIRCLE, 0);
  // Long-press the time for the panel colour check.
  lv_obj_t *hot = lv_obj_create(scr);
  lv_obj_remove_style_all(hot);
  lv_obj_set_pos(hot, 0, 0);
  lv_obj_set_size(hot, 100, 36);
  lv_obj_add_flag(hot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(hot, on_open, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)Page::SWATCH);

  // Long-press the outdoor weather for the hourly forecast.
  lv_obj_t *wx_hot = lv_obj_create(scr);
  lv_obj_remove_style_all(wx_hot);
  lv_obj_set_pos(wx_hot, 190, 0);
  lv_obj_set_size(wx_hot, 130, 36);
  lv_obj_add_flag(wx_hot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wx_hot, on_open, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)Page::FORECAST);

  h_wx = mk_label(scr, &font_noto_16_bold, C_TEXT, "");
  lv_obj_align(h_wx, LV_ALIGN_TOP_RIGHT, -10, y16);

  // Alerts take the weather's place: they matter more, and the sky still
  // shows the weather.
  h_pill = mk_box(scr, 196, mid - 13, 120, 26, C_ALERT);
  lv_obj_set_style_radius(h_pill, 12, 0);
  h_pill_lbl = mk_label(h_pill, &font_noto_16_bold, C_BG, "");
  lv_obj_center(h_pill_lbl);
  lv_obj_add_flag(h_pill, LV_OBJ_FLAG_HIDDEN);

  // Room
  lv_obj_t *c = nav_card(scr, 4, 38, 154, 96, "室內", Page::ROOM);
  r_hum = mk_label(c, &lv_font_montserrat_20, C_TEXT, "--%");
  lv_obj_align(r_hum, LV_ALIGN_TOP_RIGHT, 0, -4);
  r_temp = mk_label(c, &lv_font_montserrat_28, C_TEXT, "--");
  lv_obj_align(r_temp, LV_ALIGN_TOP_LEFT, 0, 20);
  lv_obj_t *t = mk_label(c, &lv_font_montserrat_14, C_DIM, "CO2");
  lv_obj_align(t, LV_ALIGN_BOTTOM_LEFT, 0, -2);
  r_co2 = mk_label(c, &lv_font_montserrat_20, C_TEXT, "-- ppm");
  lv_obj_align(r_co2, LV_ALIGN_BOTTOM_RIGHT, 0, 2);

  // Light bar
  c = nav_card(scr, 162, 38, 154, 96, "掛燈", Page::LIGHT);
  l_state = mk_label(c, &font_noto_16, C_DIM, "");
  lv_obj_align(l_state, LV_ALIGN_TOP_RIGHT, 0, -3);
  l_level = mk_label(c, &font_noto_20, C_TEXT, "");
  lv_obj_align(l_level, LV_ALIGN_TOP_LEFT, 0, 18);
  l_kelvin = mk_label(c, &font_noto_16, C_DIM, "");
  lv_obj_align(l_kelvin, LV_ALIGN_TOP_LEFT, 0, 44);
  lv_obj_t *track = mk_box(c, 0, 72, 138, 6, C_SEG_OFF);
  lv_obj_set_style_radius(track, 3, 0);
  l_bar_fill = mk_box(track, 0, 0, 0, 6, C_ACCENT);
  lv_obj_set_style_radius(l_bar_fill, 3, 0);

  // Power: today's kWh lives in the title row, so each value row has room.
  c = nav_card(scr, 4, 138, 154, 98, "用電", Page::POWER);
  p_cost = mk_label(c, &font_noto_16, C_ACCENT, "");
  lv_obj_align(p_cost, LV_ALIGN_TOP_RIGHT, 0, -3);
  p_today = mk_label(c, &font_noto_16, C_DIM, "今日");
  lv_obj_add_flag(p_today, LV_OBJ_FLAG_HIDDEN);
  p_house = value_row(c, 18, "全屋", "W");
  p_pc = value_row(c, 50, "電腦", "W");

  // Basil
  c = nav_card(scr, 162, 138, 154, 98, "羅勒", Page::BASIL);
  b_status = mk_label(c, &font_noto_16, C_DIM, "");
  lv_obj_align(b_status, LV_ALIGN_TOP_RIGHT, 0, -3);
  b_soil = value_row(c, 18, "土壤", "%");
  b_tank = value_row(c, 50, "水箱", "%");

  build_idle(root);
  return root;
}

// --- Idle layer ---------------------------------------------------------------
// Shown instead of the header + cards when nobody has touched the panel for
// IDLE_TIMEOUT_MS by day (backlight at half): big clock, date, indoor and
// outdoor at a glance, alerts only when active. Layout from an agy review
// (2026-09-27), plus CO2 in the indoor column. A 50 % black scrim keeps text
// legible on the sky at half backlight.

static void build_idle(lv_obj_t *root) {
  s_idle = mk_box(root, 0, 0, 320, 240, lv_color_black());
  lv_obj_set_style_bg_opa(s_idle, LV_OPA_50, 0);
  lv_obj_add_flag(s_idle, LV_OBJ_FLAG_HIDDEN);

  i_alert = mk_box(s_idle, 0, 8, 10, 24, C_ALERT);  // width set on refresh
  lv_obj_set_style_radius(i_alert, 12, 0);
  i_alert_lbl = mk_label(i_alert, &font_noto_16_bold, C_BG, "");
  lv_obj_center(i_alert_lbl);
  lv_obj_add_flag(i_alert, LV_OBJ_FLAG_HIDDEN);

  i_time = mk_label(s_idle, &lv_font_montserrat_48, C_TEXT, "--:--");
  lv_obj_align(i_time, LV_ALIGN_TOP_MID, 0, 46);
  i_date = mk_label(s_idle, &font_noto_20, C_SUB, "");
  lv_obj_align(i_date, LV_ALIGN_TOP_MID, 0, 112);

  // Two columns either side of a divider. Positions are set per refresh from
  // the text widths (layout_idle_row) so the pair sits centred as a group with
  // equal gaps at the divider; fixed half-width columns looked lopsided.
  i_div = mk_box(s_idle, 160, 170, 1, 48, C_DIM);
  lv_obj_set_style_bg_opa(i_div, LV_OPA_50, 0);

  i_in = mk_label(s_idle, &font_noto_20_bold, C_TEXT, "");
  i_in_sub = mk_label(s_idle, &font_noto_16, C_SUB, "");
  i_out = mk_label(s_idle, &font_noto_20_bold, C_TEXT, "");
  i_out_sub = mk_label(s_idle, &font_noto_16, C_SUB, "室外");
}

static void layout_idle_row() {
  static constexpr int GAP = 16, Y_MAIN = 168, Y_SUB = 196;
  lv_obj_update_layout(s_idle);
  int l = LV_MAX(lv_obj_get_width(i_in), lv_obj_get_width(i_in_sub));
  int r = LV_MAX(lv_obj_get_width(i_out), lv_obj_get_width(i_out_sub));
  int x0 = (320 - (l + GAP + 1 + GAP + r)) / 2;
  if (x0 < 4) x0 = 4;
  int div_x = x0 + l + GAP;
  int lc = x0 + l / 2, rc = div_x + 1 + GAP + r / 2;
  lv_obj_set_pos(i_in, lc - lv_obj_get_width(i_in) / 2, Y_MAIN);
  lv_obj_set_pos(i_in_sub, lc - lv_obj_get_width(i_in_sub) / 2, Y_SUB);
  lv_obj_set_pos(i_out, rc - lv_obj_get_width(i_out) / 2, Y_MAIN);
  lv_obj_set_pos(i_out_sub, rc - lv_obj_get_width(i_out_sub) / 2, Y_SUB);
  lv_obj_set_x(i_div, div_x);
}

void home_set_idle(bool idle) {
  if (!s_idle || s_idle_on == idle) return;
  s_idle_on = idle;
  set_hidden(s_ui, idle);
  set_hidden(s_idle, !idle);
}

static void refresh_idle(const Model &m, bool live) {
  char buf[48], num[12], num2[12];
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    set_text(i_time, buf);
    snprintf(buf, sizeof(buf), "%d月%d日 週%s", tm.tm_mon + 1, tm.tm_mday, WEEKDAY[tm.tm_wday]);
    set_text(i_date, buf);
  }

  fmt(num, sizeof(num), m.temp, 1);
  fmt(num2, sizeof(num2), m.hum, 0);
  snprintf(buf, sizeof(buf), "%s°  %s%%", num, num2);
  set_text(i_in, buf);
  fmt(num, sizeof(num), m.co2, 0);
  snprintf(buf, sizeof(buf), "室內  CO2 %s", num);
  set_text(i_in_sub, buf);
  lv_color_t co2c = C_SUB;
  if (live && !isnan(m.co2)) co2c = m.co2 >= CO2_ALERT ? C_ALERT : (m.co2 >= CO2_WARN ? C_WARN : C_SUB);
  set_text_color(i_in_sub, co2c);

  if (m.wx_cond[0] && !isnan(m.out_temp)) {
    snprintf(buf, sizeof(buf), "%.0f°  %s", m.out_temp, sky_label(m.wx_cond));
    set_text(i_out, buf);
  } else {
    set_text(i_out, "--");
  }
  layout_idle_row();

  // Alerts only when active, same priority as the home pill.
  const char *a = nullptr;
  lv_color_t ac = C_ALERT;
  if (!m.mqtt_connected) a = "離線";
  else if (m.drain_fault) a = "羅勒排水異常";
  else if (m.tank_empty) a = "羅勒水箱缺水";
  else if (!isnan(m.co2) && m.co2 >= CO2_ALERT) a = "CO2 過高 請開窗";
  else if (m.need_water) { a = "羅勒需澆水"; ac = C_WARN; }
  if (a) {
    set_text(i_alert_lbl, a);
    set_bg(i_alert, ac);
    lv_obj_update_layout(i_alert_lbl);
    int w = lv_obj_get_width(i_alert_lbl) + 24;
    set_width(i_alert, w);
    lv_obj_set_x(i_alert, (320 - w) / 2);
  }
  set_hidden(i_alert, a == nullptr);
}

static void refresh_header(const Model &m, bool live) {
  char buf[40];
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    if (strcmp(lv_label_get_text(h_time), buf) != 0) {
      set_text(h_time, buf);
      lv_obj_update_layout(h_time);
      lv_obj_set_x(h_date, lv_obj_get_x(h_time) + lv_obj_get_width(h_time) + 10);
    }
    snprintf(buf, sizeof(buf), "%d/%d 週%s", tm.tm_mon + 1, tm.tm_mday, WEEKDAY[tm.tm_wday]);
    set_text(h_date, buf);
  } else {
    set_text(h_time, "--:--");
    set_text(h_date, "");
  }

  // red = no broker; amber = connected but quiet for 3 min (the house meter
  // normally publishes every few seconds); hidden when all is well.
  bool quiet = millis() - m.last_msg_ms > 180000;
  set_bg(h_dot, !m.mqtt_connected ? C_ALERT : C_WARN);
  set_hidden(h_dot, m.mqtt_connected && !quiet);

  // One pill, highest priority first. Everything else stays on its card.
  const char *pill = nullptr;
  lv_color_t pc = C_ALERT;
  if (live && m.drain_fault) pill = "排水異常";
  else if (live && m.tank_empty) pill = "水箱缺水";
  else if (live && !isnan(m.co2) && m.co2 >= CO2_ALERT) pill = "CO2 過高";
  else if (live && m.need_water) { pill = "需澆水"; pc = C_WARN; }
  else if (live && !m.basil_online) { pill = "羅勒離線"; pc = C_WARN; }
  if (pill) {
    set_text(h_pill_lbl, pill);
    set_bg(h_pill, pc);
  }
  set_hidden(h_pill, pill == nullptr);

  if (m.wx_cond[0] && !isnan(m.out_temp)) {
    snprintf(buf, sizeof(buf), "%.0f° %s", m.out_temp, sky_label(m.wx_cond));
    set_text(h_wx, buf);
  } else {
    set_text(h_wx, "");
  }
  set_hidden(h_wx, pill != nullptr);
}

void home_refresh(const Model &m, bool live) {
  sky_update(m);
  if (s_idle_on) {
    refresh_idle(m, live);
    return;
  }
  refresh_header(m, live);

  char buf[24], num[12];
  lv_color_t text = live ? C_TEXT : C_STALE;

  // room
  fmt(num, sizeof(num), m.temp, 1);
  snprintf(buf, sizeof(buf), isnan(m.temp) ? "%s" : "%s°C", num);
  set_text(r_temp, buf);
  set_text_color(r_temp, text);
  fmt(num, sizeof(num), m.hum, 0);
  snprintf(buf, sizeof(buf), "%s%%", num);
  set_text(r_hum, buf);
  set_text_color(r_hum, text);
  fmt(num, sizeof(num), m.co2, 0);
  snprintf(buf, sizeof(buf), "%s ppm", num);
  set_text(r_co2, buf);
  lv_color_t co2c = text;
  if (live && !isnan(m.co2)) co2c = m.co2 >= CO2_ALERT ? C_ALERT : (m.co2 >= CO2_WARN ? C_WARN : C_TEXT);
  set_text_color(r_co2, co2c);

  // light bar
  bool on = light_disp_on(m);
  int lvl = light_disp_level(m);
  set_text(l_state, !m.lb_known ? "--" : (on ? "開" : "關"));
  set_text_color(l_state, on ? C_ACCENT : C_DIM);
  snprintf(buf, sizeof(buf), "亮度 %d/15", lvl);
  set_text(l_level, buf);
  set_text_color(l_level, on ? C_TEXT : C_DIM);
  snprintf(buf, sizeof(buf), "色溫 %dK", light_disp_kelvin(m));
  set_text(l_kelvin, buf);
  set_width(l_bar_fill, 138 * lvl / 15);
  set_bg(l_bar_fill, on ? C_ACCENT : C_STALE);

  // power
  // Today's cost, not kWh: the title row cannot fit both next to "用電"
  // (agy review 2026-09-27); kWh stays on the power page.
  if (isnan(m.house_kwh)) buf[0] = '\0';
  else snprintf(buf, sizeof(buf), "€%.2f", m.house_kwh * PRICE_EUR_KWH);
  if (strcmp(lv_label_get_text(p_cost), buf) != 0) {
    set_text(p_cost, buf);
    lv_obj_update_layout(p_cost);
    lv_obj_align_to(p_today, p_cost, LV_ALIGN_OUT_LEFT_MID, -4, 0);
  }
  set_hidden(p_today, buf[0] == '\0');
  fmt(num, sizeof(num), m.house_w, 0);
  set_text(p_house, num);
  set_text_color(p_house, text);
  fmt(num, sizeof(num), m.pc_w, 0);
  set_text(p_pc, num);
  set_text_color(p_pc, isnan(m.pc_w) ? C_STALE : text);  // NaN = PC off

  // basil
  bool b_live = live && m.basil_online;
  fmt(num, sizeof(num), m.soil, 0);
  set_text(b_soil, num);
  set_text_color(b_soil, !b_live ? C_STALE : (m.need_water ? C_WARN : C_TEXT));
  fmt(num, sizeof(num), m.tank, 0);
  set_text(b_tank, num);
  set_text_color(b_tank, !b_live ? C_STALE : (m.tank_empty ? C_ALERT : (m.tank < 20 ? C_WARN : C_TEXT)));
  if (!m.basil_online) {
    set_text(b_status, "離線");
    set_text_color(b_status, C_WARN);
  } else if (m.drain_fault) {
    set_text(b_status, "排水異常");
    set_text_color(b_status, C_ALERT);
  } else {
    // The box light is the plant's only light source (enclosed), not a supplement.
    set_text(b_status, m.grow_on ? "植物燈 開" : "植物燈 關");
    set_text_color(b_status, m.grow_on ? C_ACCENT : C_DIM);
  }
}
