/**
 * @file menu_screen.cpp
 * @brief 主菜单：Open Iconic 图标 + 文字，左右箭头，顶栏 WiFi/电池
 */
#include "menu_screen.h"
#include "display.h"
#include "app_state.h"
#include <WiFi.h>

#define OI_APP_CLOCK     (64 + 5)
#define OI_APP_CALENDAR  (64 + 2)
#define OI_APP_TIMER     (64 + 8)
#define OI_WEATHER_SUN   (64 + 5)
#define OI_APP_STOPWATCH (64 + 7)

static const char* const MENU_ITEMS[] = {
    u8"时钟", u8"日历", u8"天气", u8"计时", u8"秒表"
};

void menuScreenDraw(void) {
    u8g2.clearBuffer();
    displayTopBarBackground();
    displayWiFiIcon(WIFI_ICON_X, WIFI_ICON_Y, WiFi.status() == WL_CONNECTED);
    displayBatteryIcon(BATTERY_ICON_X, BATTERY_ICON_Y, displayGetBatteryPercent());
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
    const int contentAreaTop = TOP_BAR_H;
    const int contentAreaH = SCREEN_H - contentAreaTop;
    const int gap = 18;
    const int fontH = 12;
    const int blockH = iconSize + gap + fontH;
    const int framePad = 4;
    const int needH = blockH + framePad * 2;
    const int centerOffset = 8;
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
        (g_menuIndex + 4) % 5,
        g_menuIndex,
        (g_menuIndex + 1) % 5
    };

    for (int i = 0; i < 3; i++) {
        int idx = indices[i];
        int cx = slotCx[i];
        int iconX = cx - iconSize / 2;
        int isCenter = (idx == g_menuIndex);

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
        u8g2.drawUTF8(cx - u8g2.getUTF8Width(MENU_ITEMS[idx]) / 2, labelY, MENU_ITEMS[idx]);
        if (isCenter)
            u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();
}
