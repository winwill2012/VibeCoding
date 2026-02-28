/**
 * @file stopwatch_screen.cpp
 * @brief 秒表：三位大数字秒 + 三位小数字毫秒
 */
#include "stopwatch_screen.h"
#include "display.h"
#include "app_state.h"
#include <WiFi.h>

#define STOPWATCH_TIME_Y   TIME_Y_TOP
#define STOPWATCH_MINI_Y   (STOPWATCH_TIME_Y + BIG_H - MINI_H)
#define STOPWATCH_TOTAL_W  (3 * BIG_W + 3 * MINI_W)
#define STOPWATCH_START_X  ((SCREEN_W - STOPWATCH_TOTAL_W) / 2)

void stopwatchScreenDraw(void) {
    uint32_t totalMs = g_stopwatchAccumulatedMs;
    if (g_stopwatchRunStartMillis != 0)
        totalMs += (uint32_t)(millis() - g_stopwatchRunStartMillis);
    uint32_t sec = totalMs / 1000;
    uint32_t ms = totalMs % 1000;
    if (sec > 999) sec = 999;
    int s1 = (int)(sec / 100), s2 = (int)((sec / 10) % 10), s3 = (int)(sec % 10);
    int m1 = (int)(ms / 100), m2 = (int)((ms / 10) % 10), m3 = (int)(ms % 10);

    u8g2.clearBuffer();
    displayTopBarBackground();
    displayWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* title = u8"秒表";
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, DATE_Y_TOP, title);
    displayBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, displayGetBatteryPercent());

    int x = STOPWATCH_START_X;
    displayDrawBigDigit(x, STOPWATCH_TIME_Y, s1);  x += BIG_W;
    displayDrawBigDigit(x, STOPWATCH_TIME_Y, s2);  x += BIG_W;
    displayDrawBigDigit(x, STOPWATCH_TIME_Y, s3);  x += BIG_W;
    int miniBlockCenterX = x + (3 * MINI_W) / 2;
    const char* msLabel = u8"毫秒";
    int labelW = u8g2.getUTF8Width(msLabel);
    int labelY = STOPWATCH_TIME_Y + u8g2.getAscent() + 6;
    u8g2.drawUTF8(miniBlockCenterX - labelW / 2, labelY, msLabel);
    displayDrawMiniDigit(x, STOPWATCH_MINI_Y, m1);  x += MINI_W;
    displayDrawMiniDigit(x, STOPWATCH_MINI_Y, m2);  x += MINI_W;
    displayDrawMiniDigit(x, STOPWATCH_MINI_Y, m3);

    u8g2.sendBuffer();
}
