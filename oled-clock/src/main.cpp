/**
 * @file main.cpp
 * @brief ESP32 + SH1106 OLED：主入口，智能配网 / WiFi / NTP / 按键与状态机
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include "buttons.h"
#include "app_state.h"
#include "display.h"
#include "menu_screen.h"
#include "clock_screen.h"
#include "calendar_screen.h"
#include "weather_screen.h"
#include "timer_screen.h"
#include "stopwatch_screen.h"
#include "web_config.h"

#define BUZZER_PIN  23
#define TIMER_LEDC_CHANNEL  0
#define BATTERY_ADC_PIN     34

static void delayWithButtonPoll(uint32_t totalMs) {
    const uint32_t stepMs = 14;
    int steps = (int)(totalMs / stepMs);
    if (steps < 1) steps = 1;
    for (int i = 0; i < steps; i++) {
        buttonsUpdate();
        delay(stepMs);
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    displayInit();
    displayBootScreen(true, true, u8"正在连接 WiFi", u8"请稍候...");

    WiFiManager wm;
    wm.setConfigPortalTimeout(120);   // 配网页超时 2 分钟
    wm.setConnectTimeout(30);         // 连接路由器超时 30 秒
    wm.setMinimumSignalQuality(10);   // 信号强度至少 10%
    // 若已有保存的 WiFi 则自动连接；否则或连接失败则启动配网 AP「OLEDClock」
    bool connected = wm.autoConnect("OLEDClock");
    if (!connected) {
        displayBootScreen(true, false, u8"WiFi 连接失败", u8"请用手机连 OLEDClock 配网");
        Serial.println("WiFi: connect failed, restart to reconfigure");
        for (;;) delay(1000);
    }

    webConfigBegin();
    Serial.print("Web 配置: http://");
    Serial.println(WiFi.localIP());

    clockScreenSyncNtp(displayNtpBootScreen, 20, 80);
    g_ntpSynced = true;

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    ledcAttachPin(BUZZER_PIN, TIMER_LEDC_CHANNEL);

    buttonsInit();
    Serial.println("WiFi & NTP OK");
}

void loop() {
    webConfigHandleClient();
    buttonsUpdate();

    ButtonEvent left   = buttonsGetLeft();
    ButtonEvent center = buttonsGetCenter();
    ButtonEvent right  = buttonsGetRight();

    if (g_state == STATE_MENU) {
        if (left == BTN_CLICK || left == BTN_DOUBLE_CLICK) {
            g_menuIndex = (g_menuIndex + 4) % 5;
        }
        if (right == BTN_CLICK || right == BTN_DOUBLE_CLICK) {
            g_menuIndex = (g_menuIndex + 1) % 5;
        }
        if (center == BTN_CLICK || center == BTN_DOUBLE_CLICK) {
            switch (g_menuIndex) {
                case 0: g_state = STATE_CLOCK;    break;
                case 1: {
                    g_state = STATE_CALENDAR;
                    struct tm t;
                    if (getLocalTime(&t)) {
                        g_calYear = t.tm_year + 1900;
                        g_calMonth = t.tm_mon + 1;
                    }
                    break;
                }
                case 2: g_state = STATE_WEATHER;  break;
                case 3:
                    g_state = STATE_TIMER;
                    g_timerRunning = false;
                    g_timerDigitPos = 0;
                    break;
                case 4: g_state = STATE_STOPWATCH; break;
            }
            if (g_state != STATE_MENU) {
                delayWithButtonPoll(80);
                return;
            }
        }
        menuScreenDraw();
        delayWithButtonPoll(80);
        return;
    }

    if (center == BTN_LONG_PRESS) {
        g_state = STATE_MENU;
        menuScreenDraw();
        delayWithButtonPoll(80);
        return;
    }

    if (g_state == STATE_CALENDAR) {
        if (left == BTN_CLICK || left == BTN_DOUBLE_CLICK) {
            g_calMonth--;
            if (g_calMonth < 1) { g_calMonth = 12; g_calYear--; }
        }
        if (right == BTN_CLICK || right == BTN_DOUBLE_CLICK) {
            g_calMonth++;
            if (g_calMonth > 12) { g_calMonth = 1; g_calYear++; }
        }
        if (center == BTN_DOUBLE_CLICK) {
            struct tm t;
            if (getLocalTime(&t)) {
                g_calYear = t.tm_year + 1900;
                g_calMonth = t.tm_mon + 1;
            }
        }
    }

    if (g_state == STATE_STOPWATCH) {
        if (center == BTN_DOUBLE_CLICK) {
            g_stopwatchAccumulatedMs = 0;
            g_stopwatchRunStartMillis = 0;
        } else if (center == BTN_CLICK) {
            if (g_stopwatchRunStartMillis != 0) {
                g_stopwatchAccumulatedMs += (uint32_t)(millis() - g_stopwatchRunStartMillis);
                g_stopwatchRunStartMillis = 0;
            } else {
                g_stopwatchRunStartMillis = millis();
            }
        }
    }

    if (g_state == STATE_TIMER) {
        if (g_timerRunning) {
            if (millis() >= g_timerEndMillis) {
                g_timerRunning = false;
                timerScreenPlayBeep();
            }
        } else {
            if (left == BTN_CLICK || left == BTN_DOUBLE_CLICK) {
                g_timerDigitPos = (g_timerDigitPos + 3) % 4;
            }
            if (right == BTN_CLICK || right == BTN_DOUBLE_CLICK) {
                g_timerDigitPos = (g_timerDigitPos + 1) % 4;
            }
            if (center == BTN_CLICK) {
                g_timerDigits[g_timerDigitPos] = (g_timerDigits[g_timerDigitPos] + 1) % 10;
            }
            if (center == BTN_DOUBLE_CLICK) {
                uint32_t totalSec = (g_timerDigits[0] * 10U + g_timerDigits[1]) * 60U
                    + (g_timerDigits[2] * 10U + g_timerDigits[3]);
                if (totalSec > 0) {
                    g_timerEndMillis = millis() + totalSec * 1000;
                    g_timerRunning = true;
                }
            }
        }
    }

    switch (g_state) {
        case STATE_CLOCK:
            if (!g_ntpSynced) {
                if (!clockScreenSyncNtp(displayNtpBootScreen, 20, 80))
                    Serial.println("NTP sync failed");
                g_ntpSynced = true;
            }
            clockScreenDraw();
            delayWithButtonPoll(100);
            break;
        case STATE_CALENDAR: {
            if (!g_ntpSynced) {
                if (!clockScreenSyncNtp(displayNtpBootScreen, 20, 80))
                    Serial.println("NTP sync failed");
                g_ntpSynced = true;
                struct tm t;
                if (getLocalTime(&t)) {
                    g_calYear = t.tm_year + 1900;
                    g_calMonth = t.tm_mon + 1;
                }
            }
            struct tm t;
            int ty = 2026, tmon = 1, td = 1;
            if (getLocalTime(&t)) {
                ty = t.tm_year + 1900;
                tmon = t.tm_mon + 1;
                td = t.tm_mday;
            }
            calendarScreenDraw(ty, tmon, td);
            delayWithButtonPoll(80);
            break;
        }
        case STATE_WEATHER:
            weatherScreenDraw();
            delayWithButtonPoll(200);
            break;
        case STATE_TIMER:
            timerScreenDraw();
            delayWithButtonPoll(g_timerRunning ? 50 : 80);
            break;
        case STATE_STOPWATCH:
            stopwatchScreenDraw();
            delayWithButtonPoll(50);
            break;
        default:
            g_state = STATE_MENU;
            delayWithButtonPoll(80);
            break;
    }
}
