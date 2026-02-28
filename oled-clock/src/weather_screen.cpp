/**
 * @file weather_screen.cpp
 * @brief 天气：心知 API 拉取、左图标右城市/温度，底部 IP 条
 */
#include "weather_screen.h"
#include "display.h"
#include "app_state.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define SENIVERE_API_KEY  "SHOEXKwNcHrAxuw09"
#define WEATHER_CACHE_MS  (10 * 60 * 1000)
#define WEATHER_LEFT_W     64
#define WEATHER_DIVIDER_X  66
#define WEATHER_ICON_SIZE  32
#define WEATHER_RIGHT_CX   96
#define WEATHER_LINE_H     14
#define WEATHER_CONTENT_TOP (TOP_BAR_H + 10)
#define WEATHER_LOADING_ICON_SIZE  32
#define WEATHER_LOADING_ICON_CODE  69

static void mapWeatherToDisplay(const char* apiText, char* outTwoChars, int* outIconCode) {
    if (!apiText || !apiText[0]) {
        strcpy(outTwoChars, u8"晴");
        *outIconCode = 69;
        return;
    }
    if (strstr(apiText, u8"晴") && !strstr(apiText, u8"云")) {
        strcpy(outTwoChars, u8"晴");
        *outIconCode = 69;
        return;
    }
    if (strstr(apiText, u8"雪")) {
        strcpy(outTwoChars, u8"雪");
        *outIconCode = 66;
        return;
    }
    if (strstr(apiText, u8"雨")) {
        strcpy(outTwoChars, u8"雨");
        *outIconCode = 67;
        return;
    }
    if (strstr(apiText, u8"云") || strstr(apiText, u8"阴")) {
        strcpy(outTwoChars, strstr(apiText, u8"阴") ? u8"阴" : u8"多云");
        *outIconCode = 65;
        return;
    }
    if (strstr(apiText, u8"雾") || strstr(apiText, u8"霾")) {
        strcpy(outTwoChars, strstr(apiText, u8"霾") ? u8"霾" : u8"雾");
        *outIconCode = 64;
        return;
    }
    strcpy(outTwoChars, u8"晴");
    *outIconCode = 69;
}

static bool fetchWeather(void) {
    if (WiFi.status() != WL_CONNECTED) return false;
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);
    String url = "https://api.seniverse.com/v3/weather/now.json?key=";
    url += SENIVERE_API_KEY;
    url += "&location=";
    url += g_weatherLocation;
    url += "&language=zh-Hans&unit=c";
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();
    int ti = body.indexOf("\"temperature\":\"");
    int tei = body.indexOf("\"text\":\"");
    int locIdx = body.indexOf("\"location\"");
    if (locIdx >= 0) {
        int nameKey = body.indexOf("\"name\":\"", locIdx);
        if (nameKey >= 0) {
            int start = nameKey + 8;
            int end = body.indexOf('"', start);
            if (end > start && end - start < (int)sizeof(g_weatherCityName)) {
                body.substring(start, end).toCharArray(g_weatherCityName, sizeof(g_weatherCityName));
                g_weatherCityName[sizeof(g_weatherCityName) - 1] = '\0';
            }
        }
    }
    if (ti >= 0) {
        int start = ti + 15;
        int end = body.indexOf('"', start);
        if (end > start && end - start < 7) {
            body.substring(start, end).toCharArray(g_weatherTemp, sizeof(g_weatherTemp));
        }
    }
    if (tei >= 0) {
        int start = tei + 8;
        int end = body.indexOf('"', start);
        if (end > start) {
            String text = body.substring(start, end);
            char two[8];
            mapWeatherToDisplay(text.c_str(), two, &g_weatherIconCode);
            strncpy(g_weatherText, two, sizeof(g_weatherText) - 1);
            g_weatherText[sizeof(g_weatherText) - 1] = '\0';
        }
    }
    g_weatherLastFetch = millis();
    return true;
}

