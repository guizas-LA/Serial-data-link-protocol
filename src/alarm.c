// RCOM 2026/2027
//
// Alarm used for the retransmission time-outs

#include "alarm.h"
#include "link_layer.h"
#include <stdio.h>
#include <unistd.h>

// Flag que fica a TRUE quando o alarme dispara (timeout)
volatile sig_atomic_t alarmTriggered = FALSE;
// Numero total de timeouts, usado nas estatisticas
int timeouts = 0;


// Funcao chamada quando chega o sinal SIGALRM
static void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    timeouts++;
    printf("Alarme #%d disparado\n", timeouts);
}

// Liga o alarme para disparar daqui a 'seconds' segundos
void startAlarm(int seconds) {
    alarmTriggered = FALSE;
    alarm(seconds);
}

// Desliga o alarme (a resposta chegou a tempo)
void stopAlarm() {
    alarm(0);
    alarmTriggered = FALSE;
}

// Associa o alarmHandler ao sinal SIGALRM
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




