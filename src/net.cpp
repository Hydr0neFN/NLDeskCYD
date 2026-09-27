#include "net.h"

#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "secrets.h"

static WiFiClient s_wifi;
static PubSubClient s_mqtt(s_wifi);

static Model s_model;
static Series s_hist[H_COUNT];
static uint32_t s_hist_ver[H_COUNT];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_cmd_queue = nullptr;

struct Cmd {
  CmdTarget target;
  char json[80];
};

void net_snapshot(Model &out) {
  portENTER_CRITICAL(&s_mux);
  out = s_model;
  portEXIT_CRITICAL(&s_mux);
}

bool net_hist(HistKey k, Series &out, uint32_t &seen) {
  bool changed = false;
  portENTER_CRITICAL(&s_mux);
  if (s_hist_ver[k] != seen) {
    out = s_hist[k];
    seen = s_hist_ver[k];
    changed = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return changed;
}

void net_send(CmdTarget t, const char *json) {
  if (!s_cmd_queue) return;
  Cmd c;
  c.target = t;
  strlcpy(c.json, json, sizeof(c.json));
  xQueueSend(s_cmd_queue, &c, 0);
}

// --- Incoming statestream topics ----------------------------------------
// Payloads are HA states as plain text ("59.85", "off", "unavailable") or, for
// attributes, JSON-encoded values ("51", "null").

static float parse_num(const char *p) {
  if (!*p || !strcmp(p, "unavailable") || !strcmp(p, "unknown") || !strcmp(p, "null") ||
      !strcmp(p, "None")) {
    return NAN;
  }
  char *end = nullptr;
  float v = strtof(p, &end);
  return end == p ? NAN : v;
}

static bool is_on(const char *p) { return !strcmp(p, "on"); }

enum class Field {
  LB_STATE, LB_BRIGHTNESS, LB_KELVIN,
  TEMP, HUM, CO2, PRESSURE,
  HOUSE_W, PC_W, HOUSE_KWH, PC_KWH,
  SOIL, TANK, BOX_TEMP, BOX_HUM, WATER_BELOW, STOP_AT,
  BASIL_UPTIME, NEED_WATER, TANK_EMPTY, DRAIN_FAULT, GROW_LIGHT,
};

struct Sub {
  const char *suffix;  // after TOPIC_BASE
  Field field;
};

static const Sub SUBS[] = {
    {"light/monitor_light_bar_light_bar/state", Field::LB_STATE},
    {"light/monitor_light_bar_light_bar/brightness", Field::LB_BRIGHTNESS},
    {"light/monitor_light_bar_light_bar/color_temp_kelvin", Field::LB_KELVIN},
    {"sensor/room_environment_temperature/state", Field::TEMP},
    {"sensor/room_environment_humidity/state", Field::HUM},
    {"sensor/room_environment_co2/state", Field::CO2},
    {"sensor/room_environment_pressure/state", Field::PRESSURE},
    {"sensor/power_monitor_power/state", Field::HOUSE_W},
    {"sensor/gaming_pc_power/state", Field::PC_W},
    {"sensor/house_energy_today/state", Field::HOUSE_KWH},
    {"sensor/gaming_pc_energy_today/state", Field::PC_KWH},
    {"sensor/basil_soil_moisture/state", Field::SOIL},
    {"sensor/basil_tank/state", Field::TANK},
    {"sensor/basil_box_temperature/state", Field::BOX_TEMP},
    {"sensor/basil_box_humidity/state", Field::BOX_HUM},
    {"number/basil_water_below/state", Field::WATER_BELOW},
    {"number/basil_stop_at/state", Field::STOP_AT},
    {"sensor/basil_uptime/state", Field::BASIL_UPTIME},
    {"binary_sensor/basil_needs_water/state", Field::NEED_WATER},
    {"binary_sensor/basil_tank_empty/state", Field::TANK_EMPTY},
    {"binary_sensor/basil_drain_fault/state", Field::DRAIN_FAULT},
    {"switch/basil_grow_light/state", Field::GROW_LIGHT},
};

static const char *const HIST_KEYS[H_COUNT] = {"temp", "hum", "co2", "house", "pc", "soil", "tank", "daily"};

static void apply(Field f, const char *p) {
  float v = parse_num(p);
  portENTER_CRITICAL(&s_mux);
  Model &m = s_model;
  switch (f) {
    case Field::LB_STATE:
      m.lb_known = strcmp(p, "unavailable") != 0 && strcmp(p, "unknown") != 0;
      m.lb_on = is_on(p);
      break;
    case Field::LB_BRIGHTNESS:
      // HA scales the bar's 15 hardware levels to 0-255 (brightness_scale: 15),
      // so one level is exactly 17. null while off -> keep the last level.
      if (!isnan(v)) m.lb_level = constrain((int)lroundf(v / 17.0f), 1, 15);
      break;
    case Field::LB_KELVIN:
      if (!isnan(v)) m.lb_kelvin = (int)lroundf(v);
      break;
    case Field::TEMP: m.temp = v; break;
    case Field::HUM: m.hum = v; break;
    case Field::CO2: m.co2 = v; break;
    case Field::PRESSURE: m.pressure = v; break;
    case Field::HOUSE_W: m.house_w = v; break;
    case Field::PC_W: m.pc_w = v; break;
    case Field::HOUSE_KWH: m.house_kwh = v; break;
    case Field::PC_KWH: m.pc_kwh = v; break;
    case Field::SOIL: m.soil = v; break;
    case Field::TANK: m.tank = v; break;
    case Field::BOX_TEMP: m.box_temp = v; break;
    case Field::BOX_HUM: m.box_hum = v; break;
    case Field::WATER_BELOW: m.water_below = v; break;
    case Field::STOP_AT: m.stop_at = v; break;
    // Uptime is always present while the device is up, so it is the cleanest
    // online proxy (same choice as automation basil_device_offline).
    case Field::BASIL_UPTIME: m.basil_online = !isnan(v); break;
    case Field::NEED_WATER: m.need_water = is_on(p); break;
    case Field::TANK_EMPTY: m.tank_empty = is_on(p); break;
    case Field::DRAIN_FAULT: m.drain_fault = is_on(p); break;
    case Field::GROW_LIGHT: m.grow_on = is_on(p); break;
  }
  m.last_msg_ms = millis();
  portEXIT_CRITICAL(&s_mux);
}

// Reads a JSON int array starting at '[' ("null" -> HIST_NONE). Returns count.
static int parse_int_array(const char *s, int16_t *out, int max) {
  if (!s || *s != '[') return 0;
  s++;
  int n = 0;
  while (*s && *s != ']' && n < max) {
    while (*s == ' ' || *s == ',') s++;
    if (*s == 'n') {
      out[n++] = HIST_NONE;
      s += 4;
    } else {
      char *end;
      long v = strtol(s, &end, 10);
      if (end == s) break;
      out[n++] = (int16_t)constrain(v, -32767L, 32767L);
      s = end;
    }
  }
  return n;
}

static const char *after_key(const char *s, const char *key) {
  const char *p = strstr(s, key);
  return p ? p + strlen(key) : nullptr;
}

// {"end":1790457300,"step":900,"v":[281,282,null,...]} (+ "dow":[..] for daily)
static void apply_hist(HistKey k, const char *json) {
  static Series tmp;  // net task only
  const char *p;
  tmp.end = (p = after_key(json, "\"end\":")) ? strtoul(p, nullptr, 10) : 0;
  tmp.step = (p = after_key(json, "\"step\":")) ? strtoul(p, nullptr, 10) : 0;
  tmp.n = parse_int_array(after_key(json, "\"v\":"), tmp.v, HIST_MAX);
  if (k == H_DAILY) {
    int16_t d[7] = {0};
    parse_int_array(after_key(json, "\"dow\":"), d, 7);
    for (int i = 0; i < 7; i++) tmp.dow[i] = (uint8_t)d[i];
  }
  if (!tmp.n) return;
  portENTER_CRITICAL(&s_mux);
  s_hist[k] = tmp;
  s_hist_ver[k]++;
  s_model.last_msg_ms = millis();
  portEXIT_CRITICAL(&s_mux);
}

static float json_num(const char *json, const char *key) {
  const char *p = after_key(json, key);
  if (!p || !strncmp(p, "null", 4)) return NAN;
  char *end;
  float v = strtof(p, &end);
  return end == p ? NAN : v;
}

// {"cond":"partlycloudy","t":12.7,"h":82,"wind":8.3,"elev":-33.79,"rising":false}
static void apply_weather(const char *json) {
  char cond[20] = "";
  const char *p = after_key(json, "\"cond\":\"");
  if (p) {
    size_t i = 0;
    while (p[i] && p[i] != '"' && i < sizeof(cond) - 1) {
      cond[i] = p[i];
      i++;
    }
    cond[i] = '\0';
  }
  float t = json_num(json, "\"t\":"), h = json_num(json, "\"h\":"), e = json_num(json, "\"elev\":");
  portENTER_CRITICAL(&s_mux);
  strlcpy(s_model.wx_cond, strcmp(cond, "unavailable") ? cond : "", sizeof(s_model.wx_cond));
  s_model.out_temp = t;
  s_model.out_hum = h;
  s_model.sun_elev = e;
  s_model.last_msg_ms = millis();
  portEXIT_CRITICAL(&s_mux);
}

static Forecast s_forecast;
static uint32_t s_forecast_ver = 0;

bool net_forecast(Forecast &out, uint32_t &seen) {
  bool changed = false;
  portENTER_CRITICAL(&s_mux);
  if (s_forecast_ver != seen) {
    out = s_forecast;
    seen = s_forecast_ver;
    changed = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return changed;
}

// {"f":[[11,"partlycloudy",15,0.0],[12,"rainy",14,0.4],...]}
static void apply_forecast(const char *json) {
  static Forecast tmp;  // net task only
  tmp.n = 0;
  const char *p = strstr(json, "[[");
  if (!p) return;
  p++;
  while (*p == '[' && tmp.n < FC_MAX) {
    int i = tmp.n;
    char *e;
    tmp.hour[i] = (uint8_t)strtol(p + 1, &e, 10);
    p = strchr(e, '"');
    if (!p) break;
    p++;
    size_t k = 0;
    while (*p && *p != '"' && k < sizeof(tmp.cond[i]) - 1) tmp.cond[i][k++] = *p++;
    tmp.cond[i][k] = 0;
    p = strchr(p, ',');
    if (!p) break;
    tmp.temp[i] = (int8_t)strtol(p + 1, &e, 10);
    p = strchr(e, ',');
    if (!p) break;
    tmp.rain[i] = strtof(p + 1, &e);
    p = strchr(e, ']');
    if (!p) break;
    tmp.n++;
    p++;
    while (*p == ',' || *p == ' ') p++;
  }
  if (!tmp.n) return;
  portENTER_CRITICAL(&s_mux);
  s_forecast = tmp;
  s_forecast_ver++;
  portEXIT_CRITICAL(&s_mux);
}

static void on_message(char *topic, uint8_t *payload, unsigned int len) {
  static constexpr size_t BASE_LEN = sizeof(TOPIC_BASE) - 1;
  static constexpr char HIST[] = "hist/";
  if (strncmp(topic, TOPIC_BASE, BASE_LEN) != 0) return;
  const char *suffix = topic + BASE_LEN;

  if (strcmp(suffix, "panel/bench") == 0) {
    char b[12];
    size_t n = len < sizeof(b) - 1 ? len : sizeof(b) - 1;
    memcpy(b, payload, n);
    b[n] = '\0';
    // "secs" or "secs:fps"
    long secs = constrain(atol(b), 0L, 600L);
    const char *colon = strchr(b, ':');
    int fps = colon ? constrain(atoi(colon + 1), 1, 60) : 20;
    // "secs:fps:full" also redraws the whole screen every frame (SPI A/B test)
    bool full = colon && strchr(colon + 1, ':') && strstr(colon, "full");
    s_model.bench_full = full;
    portENTER_CRITICAL(&s_mux);
    s_model.bench_until_ms = secs ? millis() + secs * 1000 : 0;
    s_model.bench_fps = fps;
    portEXIT_CRITICAL(&s_mux);
    return;
  }

  if (strcmp(suffix, "forecast") == 0) {
    static char f[768];  // net task only; ~320 B for 12 rows
    if (len >= sizeof(f)) return;
    memcpy(f, payload, len);
    f[len] = 0;
    apply_forecast(f);
    return;
  }

  if (strcmp(suffix, "weather") == 0) {
    char w[256];
    if (len >= sizeof(w)) return;
    memcpy(w, payload, len);
    w[len] = '\0';
    apply_weather(w);
    return;
  }

  if (strncmp(suffix, HIST, sizeof(HIST) - 1) == 0) {
    static char big[1024];  // net task only; history payloads are ~450 B
    if (len >= sizeof(big)) return;
    memcpy(big, payload, len);
    big[len] = '\0';
    for (int k = 0; k < H_COUNT; k++) {
      if (!strcmp(suffix + sizeof(HIST) - 1, HIST_KEYS[k])) {
        apply_hist((HistKey)k, big);
        return;
      }
    }
    return;
  }

  char buf[48];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = '\0';
  // Attribute payloads are JSON-encoded: strip quotes from strings.
  char *p = buf;
  if (n >= 2 && p[0] == '"' && p[n - 1] == '"') {
    p[n - 1] = '\0';
    p++;
  }

  for (const Sub &s : SUBS) {
    if (strcmp(suffix, s.suffix) == 0) {
      apply(s.field, p);
      return;
    }
  }
}

// --- HA discovery for the panel itself ------------------------------------

static void publish_discovery() {
  static const char *DEVICE =
      "\"dev\":{\"ids\":[\"nldeskcyd\"],\"name\":\"NL Desk Panel\","
      "\"mf\":\"Sunton\",\"mdl\":\"ESP32-2432S028 (CYD)\"}";
  char buf[512];

  snprintf(buf, sizeof(buf),
           "{\"name\":\"Status\",\"uniq_id\":\"nldeskcyd_status\",\"dev_cla\":\"connectivity\","
           "\"stat_t\":\"" TOPIC_PANEL "status\",\"pl_on\":\"online\",\"pl_off\":\"offline\","
           "\"ent_cat\":\"diagnostic\",%s}",
           DEVICE);
  s_mqtt.publish("homeassistant/binary_sensor/nldeskcyd/status/config", buf, true);

  snprintf(buf, sizeof(buf),
           "{\"name\":\"WiFi signal\",\"uniq_id\":\"nldeskcyd_rssi\",\"dev_cla\":\"signal_strength\","
           "\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\",\"stat_t\":\"" TOPIC_PANEL "rssi\","
           "\"avty_t\":\"" TOPIC_PANEL "status\",\"ent_cat\":\"diagnostic\",%s}",
           DEVICE);
  s_mqtt.publish("homeassistant/sensor/nldeskcyd/rssi/config", buf, true);
}

static bool mqtt_connect() {
  if (!s_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD, TOPIC_PANEL "status", 1, true,
                      "offline")) {
    Serial.printf("[MQTT] connect failed, rc=%d\n", s_mqtt.state());
    return false;
  }
  Serial.println("[MQTT] connected");
  s_mqtt.publish(TOPIC_PANEL "status", "online", true);
  publish_discovery();
  // Specific topics, not cyd/nl/#: statestream also publishes every attribute
  // (friendly_name, icon, ...) and none of those are needed here.
  char topic[96];
  for (const Sub &s : SUBS) {
    snprintf(topic, sizeof(topic), TOPIC_BASE "%s", s.suffix);
    s_mqtt.subscribe(topic, 1);
  }
  s_mqtt.subscribe(TOPIC_BASE "hist/+", 1);
  s_mqtt.subscribe(TOPIC_BASE "weather", 1);
  s_mqtt.subscribe(TOPIC_BASE "forecast", 1);
  s_mqtt.subscribe(TOPIC_PANEL "bench", 0);
  return true;
}

static void set_connected(bool c) {
  portENTER_CRITICAL(&s_mux);
  s_model.mqtt_connected = c;
  portEXIT_CRITICAL(&s_mux);
}

static void setup_ota() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    // The OTA erases ~1.2 MB of flash up front, stalling both cores for
    // seconds; the 5 s loop watchdog (main.cpp) would reboot mid-upload.
    disableLoopWDT();
    portENTER_CRITICAL(&s_mux);
    s_model.ota_active = true;
    s_model.ota_percent = 0;
    portEXIT_CRITICAL(&s_mux);
    Serial.println("[OTA] start");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    portENTER_CRITICAL(&s_mux);
    s_model.ota_percent = total ? (int)(done * 100ULL / total) : 0;
    portEXIT_CRITICAL(&s_mux);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[OTA] error %u\n", e);
    portENTER_CRITICAL(&s_mux);
    s_model.ota_active = false;
    portEXIT_CRITICAL(&s_mux);
    enableLoopWDT();
  });
  ArduinoOTA.begin();
}

