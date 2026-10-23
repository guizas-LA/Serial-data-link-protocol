// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "alarm.h"
#include "frame.h"
#include "serial_port.h"
#include "statistics.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    RR,  
    REJ, 
    NONE 
} Answer;

static LinkLayer parameters;

static unsigned char txSequence = 0; 
static unsigned char rxSequence = 0; 

////////////////////////////////////////////////
// RETRANSMISSION AND ANSWERS
////////////////////////////////////////////////

static int sendAndWait(unsigned char a, unsigned char c, unsigned char answerA, unsigned char answerC) {
    for (int try = 0; try <= parameters.nRetransmissions; try++) {
        if (try > 0)
            stats.retransmissions++;

        if (sendSupervisionFrame(a, c) != SU_FRAME_SIZE)
            return -1;

        startAlarm(parameters.timeout);
        int result = receiveSupervisionFrame(answerA, answerC);
        stopAlarm();

        if (result == 0)
            return 0;
    }

    return -1;
}


static Answer waitAnswer(unsigned char ns) {
    Frame frame;

    while (TRUE) {
        FrameResult result = readFrame(&frame);
        if (result == FRAME_TIMEOUT || result == FRAME_IO_ERROR)
            return NONE;
        if (result != FRAME_OK || frame.a != A_TX)
            continue;

        if (frame.c == C_RR(ns ^ 1)) {
            printf("Recebido %s\n", frameName(frame.c));
            return RR;
        }
        if (frame.c == C_REJ(ns)) {
            printf("Recebido %s\n", frameName(frame.c));
            stats.rejCount++;
            return REJ;
        }
    }
}


static int answerDuplicate(unsigned char ns) {
    stats.duplicates++;
    printf("Recebido I%d outra vez (duplicado, descartado)\n", ns);

    return sendSupervisionFrame(A_TX, C_RR(rxSequence)) == SU_FRAME_SIZE ? 0 : -1;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters) {
    parameters = llParameters;
    txSequence = 0;
    resetStatistics();

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Porta série %s aberta\n", llParameters.serialPort);

    if (installAlarm() < 0)
    {
        closeSerialPort();
        return -1;
    }

    if (sendAndWait(A_TX, C_SET, A_TX, C_UA) < 0) {
        printf("Nenhum UA recebido, a desistir.\n");
        closeSerialPort();
        return -1;
    }

    startStatisticsClock();
    printf("Ligação estabelecida.\n");

    return 0;
}

int llOpenRx(LinkLayer llParameters) {
    parameters = llParameters;
    rxSequence = 0;
    resetStatistics();

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }

    printf("Porta série %s aberta\n", llParameters.serialPort);

    if (installAlarm() < 0) {
        closeSerialPort();
        return -1;
    }

    if (receiveSupervisionFrame(A_TX, C_SET) < 0 || sendSupervisionFrame(A_TX, C_UA) != SU_FRAME_SIZE) {
        closeSerialPort();
        return -1;
    }

    startStatisticsClock();
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
    int frameSize = buildInformationFrame(frame, buf, bufSize, txSequence);


    for (int try = 0; try <= parameters.nRetransmissions; try++) {
        if (try > 0)
            stats.retransmissions++;

        if (writeFrame(frame, frameSize) < 0)
            return -1;

        stats.iFramesSent++;
        printf("Enviado I%d (%d bytes de dados, %d bytes após stuffing)%s\n",
               txSequence, bufSize, frameSize, try > 0 ? " [retransmissão]" : "");

        startAlarm(parameters.timeout);
        Answer answer = waitAnswer(txSequence);
        stopAlarm();

        if (answer == RR) {
            txSequence ^= 1;
            stats.frames++;
            stats.dataBytes += bufSize;
            return bufSize;
        }
    }

    printf("I%d não foi confirmado, a desistir.\n", txSequence);

    return -1;
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

        if (result == FRAME_OK && frame.a == A_TX && frame.c == C_SET) {
            printf("Recebido SET outra vez (o UA perdeu-se)\n");
            if (sendSupervisionFrame(A_TX, C_UA) != SU_FRAME_SIZE)
                return -1;
            continue;
        }

        if (frame.a != A_TX || !isInformationFrame(frame.c))
            continue;

        unsigned char ns = (frame.c == C_I(1));

        if (ns != rxSequence) {
            if (answerDuplicate(ns) < 0)
                return -1;
            continue;
        }

        if (result == FRAME_DATA_ERROR) {
            printf("Recebido I%d com erro no campo de dados (descartado)\n", ns);
            stats.rejCount++;
            if (sendSupervisionFrame(A_TX, C_REJ(ns)) != SU_FRAME_SIZE)
                return -1;
            continue;
        }

        printf("Recebido I%d (%d bytes de dados)\n", ns, frame.dataSize);
        memcpy(packet, frame.data, frame.dataSize);
        stats.frames++;
        stats.dataBytes += frame.dataSize;

        rxSequence ^= 1;
        if (sendSupervisionFrame(A_TX, C_RR(rxSequence)) != SU_FRAME_SIZE)
            return -1;

        return frame.dataSize;
    }
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx() {
    int result = sendAndWait(A_TX, C_DISC, A_RX, C_DISC);

    if (result == 0) {
        if (sendSupervisionFrame(A_RX, C_UA) != SU_FRAME_SIZE)
            result = -1;
    }
    else
        printf("Nenhum DISC recebido, a desistir.\n");

    double seconds = elapsedTime();

    sleep(1);

    if (closeSerialPort() < 0) {
        perror("closeSerialPort");
        return -1;
    }

    printf("Porta série fechada\n");

    printStatistics(TRUE, seconds);

    return result;
}

int llCloseRx() {
    int result = 0;
    Frame frame;

    while (TRUE)
    {
        FrameResult frameResult = readFrame(&frame);
        if (frameResult == FRAME_TIMEOUT || frameResult == FRAME_IO_ERROR)
        {
            result = -1;
            break;
        }
        if (frame.a != A_TX)
            continue;

        if (frameResult == FRAME_OK && frame.c == C_DISC) {
            printf("Recebido DISC\n");
            break;
        }

        unsigned char ns = (frame.c == C_I(1));
        if (isInformationFrame(frame.c) && ns != rxSequence && answerDuplicate(ns) < 0) {
            result = -1;
            break;
        }
    }

  
    if (result == 0 && sendAndWait(A_RX, C_DISC, A_RX, C_UA) < 0) {
        printf("Nenhum UA recebido , a desistir.\n");
        result = -1;
    }

    double seconds = elapsedTime();

    if (closeSerialPort() < 0) {
        perror("closeSerialPort");
        return -1;
    }

    printf("Porta série fechada\n");

    printStatistics(FALSE, seconds);

    return result;
}
