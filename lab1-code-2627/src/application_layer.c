// RCOM 2026/2027
//
// This intentionally bypasses the link-layer protocol.

#include "application_layer.h"
#include "serial_port.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define M1_TEST_MESSAGE "M1: serial communication test\n"
#define M1_MAX_MESSAGE 256

static int sendString(const char *message) {
    size_t length = strlen(message);
    size_t sent = 0;

    while (sent < length)
    {
        int result = writeBytesSerialPort(
            (const unsigned char *)message + sent,
            (int)(length - sent));

        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            perror("writeBytesSerialPort");
            return -1;
        }

        if (result == 0)
        {
            fprintf(stderr, "No bytes were written to the serial port.\n");
            return -1;
        }

        sent += (size_t)result;
    }

    printf("Sent %zu bytes: %s", sent, message);
    return 0;
}

static int receiveString(void)
{
    unsigned char message[M1_MAX_MESSAGE];
    size_t received = 0;

    while (received < sizeof(message) - 1)
    {
        unsigned char byte;
        int result = readByteSerialPort(&byte);

        if (result < 0){
            if (errno == EINTR)
                continue;
            perror("readByteSerialPort");
            return -1;
        }

        if (result == 0)
            continue;

        message[received++] = byte;

        if (byte == '\n')
            break;
    }

    message[received] = '\0';

    printf("Received %zu bytes: %s", received, (char *)message);

    if (strcmp((char *)message, M1_TEST_MESSAGE) != 0) {
        fprintf(stderr,"M1 test FAILED: received string differs from expected string.\n");
        return -1;
    }

    printf("M1 test PASSED: received string matches the transmitted string.\n");
    return 0;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {
    (void)nTries;
    (void)timeout;
    (void)filename;

    if (openSerialPort(serialPort, baudRate) < 0){
        fprintf(stderr, "Could not open serial port %s.\n", serialPort);
        return;
    }

    printf("M1 raw-string test on %s (%s)\n", serialPort, role);

    int result;
    if (strcmp(role, "tx") == 0) {
        result = sendString(M1_TEST_MESSAGE);
    }
    
    else if (strcmp(role, "rx") == 0) {
        printf("Waiting for a string...\n");
        fflush(stdout);
        result = receiveString();
    }
    else {
        fprintf(stderr, "Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
        result = -1;
    }

    if (closeSerialPort() < 0) {
        perror("closeSerialPort");
        result = -1;
    }

    if (result < 0) fprintf(stderr, "M1 test FAILED.\n");
}
