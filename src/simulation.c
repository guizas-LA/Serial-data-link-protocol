// RCOM 2026/2027
//
// Error and delay simulation, used to characterise the protocol efficiency

#include "simulation.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// Devolve 1 com probabilidade p (entre 0 e 1)
static int chance(double p) {
    return p > 0 && rand() < p * ((double)RAND_MAX + 1);
}

// Inicia o gerador de numeros aleatorios
void initSimulation() {
    srand(time(NULL));
}

// Espera T_PROP_MS milissegundos para simular o atraso de propagacao
void simulateDelay() {
    if (T_PROP_MS > 0)
        usleep(T_PROP_MS * 1000);
}

// Decide se esta trama vai ter um erro simulado no cabecalho
int simulateHeaderError() {
    return chance(FER_HEADER);
}

// Decide se esta trama vai ter um erro simulado nos dados
int simulateDataError() {
    return chance(FER_DATA);
}
