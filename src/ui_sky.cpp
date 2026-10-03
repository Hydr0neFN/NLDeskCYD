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
    // Header labels: 2-4 characters in CWA-style wording (agy pass 2026-09-27);
    // single characters (陰, 雨) looked bare next to 晴時多雲.
    {"sunny", "晴天", G_CLEAR},         {"clear-night", "晴夜", G_CLEAR},
    {"partlycloudy", "晴時多雲", G_PARTLY}, {"cloudy", "陰天", G_CLOUDY},
    {"rainy", "降雨", G_RAIN},          {"pouring", "大雨", G_POUR},
    {"snowy", "降雪", G_SNOW},          {"snowy-rainy", "雨夾雪", G_SNOW},
    {"hail", "冰雹", G_SNOW},           {"lightning", "雷電", G_STORM},
    {"lightning-rainy", "雷陣雨", G_STORM}, {"fog", "有霧", G_FOG},
    {"windy", "強風", G_CLOUDY},        {"windy-variant", "多雲強風", G_CLOUDY},
    {"exceptional", "劇烈天氣", G_STORM},
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

static Group group_of(const char *ha_cond);

// Two-character labels for the forecast grid, where a 49 px cell cannot fit
// "晴時多雲" (16 px bold CJK = 16 px per character).
const char *sky_short_label(const char *ha_cond) {
  static const char *const SHORT[][2] = {
      {"sunny", "晴"},         {"clear-night", "晴"},     {"partlycloudy", "多雲"},
      {"cloudy", "陰"},        {"rainy", "雨"},           {"pouring", "大雨"},
      {"snowy", "雪"},         {"snowy-rainy", "雨夾雪"}, {"hail", "冰雹"},
      {"lightning", "雷電"},   {"lightning-rainy", "雷雨"}, {"fog", "霧"},
      {"windy", "強風"},       {"windy-variant", "強風"}, {"exceptional", "劇烈"},
  };
  for (auto &s : SHORT) {
    if (!strcmp(s[0], ha_cond)) return s[1];
  }
  return "--";
}