static float s_perf_fps = 0, s_perf_frame_ms = 0, s_perf_flush_ms = 0;
static int s_perf_sky = 0;

void net_set_perf(float fps, float frame_ms, float flush_ms, int sky_fps) {
  portENTER_CRITICAL(&s_mux);
  s_perf_fps = fps;
  s_perf_frame_ms = frame_ms;
  s_perf_flush_ms = flush_ms;
  s_perf_sky = sky_fps;
  portEXIT_CRITICAL(&s_mux);
}

static void drop_queued_commands() {
  Cmd c;
  while (xQueueReceive(s_cmd_queue, &c, 0) == pdTRUE) {
  }
}

static void net_task(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setSleep(false);  // modem sleep adds 100+ ms to every command
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);

  s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  s_mqtt.setCallback(on_message);
  s_mqtt.setBufferSize(1280);  // history payloads ~450 B, discovery ~300 B
  s_mqtt.setSocketTimeout(5);
  s_mqtt.setKeepAlive(30);

  bool ota_started = false;
  unsigned long next_try = 0, next_rssi = 0, next_perf = 0;

  for (;;) {
    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
      if (!ota_started) {
        Serial.printf("[WiFi] %s  rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        setup_ota();
        ota_started = true;
      }
      ArduinoOTA.handle();

      if (!s_mqtt.connected()) {
        set_connected(false);
        drop_queued_commands();
        if ((long)(now - next_try) >= 0) {
          next_try = now + 5000;
          if (mqtt_connect()) set_connected(true);
        }
      } else {
        s_mqtt.loop();
        Cmd c;
        while (xQueueReceive(s_cmd_queue, &c, 0) == pdTRUE) {
          const char *t = c.target == CmdTarget::BASIL ? TOPIC_CMD_BASIL : TOPIC_CMD_LIGHTBAR;
          Serial.printf("[MQTT] %s %s\n", t, c.json);
          s_mqtt.publish(t, c.json, false);
        }
        if ((long)(now - next_perf) >= 0) {
          next_perf = now + 10000;
          char p[128];
          portENTER_CRITICAL(&s_mux);
          snprintf(p, sizeof(p), "{\"fps\":%.1f,\"frame_ms\":%.1f,\"flush_ms\":%.1f,\"sky_fps\":%d,\"heap\":%u}",
                   s_perf_fps, s_perf_frame_ms, s_perf_flush_ms, s_perf_sky, 0u);
          portEXIT_CRITICAL(&s_mux);
          // heap read outside the critical section
          char *h = strstr(p, "\"heap\":0}");
          if (h) snprintf(h, sizeof(p) - (h - p), "\"heap\":%u}", ESP.getFreeHeap());
          s_mqtt.publish(TOPIC_PANEL "perf", p, false);
        }
        if ((long)(now - next_rssi) >= 0) {
          next_rssi = now + 60000;
          char r[8];
          snprintf(r, sizeof(r), "%d", WiFi.RSSI());
          s_mqtt.publish(TOPIC_PANEL "rssi", r, true);
        }
      }
    } else {
      set_connected(false);
      drop_queued_commands();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void net_start() {
  s_cmd_queue = xQueueCreate(8, sizeof(Cmd));
  xTaskCreatePinnedToCore(net_task, "net", 8192, nullptr, 1, nullptr, 0);
}
