#pragma once

#include <math.h>
#include <stdint.h>

// Everything the panel shows live. Written by the network task (core 0), read
// by the UI (core 1) only through net_snapshot(). NAN = unknown/unavailable.
struct Model {
  // Monitor light bar. HA's MQTT light is optimistic and reports brightness /
  // colour temp only while on, so the last seen values are kept for display.
  bool lb_known = false;  // at least one state received and not unavailable
  bool lb_on = false;
  int lb_level = 8;       // 1..15 hardware levels (HA brightness / 17)
  int lb_kelvin = 4000;

  float temp = NAN, hum = NAN, co2 = NAN, pressure = NAN;
  float house_w = NAN, pc_w = NAN;
  float house_kwh = NAN, pc_kwh = NAN;

  float soil = NAN, tank = NAN, box_temp = NAN, box_hum = NAN;
  float water_below = NAN, stop_at = NAN;
  bool basil_online = false;
  bool need_water = false, tank_empty = false, drain_fault = false, grow_on = false;

  // Outdoor weather (cyd/nl/weather, automation cyd_nl_weather_publish).
  char wx_cond[20] = "";  // HA condition, e.g. "partlycloudy"; "" = unknown
  float out_temp = NAN, out_hum = NAN, sun_elev = NAN;

  bool mqtt_connected = false;
  uint32_t last_msg_ms = 0;  // millis() of the last message on any topic
  bool ota_active = false;
  // Benchmark window (cyd/nl/panel/bench, payload = seconds): the sky runs at
  // full rate even at night, backlight untouched. For remote A/B tests.
  uint32_t bench_until_ms = 0;
  int bench_fps = 20;
  bool bench_full = false;
  int ota_percent = 0;
};

// Chart history published by cyd-hist on nl-pi (cyd/nl/hist/<key>).
static constexpr int HIST_MAX = 96;
static constexpr int16_t HIST_NONE = INT16_MIN;

enum HistKey : uint8_t { H_TEMP, H_HUM, H_CO2, H_HOUSE, H_PC, H_SOIL, H_TANK, H_DAILY, H_COUNT };

struct Series {
  uint32_t end = 0;   // unix time of the end of the last bucket
  uint32_t step = 0;  // seconds per bucket
  uint8_t n = 0;
  int16_t v[HIST_MAX];  // HIST_NONE = no data; temperature is x10, daily kWh x100
  uint8_t dow[7];       // H_DAILY only: weekday of each bar, 0 = Sunday
};

// Starts WiFi, MQTT, ArduinoOTA and SNTP on a task pinned to core 0.
void net_start();

// Copies the current model. Safe from core 1.
void net_snapshot(Model &out);

// Copies series `k` into `out` if it changed since `seen` (then updates `seen`).
// Returns true when a copy was made. Safe from core 1.
bool net_hist(HistKey k, Series &out, uint32_t &seen);

// Hourly forecast (cyd/nl/forecast, automation cyd_nl_weather_publish):
// the next 12 hours from weather.get_forecasts.
static constexpr int FC_MAX = 12;
struct Forecast {
  uint8_t n = 0;
  uint8_t hour[FC_MAX];      // local hour 0-23
  char cond[FC_MAX][16];     // HA condition, e.g. "partlycloudy"
  int8_t temp[FC_MAX];       // °C
  float rain[FC_MAX];        // mm in that hour
};

// Copies the forecast into `out` if it changed since `seen`. Safe from core 1.
bool net_forecast(Forecast &out, uint32_t &seen);

enum class CmdTarget : uint8_t { LIGHTBAR, BASIL };

// Queues a command (JSON object text) for HA; see automations cyd_nl_command
// and cyd_nl_basil_command. Safe from core 1. Dropped while MQTT is down, so a
// reconnect never replays stale taps.
void net_send(CmdTarget t, const char *json);

// Display performance, published to cyd/nl/panel/perf every 10 s for A/B
// testing display changes. Safe from core 1.
void net_set_perf(float fps, float frame_ms, float flush_ms, int sky_fps);

// Front LDR (GPIO34) raw ADC average and the backlight duty at that moment,
// published to cyd/nl/panel/ldr every 2 s while evaluating the sensor.
void net_set_ldr(int raw, int backlight);