// Chip colour per condition for the forecast grid. Not the sky gradient:
// cloudy (#5f7185) and rain (#3a4d62) skies are both grey-blue and read as
// the same colour on a 40 px chip, so each condition gets a distinct hue.
lv_color_t sky_cond_color(const char *ha_cond) {
  static const struct {
    const char *ha;
    uint32_t rgb;
  } CHIP[] = {
      {"sunny", 0xFFC940},       {"clear-night", 0xFFC940},  {"partlycloudy", 0x9CC3E6},
      {"cloudy", 0x9AA4AE},      {"fog", 0x6B7280},          {"rainy", 0x3B82F6},
      {"pouring", 0x1D4ED8},     {"lightning", 0xA855F7},    {"lightning-rainy", 0xA855F7},
      {"snowy", 0xE5F0FF},       {"snowy-rainy", 0xE5F0FF},  {"hail", 0xE5F0FF},
      {"windy", 0x5EEAD4},       {"windy-variant", 0x5EEAD4}, {"exceptional", 0xF85149},
  };
  for (auto &c : CHIP) {
    if (!strcmp(c.ha, ha_cond)) return lv_color_hex(c.rgb);
  }
  return lv_color_hex(0x8B949E);
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
static lv_timer_t *s_timer = nullptr;
static void sky_frame(lv_timer_t *);
static constexpr int N_DROPS = 28, N_STARS = 14, N_CLOUDS = 3, N_FOG = 3;
static lv_obj_t *s_scr, *s_sun, *s_glow, *s_moon, *s_moon_shadow, *s_flash;
static lv_obj_t *s_drops[N_DROPS], *s_stars[N_STARS], *s_clouds[N_CLOUDS], *s_fog[N_FOG];
static int16_t s_dx[N_DROPS], s_dy[N_DROPS];  // px per 50 ms (tuned at 20 fps)
static float s_fx[N_DROPS], s_fy[N_DROPS];
static float s_cloud_x[N_CLOUDS];
static float s_cloud_speed = 1.0f;

// Parallax: far clouds are small, faint, high and slow; near ones big,
// brighter, lower and faster. ms = time per 1 px of drift at speed 1.
struct CloudLayer {
  int16_t y, w, h;
  lv_opa_t opa;
  uint16_t ms;
};
static const CloudLayer CLOUD_LAYERS[N_CLOUDS] = {
    {14, 96, 30, LV_OPA_10, 320},   // far
    {62, 140, 44, LV_OPA_20, 180},  // middle
    {140, 196, 62, 64, 100},        // near (25 %)
};
static int s_drop_count = 0;
static bool s_snow = false, s_storm = false;
static Group s_group = G_COUNT;
static Phase s_phase = P_DAY;
static int s_fps = 0;  // timer starts paused; main.cpp sets the rate
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

// Ecliptic longitudes, for the moon's phase (elongation = moon - sun).
static float sun_lambda(float d) {
  float g = wrap360(357.529f + 0.98560028f * d) * D2R;
  return wrap360(280.459f + 0.98564736f * d + 1.915f * sinf(g) + 0.020f * sinf(2 * g));
}
static float moon_lambda(float d) {
  float m = wrap360(134.963f + 13.064993f * d) * D2R;
  return wrap360(218.316f + 13.176396f * d + 6.289f * sinf(m));
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

// Half-visible body: every disc's own bg_opa is halved (the base opacity is
// kept in user_data at build time). Never the object-level `opa` style: on a
// parent with children LVGL renders it through a temporary layer, the same
// heap trap as clip_corner (see the moon in sky_build). The phase shadow
// stays opaque so the dark limb keeps hiding the disc.
static void body_fade(lv_obj_t *body, bool half) {
  auto apply = [half](lv_obj_t *o) {
    lv_opa_t base = (lv_opa_t)(uintptr_t)lv_obj_get_user_data(o);
    lv_opa_t want = half ? base / 2 : base;
    if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != want) lv_obj_set_style_bg_opa(o, want, 0);
  };
  apply(body);
  for (uint32_t i = 0; i < lv_obj_get_child_count(body); i++) {
    lv_obj_t *c = lv_obj_get_child(body, i);
    if (c != s_moon_shadow) apply(c);
  }
}

static void body_remember_opa(lv_obj_t *body) {
  lv_obj_set_user_data(body, (void *)(uintptr_t)lv_obj_get_style_bg_opa(body, LV_PART_MAIN));
  for (uint32_t i = 0; i < lv_obj_get_child_count(body); i++) {
    lv_obj_t *c = lv_obj_get_child(body, i);
    lv_obj_set_user_data(c, (void *)(uintptr_t)lv_obj_get_style_bg_opa(c, LV_PART_MAIN));
  }
}

// Sky colour at screen row y (the screen's vertical gradient, top -> bottom).
static lv_color_t sky_at(int y) {
  const uint32_t *c = SKY[s_group][s_phase];
  int mix = constrain(y, 0, 240) * 255 / 240;
  return lv_color_mix(lv_color_hex(c[1]), lv_color_hex(c[0]), mix);
}

// The phase shadow must be invisible against the sky, both where it covers the
// moon's dark limb and where it spills past the moon's circle into the corners
// of its bounding box. A flat colour showed as a ghost disc against the
// gradient and its 90 % opacity let the craters through (user photo
// 2026-10-03), so it is opaque and carries the same gradient over its rows.
static void shadow_match_sky() {
  if (s_group == G_COUNT) return;
  int y = lv_obj_get_y(s_moon);
  lv_color_t top = sky_at(y), bot = sky_at(y + MOON_D);
  if (!lv_color_eq(lv_obj_get_style_bg_color(s_moon_shadow, LV_PART_MAIN), top) ||
      !lv_color_eq(lv_obj_get_style_bg_grad_color(s_moon_shadow, LV_PART_MAIN), bot)) {
    lv_obj_set_style_bg_color(s_moon_shadow, top, 0);
    lv_obj_set_style_bg_grad_color(s_moon_shadow, bot, 0);
  }
}
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
  shadow_match_sky();
  // Whichever body owns the time of day is drawn in front where they overlap:
  // the sun while it is up, the moon after sunset (user, 2026-10-03).
  // Order is glow, sun, moon by night and moon, glow, sun by day; only
  // reordered on a change, as a move repaints both discs.
  bool moon_front = lv_obj_get_index(s_moon) > lv_obj_get_index(s_sun);
  bool sun_up = sun.alt > 0;
  if (sun_up && moon_front) lv_obj_move_to_index(s_moon, lv_obj_get_index(s_glow));
  if (!sun_up && !moon_front) lv_obj_move_to_index(s_moon, lv_obj_get_index(s_sun));
  // ...and the other one is only half visible: a pale day moon, a faded sun
  // below the horizon at dusk.
  body_fade(s_moon, sun_up);
  body_fade(s_sun, !sun_up);
  body_fade(s_glow, !sun_up);

  // Phase: a sky-coloured disc slides across the moon. Illuminated fraction
  // f = (1 - cos elongation) / 2; waxing (elongation < 180) is lit on the
  // right as seen from the northern hemisphere, so the shadow sits left.
  // Offsetting a same-size disc by f * D leaves ~f of the area lit (within a
  // few percent), close enough for a 180 px moon.
  float elong = wrap360(moon_lambda(d) - sun_lambda(d));
  float f = (1 - cosf(elong * D2R)) / 2;
  int dx = (int)lroundf(f * MOON_D);
  lv_obj_set_x(s_moon_shadow, elong < 180 ? -dx : dx);
  set_hidden(s_moon_shadow, f > 0.97f);
}

