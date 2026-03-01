/**
 * @file display.cpp
 * @brief OLED 显示实现：顶栏、WiFi/电池图标、时间位图、开机/NTP 提示
 */
#include "display.h"
#include "bitmap.h"
#include <Wire.h>
#include <WiFi.h>
#include <math.h>

#define BATTERY_ADC_PIN      34
/* 原理图 R10=20K R11=10K：Vadc = Vbat/3。满电 4.2V→1.4V，空电 3.0V→1.0V (12bit@3.3V) */
#define ADC_RAW_EMPTY        1241   /* 空电约 3.0V → 1.0V@ADC */
#define ADC_RAW_FULL         1738   /* 满电约 4.2V → 1.4V@ADC */
#define BIG_BYTES  3
#define DOT_BYTES  1
#define MINI_BYTES 1
#define MINI_Y     (TIME_Y_TOP + BIG_H - MINI_H)

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const unsigned char* const BIG_DIGIT[] = {
    IMAGE_0, IMAGE_1, IMAGE_2, IMAGE_3, IMAGE_4,
    IMAGE_5, IMAGE_6, IMAGE_7, IMAGE_8, IMAGE_9
};
static const unsigned char* const MINI_DIGIT[] = {
    IMAGE_MINI_0, IMAGE_MINI_1, IMAGE_MINI_2, IMAGE_MINI_3, IMAGE_MINI_4,
    IMAGE_MINI_5, IMAGE_MINI_6, IMAGE_MINI_7, IMAGE_MINI_8, IMAGE_MINI_9
};

void displayInit(void) {
    Wire.begin(I2C_SDA, I2C_SCL);
    u8g2.setBusClock(400000);
    u8g2.begin();
}

void displayTopBarBackground(void) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 0, SCREEN_W, TOP_BAR_H);
    u8g2.setDrawColor(1);
}

void displayWiFiIcon(int x, int y, bool connected) {
    const int cx = x + 7;
    const int tipY = y + 11;
    const int maxW = 14;
    const int maxH = 12;
    if (!connected) {
        u8g2.drawFrame(x, y, maxW, maxH);
        u8g2.drawLine(x + 2, y + maxH - 1, x + 12, y + 2);
        u8g2.drawLine(x + 2, y + 2, x + 12, y + maxH - 1);
        return;
    }
    const int radii[] = { 2, 5, 8 };
    const int degStart = 145;
    const int degEnd   = 35;
    const int degStep  = 8;
    for (int ri = 0; ri < 3; ri++) {
        int r = radii[ri];
        int prev_x = -999, prev_y = -999;
        for (int d = degStart; d >= degEnd; d -= degStep) {
            float rad = d * 3.14159265f / 180.0f;
            int px = (int)(cx + r * cosf(rad) + 0.5f);
            int py = (int)(tipY - r * sinf(rad) + 0.5f);
            if (prev_x != -999)
                u8g2.drawLine(prev_x, prev_y, px, py);
            prev_x = px;
            prev_y = py;
        }
    }
}

void displayBatteryIcon(int x, int y, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const int w = BATTERY_ICON_W - 3;
    const int h = BATTERY_ICON_H - 2;
    const int pad = 2;
    const int innerW = w - 2 * pad;
    const int innerH = h - 2 * pad;
    if (innerW <= 0 || innerH <= 0) return;
    u8g2.drawFrame(x, y, w, h);
    u8g2.drawBox(x + w, y + 2, 2, h - 4);
    int fill = percent >= 100 ? innerW : (innerW * percent + 99) / 100;
    if (fill > innerW) fill = innerW;
    if (fill > 0)
        u8g2.drawBox(x + pad, y + pad, fill, innerH);
}

int displayGetBatteryPercent(void) {
    const int samples = 8;
    uint32_t sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(BATTERY_ADC_PIN);
        delay(3);
    }
    int raw = sum / samples;
    int p = (raw - ADC_RAW_EMPTY) * 100 / (ADC_RAW_FULL - ADC_RAW_EMPTY);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

void displayDrawBigDigit(int x, int y, int d) {
    if (d >= 0 && d <= 9)
        u8g2.drawBitmap(x, y, BIG_BYTES, BIG_H, BIG_DIGIT[d]);
}

