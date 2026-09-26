# NLDeskCYD

[English](README.md)

專為套房（one-room studio）設計的桌面儀表板，運行於 ESP32-2432S028「Cheap Yellow Display」（320x240 電阻式觸控螢幕）。為 [PCDeskCYD](https://github.com/Hydr0neFN/PCDeskCYD) 的姊妹專案：採用相同的開發板初始化設定（TFT_eSPI `ILI9341_2_DRIVER` + `TFT_INVERSION_ON`，觸控使用獨立的 VSPI 匯流排），但採用不同的 UI 與傳輸機制。

## 資料路徑

```
[Home Assistant on nl-pi]
  mqtt_statestream ──> cyd/nl/<domain>/<object_id>/state   (retained)
  cyd-hist.timer   ──> cyd/nl/hist/<key>                   (retained, 5 min)
  automation cyd_nl_weather_publish ──> cyd/nl/weather     (retained, 10 min)
  automation cyd_nl_command        <── cyd/nl/cmd/lightbar
  automation cyd_nl_basil_command  <── cyd/nl/cmd/basil
                     ▲ MQTT (mosquitto 10.0.0.20:1883, user "cyd")
[CYD] core 0: WiFi + PubSubClient + ArduinoOTA + SNTP  (src/net.cpp)
      core 1: LVGL 9.5 UI                              (src/ui*.cpp)
```

裝置上未存放任何 Home Assistant 權杖（token）；只有在 HA 的 `configuration.yaml` 中 `mqtt_statestream` 底下列出的實體才會傳出 HA。圖表歷史資料來自 nl-pi 上的 `/opt/cyd-hist/cyd_hist.py`，該腳本以唯讀方式讀取 recorder 的統計資料表。

## 頁面

| 頁面   | 開啟方式             | 內容 |
|--------|----------------------|------|
| 首頁   | —                    | 動態天氣天空背景、時間／日期、室外天氣或警示膠囊、4 張卡片 |
| 室內   | 室內卡片             | 24 小時溫度／濕度／CO2 圖表（分頁切換）、氣壓 |
| 掛燈   | 掛燈卡片             | 開／關、15 階亮度與色溫調整、預設模式 |
| 用電   | 用電卡片             | 24 小時全屋 vs 電腦瓦數、7 天 kWh 長條圖 |
| 羅勒   | 羅勒卡片             | 48 小時土壤／水箱圖表、植物燈、點兩次確認的澆水／清除異常 |
| 螢幕校色 | 長按時鐘           | 調色盤色票＋螢幕反相切換（設定會保存） |

## 天空背景

首頁背景是動態天空，採用與使用者投影機喚醒頁面相同的天氣條件分組與漸層表：白天／黃昏／夜晚漸層、飄動的雲朵、降雨、降雪、霧帶以及隱約的閃電。太陽與月亮沿真實軌跡移動，由裝置端依住家位置（`include/config.h` 中的 `HOME_LAT` / `HOME_LON`，只取到城市層級精度）以低精度星曆（ephemeris）計算：方位角由東到西對應螢幕由左到右，仰角對應高度，地平線在螢幕底緣，因此升起的天體會先在左下方露出大半圓，再於右下方落下。日月只在晴朗或晴時多雲的天氣繪製。動畫在使用中以 20 fps 執行，微亮閒置模式為 4 fps，夜間完全停止。

背光：觸控時全亮；白天閒置時微亮（有警示時亮度較高）；00:30–07:30 完全關閉。觸碰微亮的螢幕只會喚醒，不會觸發按鈕。

## 建置

```
cp include/secrets.h.example include/secrets.h   # fill in
cp secrets.ini.example secrets.ini               # same OTA password
pio run -e cyd -t upload --upload-port COMx      # first flash (hold BOOT if auto-reset fails)
pio run -e cyd_ota -t upload                     # every flash after that
```

Arduino-ESP32 core 2.0.x（platform espressif32 7.x）：LEDC 使用 channel API。

## 字型

CJK 標籤使用 Noto Sans TC 子集（一般與粗體），字元集取自 `src/*.cpp` 中實際出現的文字。修改任何 CJK 文字後請執行：`python tools/gen_fonts.py`（需要 node 與 fontTools）。
