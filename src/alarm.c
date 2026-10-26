// RCOM 2026/2027
//
// Alarm used for the retransmission time-outs

#include "alarm.h"
#include "link_layer.h"
#include <stdio.h>
#include <unistd.h>

volatile sig_atomic_t alarmTriggered = FALSE;
int timeouts = 0;


static void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    timeouts++;
    printf("Alarme #%d disparado\n", timeouts);
}

void startAlarm(int seconds) {
    alarmTriggered = FALSE;
    alarm(seconds);
}

void stopAlarm() {
    alarm(0);
    alarmTriggered = FALSE;
}

int installAlarm() {
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    timeouts = 0;
    return 0;
}




