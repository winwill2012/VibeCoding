/**
 * @file app_state.cpp
 * @brief 应用状态与各页面共享的全局变量定义与初始化
 */
#include "app_state.h"

AppState g_state = STATE_MENU;
int g_menuIndex = 0;
int g_calYear = 2026;
int g_calMonth = 1;

uint32_t g_stopwatchAccumulatedMs = 0;
uint32_t g_stopwatchRunStartMillis = 0;

bool g_ntpSynced = false;

uint8_t g_timerDigits[4] = { 0, 0, 0, 0 };
int g_timerDigitPos = 0;
bool g_timerRunning = false;
uint32_t g_timerEndMillis = 0;

char g_weatherLocation[32] = "kunming";
char g_weatherCityName[16] = u8"昆明";
char g_weatherTemp[8] = "--";
char g_weatherText[8] = u8"晴";
int g_weatherIconCode = 69;
uint32_t g_weatherLastFetch = 0;

void appStateInit(void) {
    g_state = STATE_MENU;
    g_menuIndex = 0;
    g_ntpSynced = false;
    g_stopwatchAccumulatedMs = 0;
    g_stopwatchRunStartMillis = 0;
    g_timerDigitPos = 0;
    g_timerRunning = false;
}
