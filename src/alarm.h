// RCOM 2026/2027
//
// Alarm used for the retransmission time-outs

#ifndef ALARM_H
#define ALARM_H

#include <signal.h>

extern int timeouts;

extern volatile sig_atomic_t alarmTriggered;

void startAlarm(int seconds);

void stopAlarm();

int installAlarm();

#endif // ALARM_H
