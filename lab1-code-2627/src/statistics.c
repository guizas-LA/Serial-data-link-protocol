// RCOM 2026/2027
//
// Statistics of the link layer connection

#include "statistics.h"
#include "alarm.h"
#include <stdio.h>
#include <string.h>

Statistics stats;

void resetStatistics() {
    memset(&stats, 0, sizeof(stats));
}

void startStatisticsClock() {
    clock_gettime(CLOCK_MONOTONIC, &stats.start);
}

double elapsedTime() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return (now.tv_sec - stats.start.tv_sec) + (now.tv_nsec - stats.start.tv_nsec) / 1e9;
}

void printStatistics(int transmitter, double seconds) {
    printf("\n-------------------------------\n");
    printf("      Estatísticas (%s)    ", transmitter ? "emissor" : "recetor");
    printf("\n-------------------------------\n");
    printf("Tempo total: %.3f s\n", seconds);

    if (transmitter) {
        printf("Tramas I confirmadas: %d (%ld bytes de dados)\n", stats.frames, stats.dataBytes);
        printf("Tramas I enviadas: %d (incluindo retransmissões)\n", stats.iFramesSent);
        printf("REJ recebidos: %d\n", stats.rejCount);
    }
    else {
        printf("Tramas I recebidas: %d (%ld bytes de dados)\n", stats.frames, stats.dataBytes);
        printf("Duplicados descartados: %d\n", stats.duplicates);
        printf("REJ enviados: %d\n", stats.rejCount);
    }

    printf("Retransmissões: %d\n", stats.retransmissions);
    printf("Timeouts: %d\n", alarmCount);
}
