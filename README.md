# NLDeskCYD

Desk dashboard for a one-room studio, on an ESP32-2432S028 "Cheap Yellow
Display" (320x240 resistive touch). Sibling of
[PCDeskCYD](https://github.com/Hydr0neFN/PCDeskCYD): same board bring-up
(TFT_eSPI `ILI9341_2_DRIVER` + `TFT_INVERSION_ON`, touch on its own VSPI bus),
different UI and transport.

## Data path

```
[Home Assistant on nl-pi]
  mqtt_statestream ──> cyd/nl/<domain>/<object_id>/state   (retained)
  cyd-hist.timer   ──> cyd/nl/hist/<key>                   (retained, 5 min)
  automation cyd_nl_command        <── cyd/nl/cmd/lightbar
  automation cyd_nl_basil_command  <── cyd/nl/cmd/basil
                     ▲ MQTT (mosquitto 10.0.0.20:1883, user "cyd")
[CYD] core 0: WiFi + PubSubClient + ArduinoOTA + SNTP  (src/net.cpp)
      core 1: LVGL 9.5 UI                              (src/ui*.cpp)
```

No Home Assistant token lives on the device; only the entities listed under
`mqtt_statestream` in HA's `configuration.yaml` leave HA. Chart history comes
from `/opt/cyd-hist/cyd_hist.py` on nl-pi, which reads the recorder's
statistics tables read-only.

## Pages

| Page   | Opened by            | Content |
|--------|----------------------|---------|
| Home   | —                    | time/date, link dot, alert pill, light-bar toggle, 4 cards |
| Room   | room card            | 24 h temp / humidity / CO2 chart (tabs), pressure |
| Light  | light-bar card       | on/off, 15-step brightness and colour temp, presets |
| Power  | power card           | 24 h house vs PC W, 7-day kWh bars |
| Basil  | basil card           | 48 h soil / tank chart, plant light, two-tap water / clear fault |
| Colour | long-press the clock | palette swatches + display inversion toggle (persisted) |

Backlight: bright while touched, dim glance when idle by day (brighter while
an alert is active), fully dark 00:30-07:30. A touch on a dim screen only
wakes it.

## Build

```
cp include/secrets.h.example include/secrets.h   # fill in
cp secrets.ini.example secrets.ini               # same OTA password
pio run -e cyd -t upload --upload-port COMx      # first flash (hold BOOT if auto-reset fails)
pio run -e cyd_ota -t upload                     # every flash after that
```

Arduino-ESP32 core 2.0.x (platform espressif32 7.x): LEDC uses the channel API.

## Fonts

CJK labels use Noto Sans TC subsets generated from the characters actually in
`src/*.cpp`. After changing any CJK text: `python tools/gen_fonts.py`.
