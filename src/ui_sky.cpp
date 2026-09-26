// Animated weather sky behind the home screen, after the projector wake page
// (/opt/wake-display/v2.html on nl-pi): same condition groups and the same
// gradient table per phase (day / dusk / night), redrawn with a handful of
// LVGL objects instead of CSS layers.
//
//   screen bg      vertical gradient (static; changes with weather / phase)
//   static layers  sun + glow, moon, stars (twinkle)
//   moving layers  clouds (slow drift), rain / snow, fog bands, lightning flash
//
// The info cards sit on top at 80 % opacity. Only the small rectangles a
// particle leaves and enters are redrawn each frame, so the card text under a
// raindrop is re-rendered for a few pixels, not the whole card. Frame rate is
// set by main.cpp from the backlight state: 20 fps in use, 4 fps in the dim
// glance mode, 0 (a still sky) at night.

#include <Arduino.h>
#include <math.h>

#include <time.h>

#include "config.h"
#include "ui_common.h"

enum Group : uint8_t { G_CLEAR, G_PARTLY, G_CLOUDY, G_RAIN, G_POUR, G_STORM, G_SNOW, G_FOG, G_COUNT };
enum Phase : uint8_t { P_DAY, P_DUSK, P_NIGHT };

struct Cond {
  const char *ha;
  const char *label;
  Group group;
};
static const Cond CONDS[] = {
    {"sunny", "晴", G_CLEAR},           {"clear-night", "晴朗", G_CLEAR},
    {"partlycloudy", "晴時多雲", G_PARTLY}, {"cloudy", "陰", G_CLOUDY},
    {"rainy", "雨", G_RAIN},            {"pouring", "大雨", G_POUR},
    {"snowy", "雪", G_SNOW},            {"snowy-rainy", "雨夾雪", G_SNOW},
    {"hail", "冰雹", G_SNOW},           {"lightning", "雷", G_STORM},
    {"lightning-rainy", "雷雨", G_STORM}, {"fog", "霧", G_FOG},
    {"windy", "強風", G_CLOUDY},        {"windy-variant", "強風多雲", G_CLOUDY},
    {"exceptional", "異常", G_STORM},
};

// [group][phase] = {top, bottom}; copied from the projector page's SKY table.
static const uint32_t SKY[G_COUNT][3][2] = {
    {{0x2f7de1, 0x8ec5ff}, {0x3b4a8a, 0xf0a36b}, {0x07122b, 0x1d3563}},  // clear
    {{0x3f7cc4, 0x9cc3e6}, {0x46507e, 0xd6977a}, {0x0d1830, 0x27395a}},  // partly
    {{0x5f7185, 0xa3b1bf}, {0x4b5470, 0xb98a74}, {0x1b2230, 0x36404f}},  // cloudy
    {{0x3a4d62, 0x6f8499}, {0x353d57, 0x7d6a66}, {0x111925, 0x2a3747}},  // rain
    {{0x2f3f52, 0x586b80}, {0x2c3349, 0x63575a}, {0x0d141e, 0x222d3b}},  // pour
    {{0x2b3442, 0x515e6e}, {0x282c3f, 0x584c52}, {0x0b0f18, 0x1f2633}},  // storm
    {{0x8ea3ba, 0xd3dde8}, {0x6d7896, 0xc9a99a}, {0x26304a, 0x4c5b72}},  // snow
    {{0x7f8a95, 0xb7bec5}, {0x6b6f80, 0xa8958c}, {0x262c33, 0x454c54}},  // fog
};

const char *sky_label(const char *ha_cond) {
  for (const Cond &c : CONDS) {
    if (!strcmp(c.ha, ha_cond)) return c.label;
  }
  return "";
}

static Group group_of(const char *ha_cond) {
  for (const Cond &c : CONDS) {
    if (!strcmp(c.ha, ha_cond)) return c.group;
  }
  return G_CLOUDY;
}

// Civil twilight band counts as dusk/dawn.
static Phase phase_of(float elev) {
  if (isnan(elev)) return P_DAY;
  return elev > 6 ? P_DAY : (elev > -6 ? P_DUSK : P_NIGHT);
}