static void drawWeatherLoadingScreen(void) {
    u8g2.clearBuffer();
    const int cx = SCREEN_W / 2;
    const int iconH = WEATHER_LOADING_ICON_SIZE;
    const int gap = 10;
    const int textH = 12;
    const int totalH = iconH + gap + textH;
    const int startY = (SCREEN_H - totalH) / 2;
    const int iconY = startY;
    u8g2.setBitmapMode(1);
    u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
    u8g2.drawGlyph(cx - WEATHER_LOADING_ICON_SIZE / 2, iconY + WEATHER_LOADING_ICON_SIZE, WEATHER_LOADING_ICON_CODE);
    u8g2.setBitmapMode(0);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* msg = u8"正在获取天气";
    int w = u8g2.getUTF8Width(msg);
    int textBaseline = iconY + iconH + gap + textH - 2;
    u8g2.drawUTF8(cx - w / 2, textBaseline, msg);
    u8g2.sendBuffer();
}

void weatherScreenDraw(void) {
    bool needFetch = (g_weatherLastFetch == 0 || (uint32_t)(millis() - g_weatherLastFetch) > WEATHER_CACHE_MS);
    if (needFetch && WiFi.status() == WL_CONNECTED) {
        drawWeatherLoadingScreen();
        fetchWeather();
    }
    u8g2.clearBuffer();
    displayTopBarBackground();
    displayWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* title = u8"实时天气";
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, DATE_Y_TOP, title);
    displayBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, displayGetBatteryPercent());

    int contentTop = WEATHER_CONTENT_TOP;
    int contentH = SCREEN_H - contentTop;
    int leftCenterX = WEATHER_LEFT_W / 2;
    const int iconY = contentTop - 6;
    const int iconBottom = iconY + WEATHER_ICON_SIZE;

    u8g2.drawVLine(WEATHER_DIVIDER_X, contentTop, contentH);
    u8g2.setBitmapMode(1);
    u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
    u8g2.drawGlyph(leftCenterX - WEATHER_ICON_SIZE / 2, iconBottom, g_weatherIconCode);
    u8g2.setBitmapMode(0);

    if (WiFi.status() == WL_CONNECTED) {
        const int ipBarH = 8;
        const int ipBarY = SCREEN_H - ipBarH;
        u8g2.drawRBox(0, ipBarY, WEATHER_LEFT_W, ipBarH, 1);
        String ipStr = WiFi.localIP().toString();
        u8g2.setFont(u8g2_font_4x6_tf);
        int ipW = u8g2.getStrWidth(ipStr.c_str());
        int ipBaseline = SCREEN_H - 2;
        int ipX = leftCenterX - ipW / 2;
        if (ipX < 2) ipX = 2;
        if (ipX + ipW > WEATHER_LEFT_W - 2) ipX = WEATHER_LEFT_W - ipW - 2;
        u8g2.setDrawColor(0);
        u8g2.drawStr(ipX, ipBaseline, ipStr.c_str());
        u8g2.setDrawColor(1);
    }
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    int cityW = u8g2.getUTF8Width(g_weatherCityName);
    int cityX = WEATHER_RIGHT_CX - cityW / 2;
    int cityY = contentTop + WEATHER_LINE_H - 2;
    u8g2.drawRBox(cityX - 2, cityY - 11, cityW + 4, 14, 2);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(cityX, cityY, g_weatherCityName);
    u8g2.setDrawColor(1);

    int ry = contentTop + WEATHER_LINE_H + 18;
    int textW = u8g2.getUTF8Width(g_weatherText);
    int gap = 6;
    const char* celsiusStr = u8"℃";
    int celsiusW = u8g2.getUTF8Width(celsiusStr);
    u8g2.setFont(u8g2_font_7x13B_tf);
    int tempNumW = u8g2.getStrWidth(g_weatherTemp);
    int totalW = textW + gap + tempNumW + celsiusW;
    int lineX = WEATHER_RIGHT_CX - totalW / 2;
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.drawUTF8(lineX, ry, g_weatherText);
    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.drawStr(lineX + textW + gap, ry, g_weatherTemp);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.drawUTF8(lineX + textW + gap + tempNumW, ry, celsiusStr);
    u8g2.sendBuffer();
}
