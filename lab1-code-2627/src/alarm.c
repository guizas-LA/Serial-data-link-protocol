// RCOM 2026/2027
//
// Alarm used for the retransmission time-outs

#include "alarm.h"
#include "link_layer.h"
#include <stdio.h>
#include <unistd.h>

int alarmCount = 0;

volatile sig_atomic_t alarmFired = FALSE;

static void alarmHandler(int signal) {
    alarmFired = TRUE;
    alarmCount++;
    printf("Alarme #%d disparado\n", alarmCount);
}

int installAlarm() {
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    }

    alarmCount = 0;
    return 0;
}

void startAlarm(int seconds) {
    alarmFired = FALSE;
    alarm(seconds);
}

void stopAlarm() {
    alarm(0);
    alarmFired = FALSE;
}
