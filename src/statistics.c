// RCOM 2026/2027
//
// Statistics of the link layer connection

#include "statistics.h"
#include "alarm.h"
#include "link_layer.h"
#include <stdio.h>
#include <string.h>

// Estatisticas da ligacao, partilhadas com o link_layer.c
Statistics stats;

// Guarda o instante em que a ligacao foi estabelecida
void startStatsClock() {
    clock_gettime(CLOCK_MONOTONIC, &stats.start);
}

// Poe todos os contadores a zero
void resetStats() {
    memset(&stats, 0, sizeof(stats));
}

// Devolve os segundos que passaram desde o startStatsClock
double elapsedTime() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return (now.tv_sec - stats.start.tv_sec) + (now.tv_nsec - stats.start.tv_nsec) / 1e9;
}

// Mostra as estatisticas do emissor ou do recetor no fim da ligacao
void printStats(int transmitter, double seconds, int baudRate) {
    printf("\n-------------------------------\n");
    printf("      Estatísticas (%s)    ", transmitter ? "emissor" : "recetor");
    printf("\n-------------------------------\n");
    printf("Tempo total: %.3f s\n", seconds);

    switch (transmitter) { 
        case TRUE:
            printf("Tramas I confirmadas: %d (%ld bytes de dados)\n", stats.frames, stats.dataBytes);
            printf("Tramas I enviadas: %d (incluindo retransmissões)\n", stats.iFramesSent);
            printf("REJ recebidos: %d\n", stats.rejCount);
            break;

        case FALSE:
            printf("Tramas I recebidas: %d (%ld bytes de dados)\n", stats.frames, stats.dataBytes);
            printf("Duplicados descartados: %d\n", stats.duplicates);
            printf("REJ enviados: %d\n", stats.rejCount);
            break;
    }
    printf("Retransmissões: %d\n", stats.retransmissions);
    printf("Timeouts: %d\n", timeouts);

    // Debito R = bits de dados / tempo e eficiencia S = R / C
    long dataBits = stats.dataBytes * 8;
    double bitrate = 0;
    if (seconds > 0)  
        bitrate = dataBits / seconds;

    double efficiency = bitrate / baudRate;

    printf("Débito (R): %.1f bit/s\n", bitrate);
    printf("Capacidade (C): %d bit/s\n", baudRate);
    printf("Eficiência (S = R/C): %.4f\n", efficiency);
    printf("-------------------------------\n");
}