void displayDrawMiniDigit(int x, int y, int d) {
    if (d >= 0 && d <= 9)
        u8g2.drawBitmap(x, y, MINI_BYTES, MINI_H, MINI_DIGIT[d]);
}

void displayDrawDot(int x, int y) {
    u8g2.drawBitmap(x, y, DOT_BYTES, DOT_H, IMAGE_DOT);
}

void displayDrawTime(int hour, int minute, int second) {
    int h1 = hour / 10, h2 = hour % 10;
    int m1 = minute / 10, m2 = minute % 10;
    int s1 = second / 10, s2 = second % 10;
    int x = MARGIN_LEFT;
    displayDrawBigDigit(x, TIME_Y_TOP, h1);  x += BIG_W;
    displayDrawBigDigit(x, TIME_Y_TOP, h2);  x += BIG_W;
    if ((second & 1) == 0)
        displayDrawDot(x, TIME_Y_TOP);
    x += DOT_W;
    displayDrawBigDigit(x, TIME_Y_TOP, m1);  x += BIG_W;
    displayDrawBigDigit(x, TIME_Y_TOP, m2);  x += BIG_W;
    x += 4;
    displayDrawMiniDigit(x, MINI_Y, s1);  x += MINI_W;
    displayDrawMiniDigit(x, MINI_Y, s2);
}

#define OI_WIFI  0xe0d9
#define OI_X     0xe0db

void displayBootScreen(bool showWifiIcon, bool wifiOk, const char* title, const char* subtitle) {
    u8g2.clearBuffer();
    const int iconSize = 16;
    const int iconY = 0;
    if (showWifiIcon) {
        u8g2.setBitmapMode(1);
        u8g2.setFont(u8g2_font_open_iconic_www_2x_t);
        u8g2.drawGlyph((SCREEN_W - iconSize) / 2, iconY + iconSize, wifiOk ? OI_WIFI : OI_X);
        u8g2.setBitmapMode(0);
    }
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    int cy = iconY + iconSize + 10;
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, cy, title);
    cy += 16;
    if (subtitle && subtitle[0]) {
        int sw = u8g2.getUTF8Width(subtitle);
        u8g2.drawUTF8((SCREEN_W - sw) / 2, cy, subtitle);
    }
    u8g2.sendBuffer();
}

#define OI_CLOCK  (64 + 5)
#define NTP_ICON_SIZE  32
#define NTP_ICON_GAP   12
#define NTP_TEXT_H     12

void displayNtpBootScreen(const char* title, int dotCount) {
    u8g2.clearBuffer();
    const int totalH = NTP_ICON_SIZE + NTP_ICON_GAP + NTP_TEXT_H;
    const int startY = (SCREEN_H - totalH) / 2;
    const int iconY = startY;
    const int cx = SCREEN_W / 2;
    u8g2.setBitmapMode(1);
    u8g2.setFont(u8g2_font_open_iconic_app_4x_t);
    u8g2.drawGlyph(cx - NTP_ICON_SIZE / 2, iconY + NTP_ICON_SIZE, OI_CLOCK);
    u8g2.setBitmapMode(0);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* dots[] = { "", ".", "..", "..." };
    int d = (dotCount >= 0 && dotCount <= 3) ? dotCount : 0;
    char line[32];
    snprintf(line, sizeof(line), "%s%s", title, dots[d]);
    int tw = u8g2.getUTF8Width(line);
    int textY = iconY + NTP_ICON_SIZE + NTP_ICON_GAP + NTP_TEXT_H - 2;
    u8g2.drawUTF8(cx - tw / 2, textY, line);
    u8g2.sendBuffer();
}

void displayPlaceholderPage(const char* title, const char* hint) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, 18, title);
    if (hint && hint[0]) {
        int hw = u8g2.getUTF8Width(hint);
        u8g2.drawUTF8((SCREEN_W - hw) / 2, 42, hint);
    }
    const char* back = u8"中键长按返回";
    int bw = u8g2.getUTF8Width(back);
    u8g2.drawUTF8((SCREEN_W - bw) / 2, 58, back);
    u8g2.sendBuffer();
}
