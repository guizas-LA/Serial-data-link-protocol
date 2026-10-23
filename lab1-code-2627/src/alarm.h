// RCOM 2026/2027
//
// Alarm used for the retransmission time-outs

#ifndef ALARM_H
#define ALARM_H

#include <signal.h>

extern int alarmCount;

extern volatile sig_atomic_t alarmFired;

int installAlarm();

void startAlarm(int seconds);

void stopAlarm();

#endif // ALARM_H
