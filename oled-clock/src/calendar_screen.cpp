/**
 * @file calendar_screen.cpp
 * @brief 日历：月历网格 + 右侧年月显示
 */
#include "calendar_screen.h"
#include "display.h"
#include "app_state.h"
#include <time.h>

#define CAL_LEFT_W   101
#define CAL_RIGHT_W  (SCREEN_W - CAL_LEFT_W)
#define CAL_HEADER_H 12
#define CAL_ROW_H    10
#define CAL_ROWS     5
#define CAL_CELL_W   (CAL_LEFT_W / 7)

static int daysInMonth(int year, int month) {
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int n = d[month - 1];
    if (month == 2 && (year % 4 == 0 && year % 100 != 0 || year % 400 == 0))
        n = 29;
    return n;
}

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

void calendarScreenDraw(int todayYear, int todayMonth, int todayDay) {
    u8g2.clearBuffer();

    const int first = firstWday(g_calYear, g_calMonth);
    const int days = daysInMonth(g_calYear, g_calMonth);

    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const char* weekdays[] = { u8"日", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六" };
    for (int c = 0; c < 7; c++) {
        int cw = u8g2.getUTF8Width(weekdays[c]);
        u8g2.drawUTF8(c * CAL_CELL_W + (CAL_CELL_W - cw) / 2, CAL_HEADER_H - 2, weekdays[c]);
    }

    u8g2.setFont(u8g2_font_6x10_tf);
    for (int day = 1; day <= days; day++) {
        int pos = first + (day - 1);
        if (pos >= CAL_ROWS * 7) break;
        int row = pos / 7;
        int col = pos % 7;
        int x = col * CAL_CELL_W;
        int cellY = CAL_HEADER_H + row * CAL_ROW_H;
        int y = cellY + CAL_ROW_H - 2;
        int isToday = (g_calYear == todayYear && g_calMonth == todayMonth && day == todayDay);
        if (isToday) {
            int bx = x + 1, by = cellY + 1, bw = CAL_CELL_W - 2, bh = CAL_ROW_H - 2;
            u8g2.drawRBox(bx, by, bw, bh, 1);
            u8g2.setDrawColor(0);
        }
        char num[4];
        snprintf(num, sizeof(num), "%d", day);
        int nw = u8g2.getStrWidth(num);
        u8g2.drawStr(x + (CAL_CELL_W - nw) / 2, y, num);
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
    snprintf(yearNum, sizeof(yearNum), "%02d", g_calYear % 100);
    snprintf(monthNum, sizeof(monthNum), "%02d", g_calMonth);
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
