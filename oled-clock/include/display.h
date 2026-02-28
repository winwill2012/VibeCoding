/**
 * @file display.h
 * @brief OLED 显示：初始化、顶栏、电池、时间位图、通用绘制
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <U8g2lib.h>

#define I2C_SDA  21
#define I2C_SCL  22

#define SCREEN_W     128
#define SCREEN_H     64
#define TOP_BAR_H     14
#define DATE_Y_TOP    11
#define TIME_Y_TOP    24
#define MARGIN_LEFT   2
#define WIFI_ICON_X   2
#define WIFI_ICON_Y   0
#define BATTERY_ICON_W  18
#define BATTERY_ICON_H  10
#define BATTERY_ICON_X  (SCREEN_W - BATTERY_ICON_W - 2)
#define BATTERY_ICON_Y  3

#define BIG_W      24
#define BIG_H      32
#define MINI_W     8
#define MINI_H     12
#define DOT_W      8
#define DOT_H      32

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

void displayInit(void);
void displayTopBarBackground(void);
void displayWiFiIcon(int x, int y, bool connected);
void displayBatteryIcon(int x, int y, int percent);
int displayGetBatteryPercent(void);

void displayDrawTime(int hour, int minute, int second);
void displayDrawBigDigit(int x, int y, int d);
void displayDrawMiniDigit(int x, int y, int d);
void displayDrawDot(int x, int y);

void displayBootScreen(bool showWifiIcon, bool wifiOk, const char* title, const char* subtitle);
void displayNtpBootScreen(const char* title, int dotCount);
void displayPlaceholderPage(const char* title, const char* hint);

#endif
