// RCOM 2026/2027
//
// Statistics of the link layer connection

#ifndef STATISTICS_H
#define STATISTICS_H

#include <time.h>

typedef struct {
    int frames;
    long dataBytes;
    int iFramesSent;
    int retransmissions;
    int rejCount;
    int duplicates;
    struct timespec start;
} Statistics;

extern Statistics stats;

void resetStatistics();

void startStatisticsClock();

double elapsedTime();

void printStatistics(int transmitter, double seconds);

#endif // STATISTICS_H
