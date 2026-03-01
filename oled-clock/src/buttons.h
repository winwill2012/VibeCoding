/**
 * @file buttons.h
 * @brief 三键检测：单击、双击、长按（左=GPIO5, 中=GPIO19, 右=GPIO18）
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

#define BTN_LEFT_PIN   18
#define BTN_CENTER_PIN 19
#define BTN_RIGHT_PIN   5

#define BTN_DEBOUNCE_MS    20
#define BTN_LONG_PRESS_MS  600
#define BTN_DOUBLE_MS      280
#define BTN_CLICK_MAX_MS   400

enum ButtonEvent {
    BTN_NONE = 0,
    BTN_CLICK,
    BTN_DOUBLE_CLICK,
    BTN_LONG_PRESS
};

void buttonsInit(void);
void buttonsUpdate(void);
ButtonEvent buttonsGetLeft(void);
ButtonEvent buttonsGetCenter(void);
ButtonEvent buttonsGetRight(void);

#endif
