/**
 * @file clock_screen.h
 * @brief 时钟页与 NTP 同步
 */
#ifndef CLOCK_SCREEN_H
#define CLOCK_SCREEN_H

bool clockScreenSyncNtp(void (*drawNtp)(const char*, int), int maxTries, int intervalMs);
void clockScreenDraw(void);

#endif