// --- Objects ----------------------------------------------------------------------
// Sun and moon travel their real path: x from azimuth (east = left, west =
// right), y from altitude with the horizon at the bottom edge, so a rising
// body first shows as a half circle at the bottom-left and sets bottom-right.
static constexpr float AZ_LEFT = 60, AZ_RIGHT = 300;  // degrees, clamped
static constexpr float ALT_TOP = 65;                  // altitude drawn at y = TOP_Y
static constexpr int TOP_Y = 26, HORIZON_Y = 240;
// A rising disc shows as a half circle about two cards wide and one card high.
static constexpr int SUN_D = 200, GLOW_D = 250, MOON_D = 180;
static constexpr int N_DROPS = 28, N_STARS = 14, N_CLOUDS = 3, N_FOG = 3;
static lv_obj_t *s_scr, *s_sun, *s_glow, *s_moon, *s_flash;
static lv_obj_t *s_drops[N_DROPS], *s_stars[N_STARS], *s_clouds[N_CLOUDS], *s_fog[N_FOG];
static int16_t s_dx[N_DROPS], s_dy[N_DROPS], s_x[N_DROPS], s_y[N_DROPS];
static int16_t s_cloud_x[N_CLOUDS];
static int s_drop_count = 0;
static bool s_snow = false, s_storm = false;
static Group s_group = G_COUNT;
static Phase s_phase = P_DAY;
static int s_fps = 20;
static uint32_t s_frame = 0;
static unsigned long s_next_flash = 0, s_flash_off = 0;

