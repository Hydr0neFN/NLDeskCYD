#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <Preferences.h>

#include "config.h"
#include "net.h"
#include "ui.h"
#include "ui_common.h"

// CYD touch uses non-default SPI pins, on a separate bus from the display.
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

static TFT_eSPI tft = TFT_eSPI();
static SPIClass touchscreenSpi = SPIClass(VSPI);
static XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

static constexpr int DRAW_BUF_LINES = 64;
// LVGL 9 asserts on an unaligned draw buffer and halts in setup().
static uint16_t draw_buf[320 * DRAW_BUF_LINES] __attribute__((aligned(4)));

static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors(reinterpret_cast<uint16_t *>(px_map), w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

// --- Backlight --------------------------------------------------------------
// LEDC PWM with a soft ramp: a raw step flickers visibly on this board.
// Arduino-ESP32 core 2.x LEDC API (channel based).
static constexpr uint8_t BACKLIGHT_PWM_CH = 0;
static constexpr uint32_t BACKLIGHT_PWM_FREQ = 5000;
static constexpr uint8_t BACKLIGHT_PWM_RES = 8;
static constexpr unsigned long RAMP_STEP_MS = 8;
static constexpr int RAMP_STEP = 6;

static unsigned long s_last_touch_ms = 0;
static unsigned long s_last_ramp_ms = 0;
static int s_bl_duty = 0;
static int s_bl_target = BL_DAY_ACTIVE;

// A touch that lands on a dark or dimmed screen only wakes it: the press is
// swallowed until the finger has been lifted for RELEASE_DEBOUNCE_MS, so
// waking never toggles a control the user could not see. The debounce matters
// on resistive touch: contact chatter reads as a one-sample release, which
// would otherwise end the swallow while the finger is still down.
static constexpr unsigned long RELEASE_DEBOUNCE_MS = 50;
static bool s_swallow = false;
static unsigned long s_release_start = 0;

static bool is_active() { return millis() - s_last_touch_ms < IDLE_TIMEOUT_MS; }

static void my_touch_read_cb(lv_indev_t *, lv_indev_data_t *data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    s_release_start = 0;
    if (!is_active()) s_swallow = true;
    s_last_touch_ms = millis();
    if (s_swallow) {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    TS_Point p = touchscreen.getPoint();
    // Raw ADC calibration range for XPT2046 on this CYD.
    data->point.x = map(p.x, 200, 3700, 1, 320);
    data->point.y = map(p.y, 240, 3800, 1, 240);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    if (!s_release_start) s_release_start = millis();
    if (millis() - s_release_start >= RELEASE_DEBOUNCE_MS) s_swallow = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// --- Display inversion, persisted -------------------------------------------
// The build flag TFT_INVERSION_ON is the proven default for this board; the
// colour-check page can flip it at runtime and the choice survives reboots.
static Preferences s_prefs;
static bool s_inverted = true;

void display_set_inverted(bool inverted) {
  s_inverted = inverted;
  tft.invertDisplay(inverted);
  s_prefs.putBool("invert", inverted);
}

bool display_is_inverted() { return s_inverted; }

static int backlight_target(const Model &m) {
  bool night = ui_is_night();
  if (m.ota_active) return night ? BL_NIGHT_ACTIVE : BL_DAY_ACTIVE;
  if (is_active()) return night ? BL_NIGHT_ACTIVE : BL_DAY_ACTIVE;
  // Sleep beats the plant: alerts never light the screen at night.
  if (night) return BL_NIGHT_IDLE;
  return ui_has_alert(m) ? BL_DAY_ALERT : BL_DAY_GLANCE;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("NLDeskCYD boot");

  net_start();  // own task, pinned to core 0

  touchscreenSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSpi);
  touchscreen.setRotation(1);

  tft.init();
  tft.setRotation(1);  // proven unmirrored orientation for this board, matches touch
  tft.fillScreen(TFT_BLACK);
  s_prefs.begin("nldesk", false);
  s_inverted = s_prefs.getBool("invert", true);
  tft.invertDisplay(s_inverted);

  ledcSetup(BACKLIGHT_PWM_CH, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RES);
  ledcAttachPin(TFT_BL, BACKLIGHT_PWM_CH);
  ledcWrite(BACKLIGHT_PWM_CH, 0);
  s_last_touch_ms = millis();

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  lv_display_t *disp = lv_display_create(320, 240);
  lv_display_set_flush_cb(disp, my_flush_cb);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, my_touch_read_cb);

  ui_init();
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
}

void loop() {
  static unsigned long last_refresh = 0;
  static bool was_active = true;
  static Model m;
  unsigned long now = millis();

  if (now - last_refresh >= 200) {
    last_refresh = now;
    net_snapshot(m);
    ui_refresh(m);
    s_bl_target = backlight_target(m);
    // Sky animation follows the backlight: full rate in use, a slow drift in
    // the dim glance mode, a still sky at night (moving pixels in a dark
    // bedroom catch the eye).
    sky_set_fps(ui_is_night() ? 0 : (is_active() ? 20 : 4));

    bool active = is_active();
    if (was_active && !active) ui_go_home();  // idle always returns to the overview
    was_active = active;
  }

  if (s_bl_duty != s_bl_target && now - s_last_ramp_ms >= RAMP_STEP_MS) {
    s_last_ramp_ms = now;
    s_bl_duty = s_bl_duty < s_bl_target ? min(s_bl_duty + RAMP_STEP, s_bl_target)
                                        : max(s_bl_duty - RAMP_STEP, s_bl_target);
    ledcWrite(BACKLIGHT_PWM_CH, s_bl_duty);
  }

  sky_tick();
  lv_timer_handler();
  delay(5);
}
