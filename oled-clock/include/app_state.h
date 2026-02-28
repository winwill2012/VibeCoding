/**
 * @file app_state.h
 * @brief 应用状态与各页面共享的全局变量
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>

enum AppState {
    STATE_MENU,
    STATE_CLOCK,
    STATE_CALENDAR,
    STATE_WEATHER,
    STATE_TIMER,
    STATE_STOPWATCH
};

// 主菜单与日历
extern AppState g_state;
extern int g_menuIndex;
extern int g_calYear;
extern int g_calMonth;

// 秒表
extern uint32_t g_stopwatchAccumulatedMs;
extern uint32_t g_stopwatchRunStartMillis;

// NTP
extern bool g_ntpSynced;

// 倒计时
extern uint8_t g_timerDigits[4];
extern int g_timerDigitPos;
extern bool g_timerRunning;
extern uint32_t g_timerEndMillis;

// 天气（城市 ID、显示缓存）
extern char g_weatherLocation[32];
extern char g_weatherCityName[16];
extern char g_weatherTemp[8];
extern char g_weatherText[8];
extern int g_weatherIconCode;
extern uint32_t g_weatherLastFetch;

void appStateInit(void);

#endif
