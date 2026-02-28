/**
 * @file main.cpp
 * @brief ESP32 + SH1106 OLED：主菜单 + 时钟/日历/天气/计时/秒表，三键操作
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>

#include "bitmap.h"
#include "wifi_config.h"
#include "buttons.h"

// 延时期间定期轮询按键，提高单击/双击识别率
static void delayWithButtonPoll(uint32_t totalMs) {
    const uint32_t stepMs = 14;
    int steps = (int)(totalMs / stepMs);
    if (steps < 1) steps = 1;
    for (int i = 0; i < steps; i++) {
        buttonsUpdate();
        delay(stepMs);
    }
}

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

// 秒表：累计毫秒数；当前段开始时刻（0 表示已暂停）
static uint32_t s_stopwatchAccumulatedMs = 0;
static uint32_t s_stopwatchRunStartMillis = 0;

// NTP 是否已同步（进入时钟时再同步）
static bool s_ntpSynced = false;

// 倒计时：四位数字 [分十,分个,秒十,秒个]，当前选位 0..3，是否运行，结束时间戳
#define BUZZER_PIN  23
#define TIMER_LEDC_CHANNEL  0
static uint8_t s_timerDigits[4] = { 0, 0, 0, 0 };
static int s_timerDigitPos = 0;
static bool s_timerRunning = false;
static uint32_t s_timerEndMillis = 0;

// 倒计时结束：响铃 10 秒，带节奏（叮-叮 … 叮-叮 …）
static void playTimerBeep(void) {
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

// SH1106 128x64, 硬件 I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

#define I2C_SDA  21
#define I2C_SCL  22

// 电池 ADC：GPIO34，板端已用两个相同电阻分压（接线方向为 raw 高=电量低、raw 低=电量高，故空/满对调）
#define BATTERY_ADC_PIN      34
#define ADC_RAW_EMPTY        2604   // 显示 0% 时的 ADC 值（对应实际空电时的 raw）
#define ADC_RAW_FULL         1864   // 显示 100% 时的 ADC 值（对应实际满电时的 raw）

// 心知天气 API（默认昆明）
#define SENIVERE_API_KEY  "SHOEXKwNcHrAxuw09"
#define WEATHER_LOCATION_DEFAULT  "kunming"
#define WEATHER_CACHE_MS  (10 * 60 * 1000)

static char s_weatherLocation[32] = "kunming";   // 运行时城市 ID，可由 Web 修改并持久化
static char s_weatherCityName[16] = u8"昆明";     // 显示用城市名，由 API 返回或默认
static char s_weatherTemp[8] = "--";
static char s_weatherText[8] = u8"晴";
static int s_weatherIconCode = 69;
static uint32_t s_weatherLastFetch = 0;

// Web 配置天气城市（手机连同一 WiFi 访问 ESP32 IP）
#define PREF_NAMESPACE  "vibe"
#define PREF_KEY_LOC   "wloc"
static WebServer webServer(80);
static Preferences preferences;
static void handleWebRoot(void);

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
#define WIFI_ICON_Y   0
#define BATTERY_ICON_W  18
#define BATTERY_ICON_H  10
#define BATTERY_ICON_X  (SCREEN_W - BATTERY_ICON_W - 2)
#define BATTERY_ICON_Y  3

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
    if ((second & 1) == 0)
        u8g2.drawBitmap(x, TIME_Y_TOP, DOT_BYTES, DOT_H, IMAGE_DOT);
    x += DOT_W;
    drawBigDigit(x, TIME_Y_TOP, m1);  x += BIG_W;
    drawBigDigit(x, TIME_Y_TOP, m2);  x += BIG_W;
    x += 4;
    drawMiniDigit(x, MINI_Y, s1);  x += MINI_W;
    drawMiniDigit(x, MINI_Y, s2);
}

// 顶部状态栏反色：整条黑底，之后绘制的 WiFi/标题/电池为白
static void drawTopBarBackground(void) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 0, SCREEN_W, TOP_BAR_H);
    u8g2.setDrawColor(1);
}

// 网络标识：扇形 WiFi 符号（开口朝上，由内到外三层弧），未连接时框+叉
static void drawWiFiIcon(int x, int y, bool connected) {
    const int cx = x + 7;
    const int tipY = y + 11;   // 扇形顶点（开口朝上）
    const int maxW = 14;
    const int maxH = 12;
    if (!connected) {
        u8g2.drawFrame(x, y, maxW, maxH);
        u8g2.drawLine(x + 2, y + maxH - 1, x + 12, y + 2);
        u8g2.drawLine(x + 2, y + 2, x + 12, y + maxH - 1);
        return;
    }
    const int radii[] = { 2, 5, 8 };
    const int degStart = 145;  // 扇形左边界（度）
    const int degEnd   = 35;   // 扇形右边界，约 110° 扇形
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

// 电池图标：右侧顶部，外框+按电量填充（内填充与外框留 2px 间距，小图标上更明显）
static void drawBatteryIcon(int x, int y, int percent) {
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

// 同步 NTP，等待期间刷新界面（dotCount 动画），intervalMs 越短同步越快
static bool syncNtpWithUi(void (*drawNtp)(const char*, int), int maxTries, int intervalMs) {
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

// Open Iconic 时钟图标（与主菜单时钟一致）
#define OI_CLOCK  (64 + 5)

// NTP 同步页：大号时钟图标 +「正在同步时间」+ 点点动画，整体垂直水平居中，无副标题
#define NTP_ICON_SIZE  32
#define NTP_ICON_GAP   12
#define NTP_TEXT_H     12

static void drawNtpBootScreen(const char* title, int dotCount) {
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

// 主菜单：Open Iconic 图标 + 文字，一屏 3 项，左右小箭头，整体居中
#define OI_APP_CLOCK     (64 + 5)
#define OI_APP_CALENDAR  (64 + 2)
#define OI_APP_TIMER     (64 + 8)
#define OI_WEATHER_SUN   (64 + 5)
#define OI_APP_STOPWATCH (64 + 7)
static const char* const MENU_ITEMS[] = {
    u8"时钟", u8"日历", u8"天气", u8"计时", u8"秒表"
};

static void drawMenuScreen(void) {
    u8g2.clearBuffer();
    drawTopBarBackground();
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
    // 菜单区域在顶栏下方，选中框内图标/文字上下边距一致，整体垂直居中并略下移靠向屏幕中心
    const int contentAreaTop = TOP_BAR_H;
    const int contentAreaH = SCREEN_H - contentAreaTop;
    const int gap = 18;
    const int fontH = 12;
    const int blockH = iconSize + gap + fontH;
    const int framePad = 4;   // 框内上下留白一致，图标不贴边
    const int needH = blockH + framePad * 2;
    const int centerOffset = 8;   // 整块下移几像素，图标/文字更靠近中心
    int blockTop = contentAreaTop + (contentAreaH - needH) / 2 + centerOffset;
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
                u8g2.setFont(u8g2_font_open_iconic_app_2x_t);
                u8g2.drawGlyph(iconX, iconBaseY, OI_APP_STOPWATCH);
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

// 心知天气：根据 API 返回的 text 映射为两字显示 + Open Iconic 图标码（64=云 65=云日 66=雪 67=雨 69=晴）
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
    url += s_weatherLocation;
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
            if (end > start && end - start < (int)sizeof(s_weatherCityName)) {
                body.substring(start, end).toCharArray(s_weatherCityName, sizeof(s_weatherCityName));
                s_weatherCityName[sizeof(s_weatherCityName) - 1] = '\0';
            }
        }
    }
    if (ti >= 0) {
        int start = ti + 15;  // "\"temperature\":\"" 长度为 15，从温度值首字符开始
        int end = body.indexOf('"', start);
        if (end > start && end - start < 7) {
            body.substring(start, end).toCharArray(s_weatherTemp, sizeof(s_weatherTemp));
        }
    }
    if (tei >= 0) {
        int start = tei + 8;
        int end = body.indexOf('"', start);
        if (end > start) {
            String text = body.substring(start, end);
            char two[8];
            mapWeatherToDisplay(text.c_str(), two, &s_weatherIconCode);
            strncpy(s_weatherText, two, sizeof(s_weatherText) - 1);
            s_weatherText[sizeof(s_weatherText) - 1] = '\0';
        }
    }
    s_weatherLastFetch = millis();
    return true;
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

// 天气页：左半屏图标，右半屏城市(反色)+天气+温度，竖线分隔，整体居中
#define WEATHER_LEFT_W     64
#define WEATHER_DIVIDER_X  66   // 竖线稍右移，与左侧 IP 反色条不重叠
#define WEATHER_ICON_SIZE  32
#define WEATHER_RIGHT_CX   96
#define WEATHER_LINE_H     14
#define WEATHER_CONTENT_TOP (TOP_BAR_H + 10)

// 获取天气前：Open Iconic 大图标 + 文案，整体居中、大气
#define WEATHER_LOADING_ICON_SIZE  32
#define WEATHER_LOADING_ICON_CODE  69
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

static void drawWeatherPage(void) {
    bool needFetch = (s_weatherLastFetch == 0 || (uint32_t)(millis() - s_weatherLastFetch) > WEATHER_CACHE_MS);
    if (needFetch && WiFi.status() == WL_CONNECTED) {
        drawWeatherLoadingScreen();
        fetchWeather();
    }
    u8g2.clearBuffer();
    drawTopBarBackground();
    drawWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* title = u8"实时天气";
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, DATE_Y_TOP, title);
    drawBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, getBatteryPercent());

    int contentTop = WEATHER_CONTENT_TOP;
    int contentH = SCREEN_H - contentTop;
    int leftCenterX = WEATHER_LEFT_W / 2;
    // 天气图标上移，为底部 IP 反色条留出空间
    const int iconY = contentTop - 6;
    const int iconBottom = iconY + WEATHER_ICON_SIZE;

    u8g2.drawVLine(WEATHER_DIVIDER_X, contentTop, contentH);
    u8g2.setBitmapMode(1);
    u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
    u8g2.drawGlyph(leftCenterX - WEATHER_ICON_SIZE / 2, iconBottom, s_weatherIconCode);
    u8g2.setBitmapMode(0);

    // 左侧底部 IP 反色条：整条底条 + 白字
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
        if (ipX < 2) ipX = 2;
        u8g2.setDrawColor(0);
        u8g2.drawStr(ipX, ipBaseline, ipStr.c_str());
        u8g2.setDrawColor(1);
    }
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    int cityW = u8g2.getUTF8Width(s_weatherCityName);
    int cityX = WEATHER_RIGHT_CX - cityW / 2;
    int cityY = contentTop + WEATHER_LINE_H - 2;
    u8g2.drawRBox(cityX - 2, cityY - 11, cityW + 4, 14, 2);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(cityX, cityY, s_weatherCityName);
    u8g2.setDrawColor(1);

    // 天气/温度行与城市名底条留出足够间距（再下移 4px，与城市更疏离）
    int ry = contentTop + WEATHER_LINE_H + 18;
    int textW = u8g2.getUTF8Width(s_weatherText);
    int gap = 6;
    const char* celsiusStr = u8"℃";
    int celsiusW = u8g2.getUTF8Width(celsiusStr);
    u8g2.setFont(u8g2_font_7x13B_tf);
    int tempNumW = u8g2.getStrWidth(s_weatherTemp);
    int totalW = textW + gap + tempNumW + celsiusW;
    int lineX = WEATHER_RIGHT_CX - totalW / 2;
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.drawUTF8(lineX, ry, s_weatherText);
    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.drawStr(lineX + textW + gap, ry, s_weatherTemp);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.drawUTF8(lineX + textW + gap + tempNumW, ry, celsiusStr);
    u8g2.sendBuffer();
}

// 时钟页：与原先 loop 中完全一致的绘制（顶部栏 + 时间位图）
static void drawClockPage(void) {
    struct tm t;
    if (!getLocalTime(&t)) return;

    u8g2.clearBuffer();
    drawTopBarBackground();
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

// 秒表页：顶栏 WiFi + 标题「秒表」+ 电池；中间三位大数字（秒）+ 三位小数字（毫秒）
#define STOPWATCH_TIME_Y   TIME_Y_TOP
#define STOPWATCH_MINI_Y   (STOPWATCH_TIME_Y + BIG_H - MINI_H)
#define STOPWATCH_TOTAL_W  (3 * BIG_W + 3 * MINI_W)
#define STOPWATCH_START_X   ((SCREEN_W - STOPWATCH_TOTAL_W) / 2)

static void drawStopwatchPage(void) {
    uint32_t totalMs = s_stopwatchAccumulatedMs;
    if (s_stopwatchRunStartMillis != 0)
        totalMs += (uint32_t)(millis() - s_stopwatchRunStartMillis);
    uint32_t sec = totalMs / 1000;
    uint32_t ms = totalMs % 1000;
    if (sec > 999) sec = 999;
    int s1 = (int)(sec / 100), s2 = (int)((sec / 10) % 10), s3 = (int)(sec % 10);
    int m1 = (int)(ms / 100), m2 = (int)((ms / 10) % 10), m3 = (int)(ms % 10);

    u8g2.clearBuffer();
    drawTopBarBackground();
    drawWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* title = u8"秒表";
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, DATE_Y_TOP, title);
    drawBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, getBatteryPercent());

    int x = STOPWATCH_START_X;
    drawBigDigit(x, STOPWATCH_TIME_Y, s1);  x += BIG_W;
    drawBigDigit(x, STOPWATCH_TIME_Y, s2);  x += BIG_W;
    drawBigDigit(x, STOPWATCH_TIME_Y, s3);  x += BIG_W;
    int miniBlockCenterX = x + (3 * MINI_W) / 2;
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* msLabel = u8"毫秒";
    int labelW = u8g2.getUTF8Width(msLabel);
    int labelY = STOPWATCH_TIME_Y + u8g2.getAscent() + 6;
    u8g2.drawUTF8(miniBlockCenterX - labelW / 2, labelY, msLabel);
    drawMiniDigit(x, STOPWATCH_MINI_Y, m1);  x += MINI_W;
    drawMiniDigit(x, STOPWATCH_MINI_Y, m2);  x += MINI_W;
    drawMiniDigit(x, STOPWATCH_MINI_Y, m3);

    u8g2.sendBuffer();
}

// 倒计时页：顶栏 WiFi +「倒计时」+ 电池；大号位图 MM:SS（中间用 bitmap.h 的 IMAGE_DOT 两点）；设置时三角指示当前位
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

static void drawTimerPage(void) {
    u8g2.clearBuffer();
    drawTopBarBackground();
    drawWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* title = u8"倒计时";
    int tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((SCREEN_W - tw) / 2, DATE_Y_TOP, title);
    drawBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, getBatteryPercent());

    int m1 = s_timerDigits[0], m2 = s_timerDigits[1];
    int s1 = s_timerDigits[2], s2 = s_timerDigits[3];
    if (s_timerRunning) {
        uint32_t remain = (s_timerEndMillis > millis()) ? (s_timerEndMillis - millis()) : 0;
        uint32_t sec = remain / 1000;
        m1 = (int)(sec / 60) / 10;
        m2 = (int)(sec / 60) % 10;
        s1 = (int)(sec % 60) / 10;
        s2 = (int)(sec % 60) % 10;
    }

    int x = TIMER_START_X;
    drawBigDigit(x, TIMER_TIME_Y, m1);  x += BIG_W;
    drawBigDigit(x, TIMER_TIME_Y, m2);  x += BIG_W;
    u8g2.drawBitmap(x, TIMER_TIME_Y, DOT_BYTES, DOT_H, IMAGE_DOT);
    x += TIMER_COLON_W;
    drawBigDigit(x, TIMER_TIME_Y, s1);  x += BIG_W;
    drawBigDigit(x, TIMER_TIME_Y, s2);

    if (!s_timerRunning) {
        int cx = timerDigitCenterX(s_timerDigitPos);
        u8g2.drawTriangle(cx - 5, TIMER_TRI_BASE_Y, cx, TIMER_TRI_TIP_Y, cx + 5, TIMER_TRI_BASE_Y);
    }

    u8g2.sendBuffer();
}

// Web 配置页：显示当前城市并提交修改（心知 API 城市 ID，如 kunming / beijing）
static void handleWebRoot(void) {
    if (webServer.method() == HTTP_POST) {
        if (webServer.hasArg("location")) {
            String loc = webServer.arg("location");
            loc.trim();
            if (loc.length() > 0 && loc.length() < sizeof(s_weatherLocation)) {
                loc.toCharArray(s_weatherLocation, sizeof(s_weatherLocation));
                s_weatherLocation[sizeof(s_weatherLocation) - 1] = '\0';
                preferences.begin(PREF_NAMESPACE, false);
                preferences.putString(PREF_KEY_LOC, s_weatherLocation);
                preferences.end();
                s_weatherLastFetch = 0;  // 下次进天气页会重新拉取
            }
        }
        webServer.sendHeader("Location", "/");
        webServer.send(302, "text/plain", "");
        return;
    }
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>天气城市</title></head><body style=\"font-family:sans-serif;padding:1em;\">";
    html += "<h2>天气城市设置</h2><p>心知天气城市 ID（如 kunming、beijing、shanghai）</p>";
    html += "<form method=\"post\" action=\"/\">";
    html += "<input type=\"text\" name=\"location\" value=\"" + String(s_weatherLocation) + "\" maxlength=\"31\" size=\"20\"> ";
    html += "<button type=\"submit\">保存</button></form>";
    html += "<p><small>保存后进入设备「天气」页将自动拉取新城市数据。</small></p></body></html>";
    webServer.send(200, "text/html; charset=utf-8", html);
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

    preferences.begin(PREF_NAMESPACE, true);
    String saved = preferences.getString(PREF_KEY_LOC, WEATHER_LOCATION_DEFAULT);
    preferences.end();
    if (saved.length() > 0 && saved.length() < sizeof(s_weatherLocation)) {
        saved.toCharArray(s_weatherLocation, sizeof(s_weatherLocation));
        s_weatherLocation[sizeof(s_weatherLocation) - 1] = '\0';
    }

    webServer.on("/", HTTP_GET, handleWebRoot);
    webServer.on("/", HTTP_POST, handleWebRoot);
    webServer.begin();
    Serial.print("Web 配置: http://");
    Serial.println(WiFi.localIP());

    // 启动时同步 NTP，避免首次进日历/时钟时卡在「正在同步时间」几秒
    syncNtpWithUi(drawNtpBootScreen, 20, 80);
    s_ntpSynced = true;

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    ledcAttachPin(BUZZER_PIN, TIMER_LEDC_CHANNEL);

    buttonsInit();
    Serial.println("WiFi & NTP OK");
}

void loop() {
    webServer.handleClient();
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
                case 3:
                    s_state = STATE_TIMER;
                    s_timerRunning = false;
                    s_timerDigitPos = 0;
                    break;
                case 4: s_state = STATE_STOPWATCH; break;
            }
            if (s_state != STATE_MENU) {
                delayWithButtonPoll(80);
                return;
            }
        }
        drawMenuScreen();
        delayWithButtonPoll(80);
        return;
    }

    // 子页面：中键长按返回主菜单
    if (center == BTN_LONG_PRESS) {
        s_state = STATE_MENU;
        drawMenuScreen();
        delayWithButtonPoll(80);
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

    if (s_state == STATE_STOPWATCH) {
        if (center == BTN_DOUBLE_CLICK) {
            s_stopwatchAccumulatedMs = 0;
            s_stopwatchRunStartMillis = 0;
        } else if (center == BTN_CLICK) {
            if (s_stopwatchRunStartMillis != 0) {
                s_stopwatchAccumulatedMs += (uint32_t)(millis() - s_stopwatchRunStartMillis);
                s_stopwatchRunStartMillis = 0;
            } else {
                s_stopwatchRunStartMillis = millis();
            }
        }
    }

    if (s_state == STATE_TIMER) {
        if (s_timerRunning) {
            if (millis() >= s_timerEndMillis) {
                s_timerRunning = false;
                playTimerBeep();
            }
        } else {
            if (left == BTN_CLICK || left == BTN_DOUBLE_CLICK) {
                s_timerDigitPos = (s_timerDigitPos + 3) % 4;
            }
            if (right == BTN_CLICK || right == BTN_DOUBLE_CLICK) {
                s_timerDigitPos = (s_timerDigitPos + 1) % 4;
            }
            if (center == BTN_CLICK) {
                s_timerDigits[s_timerDigitPos] = (s_timerDigits[s_timerDigitPos] + 1) % 10;
            }
            if (center == BTN_DOUBLE_CLICK) {
                uint32_t totalSec = (s_timerDigits[0] * 10U + s_timerDigits[1]) * 60U
                    + (s_timerDigits[2] * 10U + s_timerDigits[3]);
                if (totalSec > 0) {
                    s_timerEndMillis = millis() + totalSec * 1000;
                    s_timerRunning = true;
                }
            }
        }
    }

    switch (s_state) {
        case STATE_CLOCK:
            if (!s_ntpSynced) {
                if (!syncNtpWithUi(drawNtpBootScreen, 20, 80))
                    Serial.println("NTP sync failed");
                s_ntpSynced = true;
            }
            drawClockPage();
            delayWithButtonPoll(100);
            break;
        case STATE_CALENDAR: {
            if (!s_ntpSynced) {
                if (!syncNtpWithUi(drawNtpBootScreen, 20, 80))
                    Serial.println("NTP sync failed");
                s_ntpSynced = true;
                struct tm t;
                if (getLocalTime(&t)) {
                    s_calYear = t.tm_year + 1900;
                    s_calMonth = t.tm_mon + 1;
                }
            }
            struct tm t;
            int ty = 2026, tmon = 1, td = 1;
            if (getLocalTime(&t)) {
                ty = t.tm_year + 1900;
                tmon = t.tm_mon + 1;
                td = t.tm_mday;
            }
            drawCalendarPage(ty, tmon, td);
            delayWithButtonPoll(80);
            break;
        }
        case STATE_WEATHER:
            drawWeatherPage();
            delayWithButtonPoll(200);
            break;
        case STATE_TIMER:
            drawTimerPage();
            delayWithButtonPoll(s_timerRunning ? 50 : 80);
            break;
        case STATE_STOPWATCH:
            drawStopwatchPage();
            delayWithButtonPoll(50);
            break;
        default:
            s_state = STATE_MENU;
            delayWithButtonPoll(80);
            break;
    }
}