static lv_obj_t *blob(int x, int y, int w, int h, uint32_t rgb, lv_opa_t opa) {
  lv_obj_t *o = mk_box(s_scr, x, y, w, h, lv_color_hex(rgb));
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

// --- Low-precision ephemeris (~1 degree; plenty for a 320 px sky) --------------
static constexpr float D2R = 0.01745329252f;

struct AltAz {
  float alt, az;  // degrees; az from north through east
};

static float wrap360(float x) {
  x = fmodf(x, 360.0f);
  return x < 0 ? x + 360.0f : x;
}

// Ecliptic longitude/latitude -> horizontal coordinates at days-since-J2000 d.
static AltAz ecl_to_altaz(float d, float lambda, float beta) {
  float eps = (23.439f - 0.0000004f * d) * D2R;
  float l = lambda * D2R, b = beta * D2R;
  float ra = atan2f(cosf(eps) * sinf(l) - tanf(b) * sinf(eps), cosf(l));
  float dec = asinf(sinf(b) * cosf(eps) + cosf(b) * sinf(eps) * sinf(l));
  float lst = wrap360(280.46061837f + 360.98564736629f * d + HOME_LON) * D2R;
  float ha = lst - ra;
  float lat = HOME_LAT * D2R;
  float alt = asinf(sinf(lat) * sinf(dec) + cosf(lat) * cosf(dec) * cosf(ha));
  float az = atan2f(-sinf(ha), tanf(dec) * cosf(lat) - sinf(lat) * cosf(ha));
  return {alt / D2R, wrap360(az / D2R)};
}

static float days_j2000(time_t t) { return (float)((double)(t - 946728000L) / 86400.0); }

static AltAz sun_pos(float d) {
  float g = wrap360(357.529f + 0.98560028f * d) * D2R;
  float q = wrap360(280.459f + 0.98564736f * d);
  return ecl_to_altaz(d, q + 1.915f * sinf(g) + 0.020f * sinf(2 * g), 0);
}

static AltAz moon_pos(float d) {
  float l0 = wrap360(218.316f + 13.176396f * d);
  float m = wrap360(134.963f + 13.064993f * d) * D2R;
  float f = wrap360(93.272f + 13.229350f * d) * D2R;
  return ecl_to_altaz(d, l0 + 6.289f * sinf(m), 5.128f * sinf(f));
}

// Places a disc of diameter dia for body position p; hides it once it is
// fully below the horizon line.
static bool place(lv_obj_t *o, AltAz p, int dia, bool allowed) {
  float fx = (p.az - AZ_LEFT) / (AZ_RIGHT - AZ_LEFT);
  int cx = (int)lroundf(constrain(fx, 0.0f, 1.0f) * 320);
  int cy = HORIZON_Y - (int)lroundf(p.alt / ALT_TOP * (HORIZON_Y - TOP_Y));
  bool show = allowed && cy - dia / 2 < HORIZON_Y;
  if (show) lv_obj_set_pos(o, cx - dia / 2, cy - dia / 2);
  set_hidden(o, !show);
  return show;
}

static float s_sun_alt = NAN;
static unsigned long s_next_astro = 0;

// Recomputes sun and moon positions (every 30 s; they move ~0.1 deg in that time).
static void update_bodies(bool force) {
  unsigned long now = millis();
  if (!force && (long)(now - s_next_astro) < 0) return;
  s_next_astro = now + 30000;
  time_t t = time(nullptr);
  if (t < 1700000000) return;  // SNTP not synced yet
  float d = days_j2000(t);
  AltAz sun = sun_pos(d), moon = moon_pos(d);
  s_sun_alt = sun.alt;
  // Only a clear or partly cloudy sky shows its sun and moon.
  bool clear = s_group == G_CLEAR || s_group == G_PARTLY;
  place(s_sun, sun, SUN_D, clear);
  place(s_glow, sun, GLOW_D, clear);
  place(s_moon, moon, MOON_D, clear);
}

void sky_build(lv_obj_t *scr) {
  s_scr = scr;
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  s_glow = blob(0, 0, GLOW_D, GLOW_D, 0xFFD27A, LV_OPA_10);
  s_sun = blob(0, 0, SUN_D, SUN_D, 0xFFD27A, LV_OPA_COVER);
  s_moon = blob(0, 0, MOON_D, MOON_D, 0xE8E6D9, LV_OPA_90);
  for (int i = 0; i < N_STARS; i++) {
    s_stars[i] = blob(random(0, 316), random(0, 150), 2, 2, 0xFFFFFF, LV_OPA_COVER);
  }
  for (int i = 0; i < N_CLOUDS; i++) {
    s_cloud_x[i] = i * 120 - 20;
    s_clouds[i] = blob(s_cloud_x[i], 8 + i * 58, 140, 48, 0xFFFFFF, LV_OPA_20);
  }
  for (int i = 0; i < N_FOG; i++) {
    s_fog[i] = mk_box(scr, i * 90 - 40, 56 + i * 62, 220, 18, lv_color_white());
    lv_obj_set_style_radius(s_fog[i], 9, 0);
    lv_obj_set_style_bg_opa(s_fog[i], LV_OPA_20, 0);
    lv_obj_add_flag(s_fog[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < N_DROPS; i++) {
    s_drops[i] = mk_box(scr, 0, -20, 1, 10, lv_color_hex(0xC8D6E5));
    lv_obj_set_style_bg_opa(s_drops[i], LV_OPA_70, 0);
    lv_obj_add_flag(s_drops[i], LV_OBJ_FLAG_HIDDEN);
  }
  s_flash = mk_box(scr, 0, 0, 320, 240, lv_color_hex(0xC8DCFF));
  lv_obj_set_style_bg_opa(s_flash, LV_OPA_30, 0);
  lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
}

static void respawn(int i, bool anywhere) {
  s_x[i] = random(-10, 340);
  s_y[i] = anywhere ? random(-20, 240) : random(-60, -10);
  if (s_snow) {
    s_dy[i] = random(1, 3);
    s_dx[i] = 0;
  } else {
    s_dy[i] = s_group == G_POUR ? random(13, 17) : random(9, 12);
    s_dx[i] = -2;
  }
}

static void configure(Group g, Phase ph) {
  s_group = g;
  s_phase = ph;
  const uint32_t *c = SKY[g][ph];
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(c[0]), 0);
  lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(c[1]), 0);

  bool night = ph == P_NIGHT;
  bool light_sky = g == G_CLEAR || g == G_PARTLY;
  update_bodies(true);
  for (int i = 0; i < N_STARS; i++) set_hidden(s_stars[i], !(light_sky && night));

  int clouds = g == G_PARTLY ? 2 : (g == G_CLOUDY || g == G_POUR || g == G_STORM) ? 3 : (g == G_RAIN || g == G_SNOW) ? 2 : 0;
  lv_opa_t copa = (g == G_RAIN || g == G_POUR || g == G_STORM) ? LV_OPA_10 : LV_OPA_20;
  for (int i = 0; i < N_CLOUDS; i++) {
    set_hidden(s_clouds[i], i >= clouds);
    lv_obj_set_style_bg_opa(s_clouds[i], copa, 0);
  }
  for (int i = 0; i < N_FOG; i++) set_hidden(s_fog[i], g != G_FOG);

  s_snow = g == G_SNOW;
  s_storm = g == G_STORM;
  s_drop_count = g == G_POUR ? 28 : (g == G_RAIN || g == G_STORM) ? 18 : g == G_SNOW ? 22 : 0;
  for (int i = 0; i < N_DROPS; i++) {
    bool on = i < s_drop_count;
    if (on) {
      respawn(i, true);
      lv_obj_set_size(s_drops[i], s_snow ? 3 : 1, s_snow ? 3 : 10);
      lv_obj_set_style_radius(s_drops[i], s_snow ? 2 : 0, 0);
      lv_obj_set_style_bg_color(s_drops[i], s_snow ? lv_color_white() : lv_color_hex(0xC8D6E5), 0);
      lv_obj_set_pos(s_drops[i], s_x[i], s_y[i]);
    }
    set_hidden(s_drops[i], !on);
  }
  set_hidden(s_flash, true);
  s_next_flash = millis() + random(4000, 12000);
}

void sky_update(const Model &m) {
  Group g = m.wx_cond[0] ? group_of(m.wx_cond) : G_CLOUDY;
  update_bodies(false);
  Phase ph = phase_of(isnan(s_sun_alt) ? m.sun_elev : s_sun_alt);
  if (!strcmp(m.wx_cond, "clear-night")) ph = P_NIGHT;
  if (g != s_group || ph != s_phase) configure(g, ph);
}

void sky_set_fps(int fps) { s_fps = fps; }

// Called from loop() every ~50 ms; advances one frame when due.
void sky_tick() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (s_fps <= 0 || ui_page() != Page::HOME || s_group == G_COUNT) {
    set_hidden(s_flash, true);
    return;
  }
  if (now - last < (unsigned long)(1000 / s_fps)) return;
  last = now;
  s_frame++;

  for (int i = 0; i < s_drop_count; i++) {
    s_y[i] += s_dy[i];
    s_x[i] += s_snow ? (int)lroundf(sinf((s_frame + i * 7) * 0.15f)) : s_dx[i];
    if (s_y[i] > 240 || s_x[i] < -12) respawn(i, false);
    lv_obj_set_pos(s_drops[i], s_x[i], s_y[i]);
  }

  // clouds and fog drift slowly: 1 px every 3 frames
  if (s_frame % 3 == 0) {
    for (int i = 0; i < N_CLOUDS; i++) {
      if (lv_obj_has_flag(s_clouds[i], LV_OBJ_FLAG_HIDDEN)) continue;
      if (++s_cloud_x[i] > 330) s_cloud_x[i] = -150;
      lv_obj_set_x(s_clouds[i], s_cloud_x[i]);
    }
    if (s_group == G_FOG) {
      for (int i = 0; i < N_FOG; i++) {
        int x = lv_obj_get_x(s_fog[i]) + (i % 2 ? 1 : -1);
        if (x > 320) x = -220;
        if (x < -220) x = 320;
        lv_obj_set_x(s_fog[i], x);
      }
    }
  }

  // stars: one random star changes brightness per frame
  if (!lv_obj_has_flag(s_stars[0], LV_OBJ_FLAG_HIDDEN)) {
    int i = random(0, N_STARS);
    lv_obj_set_style_bg_opa(s_stars[i], random(0, 2) ? LV_OPA_COVER : LV_OPA_40, 0);
  }

  // faint lightning: one 80 ms flash every 4-12 s
  if (s_storm) {
    if (s_flash_off && (long)(now - s_flash_off) >= 0) {
      set_hidden(s_flash, true);
      s_flash_off = 0;
    } else if (!s_flash_off && (long)(now - s_next_flash) >= 0) {
      set_hidden(s_flash, false);
      s_flash_off = now + 80;
      s_next_flash = now + random(4000, 12000);
    }
  }
}
