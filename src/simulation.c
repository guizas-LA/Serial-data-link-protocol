// RCOM 2026/2027
//
// Error and delay simulation, used to characterise the protocol efficiency

#include "simulation.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int chance(double p) {
    return p > 0 && rand() < p * ((double)RAND_MAX + 1);
}

void initSimulation() {
    srand(time(NULL));
}

void simulateDelay() {
    if (T_PROP_MS > 0)
        usleep(T_PROP_MS * 1000);
}

int simulateHeaderError() {
    return chance(FER_HEADER);
}

int simulateDataError() {
    return chance(FER_DATA);
}
