/**
 * @file buttons.cpp
 * @brief 按键状态机：消抖 + 单击/双击/长按判定
 */
#include "buttons.h"

static uint8_t readStable(int pin, uint8_t* lastRaw, uint32_t* lastChange) {
    uint8_t raw = (digitalRead(pin) == LOW) ? 1 : 0;
    uint32_t now = millis();
    if (raw != *lastRaw) {
        *lastRaw = raw;
        *lastChange = now;
    }
    if (now - *lastChange >= BTN_DEBOUNCE_MS)
        return raw;
    return *lastRaw;
}

struct BtnState {
    int pin;
    uint8_t lastRaw;
    uint32_t lastChange;
    uint8_t stable;
    uint8_t wasStable;
    uint32_t pressTime;
    uint8_t inDoubleWindow;
    uint32_t releaseTime;
    ButtonEvent pending;
};

static BtnState s_left  = { BTN_LEFT_PIN,   0, 0, 0, 0, 0, 0, 0, BTN_NONE };
static BtnState s_center = { BTN_CENTER_PIN, 0, 0, 0, 0, 0, 0, 0, BTN_NONE };
static BtnState s_right = { BTN_RIGHT_PIN,  0, 0, 0, 0, 0, 0, 0, BTN_NONE };

static void updateOne(BtnState* b) {
    b->wasStable = b->stable;
    b->stable = readStable(b->pin, &b->lastRaw, &b->lastChange);
    uint32_t now = millis();

    if (b->stable && !b->wasStable) {
        b->pressTime = now;
    }
    if (!b->stable && b->wasStable) {
        uint32_t hold = now - b->pressTime;
        if (hold >= BTN_LONG_PRESS_MS) {
            b->pending = BTN_LONG_PRESS;
            b->inDoubleWindow = 0;
        } else if (hold >= BTN_DEBOUNCE_MS) {
            if (b->inDoubleWindow && (now - b->releaseTime <= BTN_DOUBLE_MS)) {
                b->pending = BTN_DOUBLE_CLICK;
                b->inDoubleWindow = 0;
            } else {
                b->releaseTime = now;
                b->inDoubleWindow = 1;
            }
        }
    }
    if (b->inDoubleWindow && (now - b->releaseTime >= BTN_DOUBLE_MS)) {
        b->inDoubleWindow = 0;
        if (b->pending == BTN_NONE)
            b->pending = BTN_CLICK;
    }
}

void buttonsInit(void) {
    pinMode(BTN_LEFT_PIN,   INPUT_PULLUP);
    pinMode(BTN_CENTER_PIN, INPUT_PULLUP);
    pinMode(BTN_RIGHT_PIN,  INPUT_PULLUP);
}

void buttonsUpdate(void) {
    updateOne(&s_left);
    updateOne(&s_center);
    updateOne(&s_right);
}

static ButtonEvent consume(BtnState* b) {
    ButtonEvent e = b->pending;
    b->pending = BTN_NONE;
    return e;
}

ButtonEvent buttonsGetLeft(void)   { return consume(&s_left);   }
ButtonEvent buttonsGetCenter(void) { return consume(&s_center); }
ButtonEvent buttonsGetRight(void)  { return consume(&s_right); }
