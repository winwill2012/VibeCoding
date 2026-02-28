/**
 * @file main.cpp
 * @brief ESP32 + SH1106 OLED：主菜单 + 时钟/日历/天气/计时/秒表，三键操作
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <time.h>

#include "bitmap.h"
#include "wifi_config.h"
#include "buttons.h"

// 应用状态：主菜单 + 五个子页面
enum AppState {
    STATE_MENU,
    STATE_CLOCK,
    STATE_CALENDAR,
    STATE_WEATHER,
    STATE_TIMER,
    STATE_STOPWATCH
};

static AppState s_state = STATE_MENU;
static int s_menuIndex = 0;  // 0=时钟, 1=日历, 2=天气, 3=计时, 4=秒表
static int s_calYear = 2026;   // 日历当前显示年/月（进入日历时设为当天）
static int s_calMonth = 1;

// SH1106 128x64, 硬件 I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

#define I2C_SDA  21
#define I2C_SCL  22

// 电池 ADC：3.7V 锂电池，GPIO34（仅能测 ≤3.3V，需分压）
// 用 ADC 原始值校准更准：满电和空电时各测一次 raw 填到下面
#define BATTERY_ADC_PIN      34
#define ADC_RAW_EMPTY        1100   // 空电时的 ADC 值（可实测后改）
#define ADC_RAW_FULL         2600   // 满电时的 ADC 值（可实测后改）

// NTP
const char* ntpServer = "ntp.aliyun.com";
const long  gmtOffset_sec = 8 * 3600;       // 东八区 UTC+8
const int   daylightOffset_sec = 0;

// 位图尺寸（drawBitmap 用：每行字节数 + 高度）
#define BIG_W     24
#define BIG_H     32
#define BIG_BYTES 3   // (24+7)/8
#define DOT_W     8
#define DOT_H     32
#define DOT_BYTES 1
#define MINI_W    8
#define MINI_H    12
#define MINI_BYTES 1

// 大数字位图表
static const unsigned char* const BIG_DIGIT[] = {
    IMAGE_0, IMAGE_1, IMAGE_2, IMAGE_3, IMAGE_4,
    IMAGE_5, IMAGE_6, IMAGE_7, IMAGE_8, IMAGE_9
};

// 小数字位图表
static const unsigned char* const MINI_DIGIT[] = {
    IMAGE_MINI_0, IMAGE_MINI_1, IMAGE_MINI_2, IMAGE_MINI_3, IMAGE_MINI_4,
    IMAGE_MINI_5, IMAGE_MINI_6, IMAGE_MINI_7, IMAGE_MINI_8, IMAGE_MINI_9
};

// 布局：顶部栏 [WiFi图标] 日期(大字体) [电池图标]，下方位图时间
#define TOP_BAR_H     14
#define DATE_Y_TOP    11
#define TIME_Y_TOP    24
#define MINI_Y        (TIME_Y_TOP + BIG_H - MINI_H)
#define SCREEN_W     128
#define SCREEN_H     64
#define MARGIN_LEFT   2
#define WIFI_ICON_X   2
#define WIFI_ICON_Y   1
#define BATTERY_ICON_W  18
#define BATTERY_ICON_H  10
#define BATTERY_ICON_X  (SCREEN_W - BATTERY_ICON_W - 2)
#define BATTERY_ICON_Y  2

// 使用 drawBitmap（MSB 先），与 img2lcd 导出的位图格式一致，避免花屏
static inline void drawBigDigit(int x, int y, int d) {
    if (d >= 0 && d <= 9)
        u8g2.drawBitmap(x, y, BIG_BYTES, BIG_H, BIG_DIGIT[d]);
}

static inline void drawMiniDigit(int x, int y, int d) {
    if (d >= 0 && d <= 9)
        u8g2.drawBitmap(x, y, MINI_BYTES, MINI_H, MINI_DIGIT[d]);
}

static void drawTime(int hour, int minute, int second) {
    int h1 = hour / 10, h2 = hour % 10;
    int m1 = minute / 10, m2 = minute % 10;
    int s1 = second / 10, s2 = second % 10;

    int x = MARGIN_LEFT;
    drawBigDigit(x, TIME_Y_TOP, h1);  x += BIG_W;
    drawBigDigit(x, TIME_Y_TOP, h2);  x += BIG_W;
    u8g2.drawBitmap(x, TIME_Y_TOP, DOT_BYTES, DOT_H, IMAGE_DOT);  x += DOT_W;
    drawBigDigit(x, TIME_Y_TOP, m1);  x += BIG_W;
    drawBigDigit(x, TIME_Y_TOP, m2);  x += BIG_W;
    x += 4;
    drawMiniDigit(x, MINI_Y, s1);  x += MINI_W;
    drawMiniDigit(x, MINI_Y, s2);
}

// 网络标识：四条竖线，底对齐，从左到右依次变高
static void drawWiFiIcon(int x, int y, bool connected) {
    const int barW = 2;
    const int gap = 1;
    const int maxH = 11;
    if (!connected) {
        u8g2.drawFrame(x, y, 14, maxH + 1);
        u8g2.drawLine(x + 2, y + maxH - 1, x + 12, y + 2);
        u8g2.drawLine(x + 2, y + 2, x + 12, y + maxH - 1);
        return;
    }
    int heights[] = { 3, 6, 9, 11 };
    int bottom = y + maxH;
    for (int i = 0; i < 4; i++) {
        int h = heights[i];
        int bx = x + i * (barW + gap);
        u8g2.drawBox(bx, bottom - h, barW, h);
    }
}

// 电池图标：右侧顶部，外框+按电量填充（满电时填满内框）
static void drawBatteryIcon(int x, int y, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const int w = BATTERY_ICON_W - 3;
    const int h = BATTERY_ICON_H - 2;
    const int innerW = w - 2;
    u8g2.drawFrame(x, y, w, h);
    u8g2.drawBox(x + w, y + 2, 2, h - 4);
    int fill = percent >= 100 ? innerW : (innerW * percent + 99) / 100;
    if (fill > innerW) fill = innerW;
    if (fill > 0)
        u8g2.drawBox(x + 1, y + 1, fill, h - 2);
}

// GPIO34 ADC 读取，按原始值线性映射到 0~100%（用 ADC_RAW_EMPTY/ADC_RAW_FULL 校准）
static int getBatteryPercent() {
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

static bool syncNtp() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm t = {};
    for (int i = 0; i < 20; i++) {
        if (getLocalTime(&t))
            return true;
        delay(500);
    }
    return false;
}

// Open Iconic 编码：WiFi=0xe0d9, 叉号(失败)=0xe0db (见 open-iconic)
#define OI_WIFI  0xe0d9
#define OI_X     0xe0db

// 开机提示：用 Open Iconic www 图标 + 中文（gb2312 覆盖更全）
static void drawBootScreen(bool showWifiIcon, bool wifiOk, const char* title, const char* subtitle) {
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

// 开机提示（中文）：NTP 页，时钟在上、文字在下
static void drawNtpBootScreen(const char* title, const char* subtitle) {
    u8g2.clearBuffer();
    int cx = SCREEN_W / 2;
    const int clockY = 2;
    const int clockR = 10;
    u8g2.drawCircle(cx, clockY + clockR, clockR);
    u8g2.drawLine(cx, clockY + clockR, cx - 4, clockY + 6);
    u8g2.drawLine(cx, clockY + clockR, cx, clockY + 2);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    int cy = clockY + clockR * 2 + 10;
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, cy, title);
    cy += 16;
    if (subtitle && subtitle[0]) {
        int sw = u8g2.getUTF8Width(subtitle);
        u8g2.drawUTF8((SCREEN_W - sw) / 2, cy, subtitle);
    }
    u8g2.sendBuffer();
}

// 主菜单：Open Iconic 图标 + 文字，一屏 3 项，左右小箭头，整体居中
#define OI_APP_CLOCK     (64 + 5)
#define OI_APP_CALENDAR  (64 + 2)
#define OI_APP_TIMER     (64 + 8)
#define OI_WEATHER_SUN   (64 + 5)
#define OI_PLAY_RECORD   (64 + 6)
static const char* const MENU_ITEMS[] = {
    u8"时钟", u8"日历", u8"天气", u8"计时", u8"秒表"
};

static void drawMenuScreen(void) {
    u8g2.clearBuffer();

    // 顶部栏：左侧 WiFi、中间「功能选择」、右侧电池（无分割线）
    drawWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    drawBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, getBatteryPercent());
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* menuTitle = u8"功能选择";
    int tw = u8g2.getUTF8Width(menuTitle);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, DATE_Y_TOP, menuTitle);

    const int iconSize = 16;
    const int slotW = 36;
    const int contentTotalW = slotW * 3;
    const int contentStartX = (SCREEN_W - contentTotalW) / 2;
    const int slotCx[3] = {
        contentStartX + slotW / 2,
        contentStartX + slotW + slotW / 2,
        contentStartX + slotW * 2 + slotW / 2
    };
    // 菜单区域在顶栏下方，选中框内图标/文字上下边距一致
    const int contentAreaTop = TOP_BAR_H;
    const int contentAreaH = SCREEN_H - contentAreaTop;
    const int gap = 18;
    const int fontH = 12;
    const int blockH = iconSize + gap + fontH;
    const int framePad = 4;   // 框内上下留白一致，图标不贴边
    const int needH = blockH + framePad * 2;
    int blockTop = contentAreaTop + (contentAreaH - needH) / 2;
    if (blockTop < contentAreaTop) blockTop = contentAreaTop;
    const int iconY = blockTop + framePad;
    const int iconBaseY = iconY + iconSize;
    const int labelY = iconBaseY + gap;
    const int frameW = slotW - 4;
    const int frameTop = blockTop;
    const int frameH = (labelY + fontH) - blockTop + framePad;
    const int frameHClip = (frameTop + frameH <= SCREEN_H) ? frameH : (SCREEN_H - frameTop);

    const int arrowCy = contentAreaTop + contentAreaH / 2;
    const int arrowW = 7;
    const int arrowHalfH = 5;
    const int leftTipX = contentStartX - 6;
    const int rightTipX = contentStartX + contentTotalW + 6;
    u8g2.drawTriangle(leftTipX, arrowCy,
                      leftTipX + arrowW, arrowCy - arrowHalfH,
                      leftTipX + arrowW, arrowCy + arrowHalfH);
    u8g2.drawTriangle(rightTipX, arrowCy,
                      rightTipX - arrowW, arrowCy - arrowHalfH,
                      rightTipX - arrowW, arrowCy + arrowHalfH);

    int indices[3] = {
        (s_menuIndex + 4) % 5,
        s_menuIndex,
        (s_menuIndex + 1) % 5
    };

    for (int i = 0; i < 3; i++) {
        int idx = indices[i];
        int cx = slotCx[i];
        int iconX = cx - iconSize / 2;
        int isCenter = (idx == s_menuIndex);

        if (isCenter) {
            u8g2.drawRBox(cx - frameW / 2, frameTop, frameW, frameHClip, 3);
            u8g2.setDrawColor(0);
        }
        u8g2.setBitmapMode(1);
        switch (idx) {
            case 0:
                u8g2.setFont(u8g2_font_open_iconic_app_2x_t);
                u8g2.drawGlyph(iconX, iconBaseY, OI_APP_CLOCK);
                break;
            case 1:
                u8g2.setFont(u8g2_font_open_iconic_app_2x_t);
                u8g2.drawGlyph(iconX, iconBaseY, OI_APP_CALENDAR);
                break;
            case 2:
                u8g2.setFont(u8g2_font_open_iconic_weather_2x_t);
                u8g2.drawGlyph(iconX, iconBaseY, OI_WEATHER_SUN);
                break;
            case 3:
                u8g2.setFont(u8g2_font_open_iconic_app_2x_t);
                u8g2.drawGlyph(iconX, iconBaseY, OI_APP_TIMER);
                break;
            case 4:
                u8g2.setFont(u8g2_font_open_iconic_play_2x_t);
                u8g2.drawGlyph(iconX, iconBaseY, OI_PLAY_RECORD);
                break;
        }
        u8g2.setBitmapMode(0);

        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        int lw = u8g2.getUTF8Width(MENU_ITEMS[idx]);
        u8g2.drawUTF8(cx - lw / 2, labelY, MENU_ITEMS[idx]);
        if (isCenter)
            u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();
}

// 日历：当月天数；闰年二月 29
static int daysInMonth(int year, int month) {
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int n = d[month - 1];
    if (month == 2 && (year % 4 == 0 && year % 100 != 0 || year % 400 == 0))
        n = 29;
    return n;
}

// 某年某月 1 号为周几（0=日 1=一 … 6=六）
static int firstWday(int year, int month) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    t.tm_hour = 12;
    time_t tt = mktime(&t);
    if (tt == (time_t)-1) return 0;
    localtime_r(&tt, &t);
    return t.tm_wday;
}

#define CAL_LEFT_W   101
#define CAL_RIGHT_W  (SCREEN_W - CAL_LEFT_W)
#define CAL_HEADER_H 12
#define CAL_ROW_H    10
#define CAL_ROWS     5
#define CAL_CELL_W   (CAL_LEFT_W / 7)

static void drawCalendarPage(int todayYear, int todayMonth, int todayDay) {
    u8g2.clearBuffer();

    const int first = firstWday(s_calYear, s_calMonth);
    const int days = daysInMonth(s_calYear, s_calMonth);

    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* weekdays[] = { u8"日", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六" };
    for (int c = 0; c < 7; c++) {
        int cw = u8g2.getUTF8Width(weekdays[c]);
        u8g2.drawUTF8(c * CAL_CELL_W + (CAL_CELL_W - cw) / 2, CAL_HEADER_H - 2, weekdays[c]);
    }

    u8g2.setFont(u8g2_font_6x10_tf);
    int cellIndex = 0;
    for (int day = 1; day <= days; day++) {
        int pos = first + (day - 1);
        if (pos >= CAL_ROWS * 7) break;
        int row = pos / 7;
        int col = pos % 7;
        int x = col * CAL_CELL_W;
        int y = CAL_HEADER_H + row * CAL_ROW_H + CAL_ROW_H - 2;
        int isToday = (s_calYear == todayYear && s_calMonth == todayMonth && day == todayDay);
        int cellY = CAL_HEADER_H + row * CAL_ROW_H;
        if (isToday) {
            int bx = x + 1, by = cellY + 1, bw = CAL_CELL_W - 2, bh = CAL_ROW_H - 2;
            u8g2.drawRBox(bx, by, bw, bh, 1);
            u8g2.setDrawColor(0);
        }
        char num[4];
        snprintf(num, sizeof(num), "%d", day);
        int nw = u8g2.getStrWidth(num);
        u8g2.drawStr(x + (CAL_CELL_W - nw) / 2, cellY + CAL_ROW_H - 2, num);
        if (isToday)
            u8g2.setDrawColor(1);
    }

    int rx = CAL_LEFT_W;
    int rw = CAL_RIGHT_W;
    u8g2.drawVLine(CAL_LEFT_W, 0, SCREEN_H);

    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const int RIGHT_LINE_H = 16;
    const int RIGHT_BLOCK_H = 4 * RIGHT_LINE_H;
    int startY = (SCREEN_H - RIGHT_BLOCK_H) / 2;
    if (startY < 0) startY = 0;
    int baseOff = 10;

    char yearNum[4], monthNum[4];
    snprintf(yearNum, sizeof(yearNum), "%02d", s_calYear % 100);
    snprintf(monthNum, sizeof(monthNum), "%02d", s_calMonth);
    const char* yearSuffix = u8"年";
    const char* monthSuffix = u8"月";
    int yNumW = u8g2.getStrWidth(yearNum);
    int ySufW = u8g2.getUTF8Width(yearSuffix);
    int mNumW = u8g2.getStrWidth(monthNum);
    int mSufW = u8g2.getUTF8Width(monthSuffix);

    int yearNumY = startY + baseOff;
    int yearSufY = startY + RIGHT_LINE_H + baseOff;
    int monthNumY = startY + 2 * RIGHT_LINE_H + baseOff;
    int monthSufY = startY + 3 * RIGHT_LINE_H + baseOff;

    int cx = rx + rw / 2;
    int startX = cx - (yNumW + 2) / 2;
    u8g2.drawRBox(startX, yearNumY - 10, yNumW + 2, 14, 1);
    u8g2.setDrawColor(0);
    u8g2.drawStr(startX + 1, yearNumY, yearNum);
    u8g2.setDrawColor(1);
    u8g2.drawUTF8(cx - ySufW / 2, yearSufY, yearSuffix);

    startX = cx - (mNumW + 2) / 2;
    u8g2.drawRBox(startX, monthNumY - 10, mNumW + 2, 14, 1);
    u8g2.setDrawColor(0);
    u8g2.drawStr(startX + 1, monthNumY, monthNum);
    u8g2.setDrawColor(1);
    u8g2.drawUTF8(cx - mSufW / 2, monthSufY, monthSuffix);

    u8g2.sendBuffer();
}

// 子页占位：标题 + 提示（日历/天气/计时/秒表）
static void drawPlaceholderPage(const char* title, const char* hint) {
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

// 时钟页：与原先 loop 中完全一致的绘制（顶部栏 + 时间位图）
static void drawClockPage(void) {
    struct tm t;
    if (!getLocalTime(&t)) return;

    u8g2.clearBuffer();
    drawWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);

    char date[16];
    snprintf(date, sizeof(date), "%04d/%02d/%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    u8g2.setFont(u8g2_font_7x13_tf);
    int dateW = u8g2.getUTF8Width(date);
    u8g2.drawStr((SCREEN_W - dateW) / 2, DATE_Y_TOP, date);

    drawBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, getBatteryPercent());
    drawTime(t.tm_hour, t.tm_min, t.tm_sec);
    u8g2.sendBuffer();
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Wire.begin(I2C_SDA, I2C_SCL);
    u8g2.setBusClock(400000);
    u8g2.begin();

    drawBootScreen(true, true, u8"正在连接 WiFi", u8"请稍候...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int w = 0;
    while (WiFi.status() != WL_CONNECTED && w < 30) {
        delay(500);
        w++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        drawBootScreen(true, false, u8"WiFi 连接失败", u8"请检查 SSID 与密码");
        Serial.println("WiFi connect failed");
        for (;;) delay(1000);
    }

    drawNtpBootScreen(u8"正在同步时间...", u8"NTP 服务器");

    if (!syncNtp()) {
        u8g2.clearBuffer();
        int cx = SCREEN_W / 2;
        const int clockY = 2;
        const int clockR = 10;
        u8g2.drawCircle(cx, clockY + clockR, clockR);
        u8g2.drawLine(cx - 6, clockY + 4, cx + 6, clockY + 16);
        u8g2.drawLine(cx - 6, clockY + 16, cx + 6, clockY + 4);
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        const char* t = u8"时间同步失败";
        u8g2.drawUTF8((SCREEN_W - u8g2.getUTF8Width(t)) / 2, clockY + clockR * 2 + 22, t);
        const char* s = u8"请稍后重试";
        u8g2.drawUTF8((SCREEN_W - u8g2.getUTF8Width(s)) / 2, clockY + clockR * 2 + 38, s);
        u8g2.sendBuffer();
        Serial.println("NTP sync failed");
        for (;;) delay(1000);
    }

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_ADC_PIN, INPUT);

    buttonsInit();
    Serial.println("WiFi & NTP OK");
}

void loop() {
    buttonsUpdate();

    ButtonEvent left   = buttonsGetLeft();
    ButtonEvent center = buttonsGetCenter();
    ButtonEvent right  = buttonsGetRight();

    if (s_state == STATE_MENU) {
        if (left == BTN_CLICK || left == BTN_DOUBLE_CLICK) {
            s_menuIndex = (s_menuIndex + 4) % 5;
        }
        if (right == BTN_CLICK || right == BTN_DOUBLE_CLICK) {
            s_menuIndex = (s_menuIndex + 1) % 5;
        }
        if (center == BTN_CLICK || center == BTN_DOUBLE_CLICK) {
            switch (s_menuIndex) {
                case 0: s_state = STATE_CLOCK;    break;
                case 1: {
                    s_state = STATE_CALENDAR;
                    struct tm t;
                    if (getLocalTime(&t)) {
                        s_calYear = t.tm_year + 1900;
                        s_calMonth = t.tm_mon + 1;
                    }
                    break;
                }
                case 2: s_state = STATE_WEATHER;  break;
                case 3: s_state = STATE_TIMER;    break;
                case 4: s_state = STATE_STOPWATCH; break;
            }
        }
        drawMenuScreen();
        delay(80);
        return;
    }

    // 子页面：中键长按返回主菜单
    if (center == BTN_LONG_PRESS) {
        s_state = STATE_MENU;
        drawMenuScreen();
        delay(80);
        return;
    }

    if (s_state == STATE_CALENDAR) {
        if (left == BTN_CLICK || left == BTN_DOUBLE_CLICK) {
            s_calMonth--;
            if (s_calMonth < 1) { s_calMonth = 12; s_calYear--; }
        }
        if (right == BTN_CLICK || right == BTN_DOUBLE_CLICK) {
            s_calMonth++;
            if (s_calMonth > 12) { s_calMonth = 1; s_calYear++; }
        }
        if (center == BTN_DOUBLE_CLICK) {
            struct tm t;
            if (getLocalTime(&t)) {
                s_calYear = t.tm_year + 1900;
                s_calMonth = t.tm_mon + 1;
            }
        }
    }

    switch (s_state) {
        case STATE_CLOCK:
            drawClockPage();
            delay(500);
            break;
        case STATE_CALENDAR: {
            struct tm t;
            int ty = 2026, tmon = 1, td = 1;
            if (getLocalTime(&t)) {
                ty = t.tm_year + 1900;
                tmon = t.tm_mon + 1;
                td = t.tm_mday;
            }
            drawCalendarPage(ty, tmon, td);
            delay(80);
            break;
        }
        case STATE_WEATHER:
            drawPlaceholderPage(u8"天气", u8"敬请期待");
            delay(200);
            break;
        case STATE_TIMER:
            drawPlaceholderPage(u8"计时", u8"敬请期待");
            delay(200);
            break;
        case STATE_STOPWATCH:
            drawPlaceholderPage(u8"秒表", u8"敬请期待");
            delay(200);
            break;
        default:
            s_state = STATE_MENU;
            delay(80);
            break;
    }
}