// Filled circle centred at (cx, cy) inside `parent`.
static void disc(lv_obj_t *parent, int cx, int cy, int d, uint32_t rgb, lv_opa_t opa) {
  lv_obj_t *o = mk_box(parent, cx - d / 2, cy - d / 2, d, d, lv_color_hex(rgb));
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
}

void sky_build(lv_obj_t *scr) {
  s_scr = scr;
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  s_glow = blob(0, 0, GLOW_D, GLOW_D, 0xFFD27A, LV_OPA_10);
  // Sun: warm orange rim around a pale hot core (concentric discs, children
  // move with the parent).
  s_sun = blob(0, 0, SUN_D, SUN_D, 0xFFB347, LV_OPA_COVER);
  disc(s_sun, SUN_D / 2, SUN_D / 2, SUN_D * 86 / 100, 0xFFCB66, LV_OPA_COVER);
  disc(s_sun, SUN_D / 2, SUN_D / 2, SUN_D * 64 / 100, 0xFFE59A, LV_OPA_COVER);
  // Moon: pale disc with darker maria and a few craters, laid out roughly
  // like the near side (centre x, centre y, diameter as % of the disc).
  s_moon = blob(0, 0, MOON_D, MOON_D, 0xE8E6D9, LV_OPA_COVER);
  static const uint8_t MARIA[][3] = {
      {34, 30, 26}, {56, 24, 18}, {30, 56, 22}, {62, 48, 24}, {48, 70, 14},
  };
  static const uint8_t CRATERS[][3] = {
      {72, 74, 8}, {20, 40, 6}, {44, 88, 7}, {80, 34, 5}, {60, 86, 5}, {84, 58, 6},
  };
  for (auto &m : MARIA) disc(s_moon, MOON_D * m[0] / 100, MOON_D * m[1] / 100, MOON_D * m[2] / 100, 0xB9B6A9, LV_OPA_70);
  for (auto &c : CRATERS) disc(s_moon, MOON_D * c[0] / 100, MOON_D * c[1] / 100, MOON_D * c[2] / 100, 0xA19E92, LV_OPA_80);
  // Phase shadow, last child so it covers the maria. Opaque, coloured with
  // the sky gradient behind it (shadow_match_sky), so the part that spills
  // past the moon's edge (children are clipped to the moon's bounding box,
  // not its circle) blends into the sky. Do NOT use clip_corner on the moon: it renders the 180 px
  // disc through a temporary layer (64-130 KB) that does not fit in the free
  // heap, and the failed allocation trips LV_ASSERT_MALLOC -- an endless
  // loop that froze the UI on 2026-09-27.
  s_moon_shadow = mk_box(s_moon, 0, 0, MOON_D, MOON_D, lv_color_hex(0x0B1530));
  lv_obj_set_style_radius(s_moon_shadow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_grad_dir(s_moon_shadow, LV_GRAD_DIR_VER, 0);
  body_remember_opa(s_glow);
  body_remember_opa(s_sun);
  body_remember_opa(s_moon);
  for (int i = 0; i < N_STARS; i++) {
    s_stars[i] = blob(random(0, 316), random(0, 150), 2, 2, 0xFFFFFF, LV_OPA_COVER);
  }
  for (int i = 0; i < N_CLOUDS; i++) {
    const CloudLayer &L = CLOUD_LAYERS[i];
    s_cloud_x[i] = i * 110 - 30;
    s_clouds[i] = blob((int)s_cloud_x[i], L.y, L.w, L.h, 0xFFFFFF, L.opa);
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

  s_timer = lv_timer_create(sky_frame, 50, nullptr);
  lv_timer_pause(s_timer);
}

static void respawn(int i, bool anywhere) {
  s_fx[i] = random(-10, 340);
  s_fy[i] = anywhere ? random(-20, 240) : random(-60, -10);
  // Depth: a faster particle reads as nearer, so it is drawn bigger and
  // brighter. Rain streaks are as long as the distance they fall in ~60 ms,
  // which is what the eye sees as motion blur.
  if (s_snow) {
    s_dy[i] = random(1, 3);
    s_dx[i] = 0;
    int d = s_dy[i] + 1;  // 2-3 px
    lv_obj_set_size(s_drops[i], d, d);
    lv_obj_set_style_bg_opa(s_drops[i], s_dy[i] > 1 ? LV_OPA_90 : LV_OPA_60, 0);
  } else {
    s_dy[i] = s_group == G_POUR ? random(12, 18) : random(8, 13);
    s_dx[i] = -2;
    lv_obj_set_size(s_drops[i], 1, s_dy[i] + 3);
    lv_obj_set_style_bg_opa(s_drops[i], s_dy[i] >= (s_group == G_POUR ? 15 : 11) ? LV_OPA_80 : LV_OPA_40, 0);
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
  // Rain skies are already grey: halve the cloud opacity so they stay subtle.
  bool wet = g == G_RAIN || g == G_POUR || g == G_STORM;
  for (int i = 0; i < N_CLOUDS; i++) {
    // Show the nearest layers first: with two clouds, the far one is dropped.
    int layer = N_CLOUDS - 1 - i;
    set_hidden(s_clouds[layer], i >= clouds);
    lv_obj_set_style_bg_opa(s_clouds[layer], wet ? CLOUD_LAYERS[layer].opa / 2 : CLOUD_LAYERS[layer].opa, 0);
  }
  for (int i = 0; i < N_FOG; i++) set_hidden(s_fog[i], g != G_FOG);

  s_snow = g == G_SNOW;
  s_storm = g == G_STORM;
  s_drop_count = g == G_POUR ? 28 : (g == G_RAIN || g == G_STORM) ? 18 : g == G_SNOW ? 22 : 0;
  for (int i = 0; i < N_DROPS; i++) {
    bool on = i < s_drop_count;
    if (on) {
      lv_obj_set_style_radius(s_drops[i], s_snow ? 2 : 0, 0);
      lv_obj_set_style_bg_color(s_drops[i], s_snow ? lv_color_white() : lv_color_hex(0xC8D6E5), 0);
      respawn(i, true);  // also sets size and opacity by depth
      lv_obj_set_pos(s_drops[i], (int)s_fx[i], (int)s_fy[i]);
    }
    set_hidden(s_drops[i], !on);
  }
  set_hidden(s_flash, true);
  s_next_flash = millis() + random(4000, 12000);
}

static bool s_full_redraw = false;
void sky_set_full_redraw(bool on) { s_full_redraw = on; }

void sky_set_cloud_speed(float k) { s_cloud_speed = k; }

void sky_set_fps(int fps) {
  if (fps == s_fps || !s_timer) return;
  s_fps = fps;
  if (fps <= 0) {
    lv_timer_pause(s_timer);
    set_hidden(s_flash, true);
  } else {
    lv_timer_set_period(s_timer, 1000 / fps);
    lv_timer_resume(s_timer);
  }
}

void sky_update(const Model &m) {
  Group g = m.wx_cond[0] ? group_of(m.wx_cond) : G_CLOUDY;
  update_bodies(false);
  Phase ph = phase_of(isnan(s_sun_alt) ? m.sun_elev : s_sun_alt);
  if (!strcmp(m.wx_cond, "clear-night")) ph = P_NIGHT;
  if (g != s_group || ph != s_phase) configure(g, ph);
}

// One animation frame. Driven by an lv_timer, so it runs inside
// lv_timer_handler() just before LVGL's refresh and frames stay evenly paced.
static void sky_frame(lv_timer_t *) {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (ui_page() != Page::HOME || s_group == G_COUNT) {
    set_hidden(s_flash, true);
    last = now;
    return;
  }
  // Motion is per second, not per frame: speeds were tuned at 20 fps, so scale
  // by the real elapsed time and the frame rate can change without the rain
  // speeding up. Capped so a long stall does not teleport everything.
  float k = min(now - last, 200UL) / 50.0f;
  last = now;
  s_frame++;
  if (s_full_redraw) lv_obj_invalidate(s_scr);

  for (int i = 0; i < s_drop_count; i++) {
    s_fy[i] += s_dy[i] * k;
    s_fx[i] += s_snow ? sinf((now / 50.0f + i * 7) * 0.15f) * k : s_dx[i] * k;
    if (s_fy[i] > 240 || s_fx[i] < -12) respawn(i, false);
    lv_obj_set_pos(s_drops[i], (int)s_fx[i], (int)s_fy[i]);
  }

  // clouds drift at 1 px per layer interval x s_cloud_speed (parallax), in
  // real time; set_x only when the whole-pixel position changes
  for (int i = 0; i < N_CLOUDS; i++) {
    if (lv_obj_has_flag(s_clouds[i], LV_OBJ_FLAG_HIDDEN)) continue;
    int old_px = (int)s_cloud_x[i];
    s_cloud_x[i] += k * 50.0f / CLOUD_LAYERS[i].ms * s_cloud_speed;
    if (s_cloud_x[i] > 330) s_cloud_x[i] = -CLOUD_LAYERS[i].w - 10;
    if ((int)s_cloud_x[i] != old_px) lv_obj_set_x(s_clouds[i], (int)s_cloud_x[i]);
  }

  // fog bands drift 1 px every 150 ms
  static unsigned long last_drift = 0;
  if (now - last_drift >= 150) {
    last_drift = now;
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
