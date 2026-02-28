/**
 * @file clock_screen.cpp
 * @brief 时钟页：顶栏 + 日期 + 时间位图；NTP 同步
 */
#include "clock_screen.h"
#include "display.h"
#include "app_state.h"
#include <WiFi.h>
#include <time.h>

static const char* ntpServer = "ntp.aliyun.com";
static const long  gmtOffset_sec = 8 * 3600;
static const int   daylightOffset_sec = 0;

bool clockScreenSyncNtp(void (*drawNtp)(const char*, int), int maxTries, int intervalMs) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm t = {};
    for (int i = 0; i < maxTries; i++) {
        if (drawNtp) drawNtp(u8"正在同步时间", i % 4);
        if (getLocalTime(&t))
            return true;
        delay(intervalMs);
    }
    return false;
}

void clockScreenDraw(void) {
    struct tm t;
    if (!getLocalTime(&t)) return;

    u8g2.clearBuffer();
    displayTopBarBackground();
    displayWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);

    char date[16];
    snprintf(date, sizeof(date), "%04d/%02d/%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    u8g2.setFont(u8g2_font_7x13_tf);
    int dateW = u8g2.getUTF8Width(date);
    u8g2.drawStr((SCREEN_W - dateW) / 2, DATE_Y_TOP, date);

    displayBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, displayGetBatteryPercent());
    displayDrawTime(t.tm_hour, t.tm_min, t.tm_sec);
    u8g2.sendBuffer();
}
