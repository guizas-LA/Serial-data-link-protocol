// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "alarm.h"
#include "frame.h"
#include "serial_port.h"
#include "simulation.h"
#include "statistics.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define NO_REJ -1 

typedef enum {
    RR,   // expected answer received
    REJ,
    NONE
} Answer;

static LinkLayer parameters;
static unsigned char sequence = 0;

////////////////////////////////////////////////
// RETRANSMISSION AND ANSWERS
////////////////////////////////////////////////

static Answer waitAnswer(unsigned char answerA, unsigned char answerC, int rejC) {
    Frame frame;

    while (TRUE) {
        FrameResult result = readFrame(&frame);
        if (result == FRAME_TIMEOUT || result == FRAME_IO_ERROR)
            return NONE;
        if (result != FRAME_OK || frame.a != answerA)
            continue;

        if (frame.c == answerC) {
            printf("Recebido %s\n", frameName(frame.c));
            return RR;
        }
        if (frame.c == rejC) {
            printf("Recebido %s\n", frameName(frame.c));
            stats.rejCount++;
            return REJ;
        }
    }
}


static int transmit(const unsigned char *frame, int frameSize, unsigned char answerA, unsigned char answerC, int rejC) {
    unsigned char c = frame[2];

    for (int try = 0; try <= parameters.nRetransmissions; try++) {
        if (try > 0)
            stats.retransmissions++;

        if (writeFrame(frame, frameSize) < 0)
            return -1;

        if (isInformationFrame(c))
            stats.iFramesSent++;
        printf("Enviado %s (%d bytes)%s\n", frameName(c), frameSize, try > 0 ? " [retransmissão]" : "");

        startAlarm(parameters.timeout);
        Answer answer = waitAnswer(answerA, answerC, rejC);
        stopAlarm();

        if (answer == RR)
            return 0;
    }

    printf("%s não foi confirmado, a desistir.\n", frameName(c));

    return -1;
}


static int sendAndWait(unsigned char a, unsigned char c, unsigned char answerA, unsigned char answerC) {
    unsigned char frame[SU_FRAME_SIZE];
    int frameSize = buildSupervisionFrame(frame, a, c);

    return transmit(frame, frameSize, answerA, answerC, NO_REJ);
}


static int answerDuplicate(unsigned char ns) {
    stats.duplicates++;
    printf("Recebido I%d outra vez (duplicado, descartado)\n", ns);

    return sendSupervisionFrame(A_TX, C_RR(sequence)) == SU_FRAME_SIZE ? 0 : -1;
}


static int handleInformationFrame(const Frame *frame, FrameResult result, unsigned char *packet) {
    unsigned char ns = frame->c >> 7;

    if (ns != sequence)
        return answerDuplicate(ns);

    if (result == FRAME_DATA_ERROR) {
        printf("Recebido I%d com erro no campo de dados (descartado)\n", ns);
        stats.rejCount++;
        return sendSupervisionFrame(A_TX, C_REJ(ns)) == SU_FRAME_SIZE ? 0 : -1;
    }

    printf("Recebido I%d (%d bytes de dados)\n", ns, frame->dataSize);
    memcpy(packet, frame->data, frame->dataSize);
    stats.frames++;
    stats.dataBytes += frame->dataSize;

    sequence ^= 1;
    if (sendSupervisionFrame(A_TX, C_RR(sequence)) != SU_FRAME_SIZE)
        return -1;

    return frame->dataSize;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////

static int setupConnection(LinkLayer llParameters) {
    parameters = llParameters;
    sequence = 0;
    resetStats();

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }

    printf("Porta série %s aberta\n", llParameters.serialPort);

    if (installAlarm() < 0) {
        closeSerialPort();
        return -1;
    }

    return 0;
}

