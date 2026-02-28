/**
 * @file timer_screen.cpp
 * @brief 倒计时：位图 MM:SS、三角指示当前位、结束蜂鸣
 */
#include "timer_screen.h"
#include "display.h"
#include "app_state.h"
#include "bitmap.h"
#include <Arduino.h>
#include <WiFi.h>

#define BUZZER_PIN  23
#define TIMER_LEDC_CHANNEL  0
#define DOT_BYTES  1
#define DOT_H      32

#define TIMER_TIME_Y    TIME_Y_TOP
#define TIMER_COLON_W   DOT_W
#define TIMER_TOTAL_W   (4 * BIG_W + TIMER_COLON_W)
#define TIMER_START_X   ((SCREEN_W - TIMER_TOTAL_W) / 2)
#define TIMER_TRI_TIP_Y   (TIMER_TIME_Y + BIG_H + 1)
#define TIMER_TRI_BASE_Y  (TIMER_TIME_Y + BIG_H + 7)

static int timerDigitCenterX(int pos) {
    if (pos <= 1)
        return TIMER_START_X + pos * BIG_W + BIG_W / 2;
    return TIMER_START_X + 2 * BIG_W + TIMER_COLON_W + (pos - 2) * BIG_W + BIG_W / 2;
}

void timerScreenPlayBeep(void) {
    const uint32_t totalMs = 10000;
    const int freq = 2000;
    const int beepOnMs = 180;
    const int beepOffMs = 120;
    const int pauseBetweenPairsMs = 480;
    uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < totalMs) {
        ledcWriteTone(TIMER_LEDC_CHANNEL, freq);
        delay(beepOnMs);
        ledcWriteTone(TIMER_LEDC_CHANNEL, 0);
        delay(beepOffMs);
        ledcWriteTone(TIMER_LEDC_CHANNEL, freq);
        delay(beepOnMs);
        ledcWriteTone(TIMER_LEDC_CHANNEL, 0);
        delay(pauseBetweenPairsMs);
    }
    ledcWriteTone(TIMER_LEDC_CHANNEL, 0);
}

void timerScreenDraw(void) {
    u8g2.clearBuffer();
    displayTopBarBackground();
    displayWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* title = u8"倒计时";
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, DATE_Y_TOP, title);
    displayBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, displayGetBatteryPercent());

    int m1 = g_timerDigits[0], m2 = g_timerDigits[1];
    int s1 = g_timerDigits[2], s2 = g_timerDigits[3];
    if (g_timerRunning) {
        uint32_t remain = (g_timerEndMillis > millis()) ? (g_timerEndMillis - millis()) : 0;
        uint32_t sec = remain / 1000;
        m1 = (int)(sec / 60) / 10;
        m2 = (int)(sec / 60) % 10;
        s1 = (int)(sec % 60) / 10;
        s2 = (int)(sec % 60) % 10;
    }

    int x = TIMER_START_X;
    displayDrawBigDigit(x, TIMER_TIME_Y, m1);  x += BIG_W;
    displayDrawBigDigit(x, TIMER_TIME_Y, m2);  x += BIG_W;
    u8g2.drawBitmap(x, TIMER_TIME_Y, DOT_BYTES, DOT_H, IMAGE_DOT);
    x += TIMER_COLON_W;
    displayDrawBigDigit(x, TIMER_TIME_Y, s1);  x += BIG_W;
    displayDrawBigDigit(x, TIMER_TIME_Y, s2);

    if (!g_timerRunning) {
        int cx = timerDigitCenterX(g_timerDigitPos);
        u8g2.drawTriangle(cx - 5, TIMER_TRI_BASE_Y, cx, TIMER_TRI_TIP_Y, cx + 5, TIMER_TRI_BASE_Y);
    }

    u8g2.sendBuffer();
}
