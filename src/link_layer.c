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

// Maximo de REJ seguidos para a mesma trama (os timeouts usam o nRetransmissions)
#define MAX_REJ_RETRIES 20

// Resposta recebida pelo emissor
typedef enum {
    RR,   
    REJ,
    NONE
} Answer;

// Parametros da ligacao e numero de sequencia atual (Ns no emissor, esperado no recetor)
static LinkLayer parameters;
static unsigned char sequence = 0;

////////////////////////////////////////////////
// RETRANSMISSION AND ANSWERS
////////////////////////////////////////////////

// Espera pela resposta a uma trama: RR (confirmada), REJ ou NONE (timeout)
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


// Envia uma trama e retransmite em caso de timeout ou REJ (Stop-and-Wait)
static int transmit(const unsigned char *frame, int frameSize, unsigned char answerA, unsigned char answerC, int rejC) {
    unsigned char c = frame[2];
    int timeoutRetries = 0;
    int rejRetries = 0;

    while (TRUE) {
        if (writeFrame(frame, frameSize) < 0)
            return -1;

        if (isInformationFrame(c))
            stats.iFramesSent++;
        int retransmission = timeoutRetries + rejRetries > 0;
        printf("Enviado %s (%d bytes)%s\n", frameName(c), frameSize, retransmission ? " [retransmissão]" : "");

        startAlarm(parameters.timeout);
        Answer answer = waitAnswer(answerA, answerC, rejC);
        stopAlarm();

        if (answer == RR)
            return 0;

        if (answer == REJ)
            rejRetries++;
        else
            timeoutRetries++;

        // Desiste se houver demasiados timeouts ou demasiados REJ
        if (timeoutRetries > parameters.nRetransmissions || rejRetries > MAX_REJ_RETRIES)
            break;

        stats.retransmissions++;
    }

    printf("%s não foi confirmado, a desistir.\n", frameName(c));

    return -1;
}


// Envia uma trama de supervisao (SET ou DISC) e espera pela resposta
static int sendAndWait(unsigned char a, unsigned char c, unsigned char answerA, unsigned char answerC) {
    unsigned char frame[SU_FRAME_SIZE];
    int frameSize = buildSupervisionFrame(frame, a, c);

    return transmit(frame, frameSize, answerA, answerC, NO_REJ);
}


// Le uma trama no recetor; se nada chegar durante muito tempo, o emissor desistiu
static FrameResult readFrameOrGiveUp(Frame *frame) {
    int seconds = parameters.timeout * (parameters.nRetransmissions + 2);

    startAlarm(seconds);
    FrameResult result = readFrame(frame);
    stopAlarm();

    if (result == FRAME_TIMEOUT)
        printf("Nenhuma trama recebida em %d s, o emissor desistiu.\n", seconds);

    return result;
}


// Trama repetida: descarta os dados e volta a confirmar com RR
static int answerDuplicate(unsigned char ns) {
    stats.duplicates++;
    printf("Recebido I%d outra vez (duplicado, descartado)\n", ns);

    return sendSupervisionFrame(A_TX, C_RR(sequence)) == SU_FRAME_SIZE ? 0 : -1;
}


// Trata uma trama I recebida e responde com RR ou REJ
static int handleInformationFrame(const Frame *frame, FrameResult result, unsigned char *packet) {
    unsigned char ns = frame->c >> 7;

    if (ns != sequence)
        return answerDuplicate(ns);

    // Trama nova com erro no BCC2: pede a retransmissao com REJ
    if (result == FRAME_DATA_ERROR) {
        printf("Recebido I%d com erro no campo de dados (descartado)\n", ns);
        stats.rejCount++;
        return sendSupervisionFrame(A_TX, C_REJ(ns)) == SU_FRAME_SIZE ? 0 : -1;
    }

    printf("Recebido I%d (%d bytes de dados)\n", ns, frame->dataSize);
    memcpy(packet, frame->data, frame->dataSize);
    stats.frames++;
    stats.dataBytes += frame->dataSize;

    // Trama nova correta: passa ao proximo numero de sequencia e confirma
    sequence ^= 1;
    if (sendSupervisionFrame(A_TX, C_RR(sequence)) != SU_FRAME_SIZE)
        return -1;

    return frame->dataSize;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////

// Parte comum do llOpen: abre a porta serie e instala o alarme
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

// Emissor: envia SET e espera pelo UA
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

// Recetor: espera pelo SET e responde com UA
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
// Envia um pacote numa trama I e espera pela confirmacao
int llSend(const unsigned char *buf, int bufSize) {
    if (buf == NULL || bufSize <= 0 || bufSize > MAX_PAYLOAD_SIZE) {
        printf("llSend: tamanho inválido %d (deve estar entre 1 e %d)\n", bufSize, MAX_PAYLOAD_SIZE);
        return -1;
    }

    unsigned char frame[MAX_I_FRAME_SIZE];
    int frameSize = buildInformationFrame(frame, buf, bufSize, sequence);

    if (transmit(frame, frameSize, A_TX, C_RR(sequence ^ 1), C_REJ(sequence)) < 0)
        return -1;

    // Pacote confirmado: troca o Ns para a proxima trama
    sequence ^= 1;
    stats.frames++;
    stats.dataBytes += bufSize;

    return bufSize;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
// Recebe o proximo pacote novo (ignora duplicados e tramas com erro)
int llReceive(unsigned char *packet) {
    Frame frame;

    while (TRUE) {
        FrameResult result = readFrameOrGiveUp(&frame);
        if (result == FRAME_TIMEOUT || result == FRAME_IO_ERROR)
            return -1;
        if (frame.a != A_TX)
            continue;

        // SET repetido: o UA perdeu-se, por isso envia outra vez
        if (result == FRAME_OK && frame.c == C_SET) {
            printf("Recebido SET outra vez (o UA perdeu-se)\n");
            if (sendSupervisionFrame(A_TX, C_UA) != SU_FRAME_SIZE)
                return -1;
            continue;
        }

        if (!isInformationFrame(frame.c))
            continue;

        // Simulacao de atraso e erros (desligada por omissao em simulation.h)
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
// Emissor: envia DISC, espera pelo DISC do recetor e responde com UA
int llCloseTx() {
    int result = sendAndWait(A_TX, C_DISC, A_RX, C_DISC);

    if (result == 0 && sendSupervisionFrame(A_RX, C_UA) != SU_FRAME_SIZE)
        result = -1;

    double seconds = elapsedTime();

    // Espera um pouco para o UA chegar antes de fechar a porta
    sleep(1);

    if (closeSerialPort() < 0) {
        perror("closeSerialPort");
        return -1;
    }

    printf("Porta série fechada\n");

    printStats(TRUE, seconds, parameters.baudRate);

    return result;
}

// Recetor: espera pelo DISC, responde com DISC e espera pelo UA
int llCloseRx() {
    int result = 0;
    Frame frame;

    while (TRUE) {
        FrameResult frameResult = readFrameOrGiveUp(&frame);
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
        // Se a ultima trama I chegar outra vez, volta a confirmar
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