int llOpenTx(LinkLayer llParameters) {
    if (setupConnection(llParameters) < 0)
        return -1;

    if (sendAndWait(A_TX, C_SET, A_TX, C_UA) < 0) {
        closeSerialPort();
        return -1;
    }

    startStatsClock();
    printf("Ligação estabelecida.\n");

    return 0;
}

int llOpenRx(LinkLayer llParameters) {
    if (setupConnection(llParameters) < 0)
        return -1;

    initSimulation();

    if (receiveSupervisionFrame(A_TX, C_SET) < 0 || sendSupervisionFrame(A_TX, C_UA) != SU_FRAME_SIZE) {
        closeSerialPort();
        return -1;
    }

    startStatsClock();
    printf("Ligação estabelecida.\n");

    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize) {
    if (buf == NULL || bufSize <= 0 || bufSize > MAX_PAYLOAD_SIZE) {
        printf("llSend: tamanho inválido %d (deve estar entre 1 e %d)\n", bufSize, MAX_PAYLOAD_SIZE);
        return -1;
    }

    unsigned char frame[MAX_I_FRAME_SIZE];
    int frameSize = buildInformationFrame(frame, buf, bufSize, sequence);

    if (transmit(frame, frameSize, A_TX, C_RR(sequence ^ 1), C_REJ(sequence)) < 0)
        return -1;

    sequence ^= 1;
    stats.frames++;
    stats.dataBytes += bufSize;

    return bufSize;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet) {
    Frame frame;

    while (TRUE) {
        FrameResult result = readFrame(&frame);
        if (result == FRAME_TIMEOUT || result == FRAME_IO_ERROR)
            return -1;
        if (frame.a != A_TX)
            continue;

        if (result == FRAME_OK && frame.c == C_SET) {
            printf("Recebido SET outra vez (o UA perdeu-se)\n");
            if (sendSupervisionFrame(A_TX, C_UA) != SU_FRAME_SIZE)
                return -1;
            continue;
        }

        if (!isInformationFrame(frame.c))
            continue;

        simulateDelay();
        if (result == FRAME_OK && simulateHeaderError()) {
            printf("[simulação] erro no cabeçalho de I%d (ignorada)\n", frame.c >> 7);
            continue;
        }
        if (result == FRAME_OK && simulateDataError()) {
            printf("[simulação] erro nos dados de I%d\n", frame.c >> 7);
            result = FRAME_DATA_ERROR;
        }

        int size = handleInformationFrame(&frame, result, packet);
        if (size != 0)
            return size;
    }
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx() {
    int result = sendAndWait(A_TX, C_DISC, A_RX, C_DISC);

    if (result == 0 && sendSupervisionFrame(A_RX, C_UA) != SU_FRAME_SIZE)
        result = -1;

    double seconds = elapsedTime();

    sleep(1);

    if (closeSerialPort() < 0) {
        perror("closeSerialPort");
        return -1;
    }

    printf("Porta série fechada\n");

    printStats(TRUE, seconds, parameters.baudRate);

    return result;
}

int llCloseRx() {
    int result = 0;
    Frame frame;

    while (TRUE) {
        FrameResult frameResult = readFrame(&frame);
        if (frameResult == FRAME_TIMEOUT || frameResult == FRAME_IO_ERROR) {
            result = -1;
            break;
        }
        if (frame.a != A_TX)
            continue;

        if (frameResult == FRAME_OK && frame.c == C_DISC) {
            printf("Recebido DISC\n");
            break;
        }

        unsigned char ns = frame.c >> 7;
        if (isInformationFrame(frame.c) && ns != sequence && answerDuplicate(ns) < 0) {
            result = -1;
            break;
        }
    }

    if (result == 0 && sendAndWait(A_RX, C_DISC, A_RX, C_UA) < 0)
        result = -1;

    double seconds = elapsedTime();

    if (closeSerialPort() < 0) {
        perror("closeSerialPort");
        return -1;
    }

    printf("Porta série fechada\n");

    printStats(FALSE, seconds, parameters.baudRate);

    return result;
}
