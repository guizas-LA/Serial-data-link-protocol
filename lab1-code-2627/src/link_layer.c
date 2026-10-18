// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "frame.h"
#include "serial_port.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define C_RR_BASE  0xAA
#define C_REJ_BASE 0x54
#define C_I_BASE   0x00
#define ESCAPE     0x7D
#define ESCAPE_XOR 0x20
#define FRAME_BUFFER_SIZE (2 * (MAX_PAYLOAD_SIZE + 4) + 8)

#define READ_ERROR   -1
#define READ_TIMEOUT -2

static volatile sig_atomic_t alarmCount = 0;
static unsigned char txSequence = 0;
static unsigned char rxExpectedSequence = 0;
static int maxRetransmissions = 3;
static int timeoutSeconds = 3;

/* O alarme interrompe a leitura quando o tempo de espera termina. */
static void alarmHandler(int signal) {
    (void)signal;
    alarmCount = 1;
}

/* Configura o tratamento do alarme usado pelo transmissor. */
static int configureAlarmHandler(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = alarmHandler;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGALRM, &action, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    return 0;
}

/* Escreve uma trama completa, mesmo que write envie apenas parte dela. */
static int writeAll(const unsigned char *data, int size) {
    int totalWritten = 0;
    while (totalWritten < size)
    {
        int written = writeBytesSerialPort(data + totalWritten, size - totalWritten);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            perror("writeBytesSerialPort");
            return -1;
        }
        if (written == 0)
            return -1;
        totalWritten += written;
    }
    return totalWritten;
}

/* Envia uma trama de supervisao (UA, RR ou REJ). */
static int sendSupervisionFrame(unsigned char address, unsigned char control) {
    unsigned char frame[SUPERVISION_FRAME_SIZE] = {
        FLAG, address, control, (unsigned char)(address ^ control), FLAG
    };
    return writeAll(frame, SUPERVISION_FRAME_SIZE) < 0 ? -1 : 0;
}

/* Le os bytes entre duas flags consecutivas (sem incluir as flags).
   Devolve o tamanho, READ_ERROR se der erro ou READ_TIMEOUT se o alarme disparou. */
static int readFrameBody(unsigned char *body, int capacity) {
    int started = 0;
    int length = 0;
    unsigned char byte;

    while (1)
    {
        /* O alarme pode ter disparado quando nao estavamos dentro do read() */
        if (alarmCount)
            return READ_TIMEOUT;

        int result = readByteSerialPort(&byte);
        if (result < 0)
        {
            if (errno == EINTR)
            {
                if (alarmCount)
                    return READ_TIMEOUT;
                continue;
            }
            perror("readByteSerialPort");
            return READ_ERROR;
        }
        if (result == 0)
            continue;

        if (byte == FLAG)
        {
            if (!started)
            {
                started = 1;
                length = 0;
                continue;
            }
            if (length == 0)
                continue; /* Ignora flags repetidas. */
            return length;
        }

        if (started)
        {
            if (length >= capacity)
            {
                /* Trama demasiado grande: descarta e volta a sincronizar. */
                started = 0;
                length = 0;
                continue;
            }
            body[length++] = byte;
        }
    }
}

/* Remove o byte stuffing aplicado aos bytes entre as flags. */
static int destuff(const unsigned char *input, int inputLength,
                   unsigned char *output, int outputCapacity) {
    int outputLength = 0;
    for (int i = 0; i < inputLength; i++)
    {
        unsigned char byte = input[i];
        if (byte == ESCAPE)
        {
            if (++i >= inputLength)
                return -1;
            byte = input[i] ^ ESCAPE_XOR;
            if (byte != FLAG && byte != ESCAPE)
                return -1;
        }
        if (outputLength >= outputCapacity)
            return -1;
        output[outputLength++] = byte;
    }
    return outputLength;
}

/* Envia a trama I com BCC1, BCC2 e byte stuffing. */
static int sendInformationFrame(const unsigned char *data, int dataLength,
                                unsigned char sequence) {
    unsigned char raw[MAX_PAYLOAD_SIZE + 4];
    unsigned char frame[FRAME_BUFFER_SIZE];
    int rawLength = 0;

    raw[rawLength++] = A_TX;
    raw[rawLength++] = (unsigned char)(C_I_BASE | (sequence << 7));
    raw[rawLength++] = (unsigned char)(raw[0] ^ raw[1]); /* BCC1 */

    unsigned char bcc2 = 0;
    for (int i = 0; i < dataLength; i++)
    {
        raw[rawLength++] = data[i];
        bcc2 ^= data[i];
    }
    raw[rawLength++] = bcc2;

    int frameLength = 0;
    frame[frameLength++] = FLAG;
    for (int i = 0; i < rawLength; i++)
    {
        if (raw[i] == FLAG || raw[i] == ESCAPE)
        {
            frame[frameLength++] = ESCAPE;
            frame[frameLength++] = raw[i] ^ ESCAPE_XOR;
        }
        else
        {
            frame[frameLength++] = raw[i];
        }
    }
    frame[frameLength++] = FLAG;

    return writeAll(frame, frameLength) < 0 ? -1 : 0;
}

/* Le a resposta RR/REJ enviada pelo recetor e valida o cabecalho.
   Devolve 0 se correr bem, READ_ERROR se der erro ou READ_TIMEOUT se houver timeout. */
static int receiveSupervisionResponse(unsigned char *control) {
    unsigned char stuffed[FRAME_BUFFER_SIZE];
    unsigned char raw[FRAME_BUFFER_SIZE];

    while (1)
    {
        int length = readFrameBody(stuffed, sizeof(stuffed));
        if (length < 0)
            return length;
        int rawLength = destuff(stuffed, length, raw, sizeof(raw));
        if (rawLength != 3)
            continue;
        if (raw[0] != A_TX || raw[2] != (unsigned char)(raw[0] ^ raw[1]))
            continue;
        if (raw[1] == C_RR_BASE || raw[1] == (C_RR_BASE | 1) ||
            raw[1] == C_REJ_BASE || raw[1] == (C_REJ_BASE | 1))
        {
            *control = raw[1];
            return 0;
        }
    }
}

/* Recebe uma trama I e devolve o seu numero de sequencia e os dados.
   Devolve 0 se recebeu uma trama I com cabecalho valido, 1 se recebeu uma
   trama SET (o Tx nao recebeu o nosso UA) e -1 se der erro. */
static int receiveInformationFrame(unsigned char *data, int *dataLength,
                                   unsigned char *sequence, int *dataError) {
    unsigned char stuffed[FRAME_BUFFER_SIZE];
    unsigned char raw[FRAME_BUFFER_SIZE];

    *dataError = 0;
    while (1)
    {
        int length = readFrameBody(stuffed, sizeof(stuffed));
        if (length < 0)
            return -1;

        int rawLength = destuff(stuffed, length, raw, sizeof(raw));

        /* Um SET repetido significa que o UA se perdeu */
        if (rawLength == 3 && raw[0] == A_TX && raw[1] == C_SET &&
            raw[2] == (unsigned char)(A_TX ^ C_SET))
            return 1;

        if (rawLength < 4)
            continue;
        if (raw[0] != A_TX)
            continue;
        if (raw[1] != 0x00 && raw[1] != 0x80)
            continue;
        if (raw[2] != (unsigned char)(raw[0] ^ raw[1]))
            continue; /* Cabecalho invalido: ignora a trama. */

        *sequence = (unsigned char)(raw[1] >> 7);
        *dataLength = rawLength - 4;
        if (*dataLength > MAX_PAYLOAD_SIZE)
            continue;

        unsigned char bcc2 = 0;
        for (int i = 0; i < *dataLength; i++)
            bcc2 ^= raw[3 + i];

        if (bcc2 != raw[rawLength - 1])
        {
            *dataError = 1;
            return 0;
        }

        memcpy(data, raw + 3, (size_t)*dataLength);
        return 0;
    }
}

/* Abre a porta e estabelece a ligacao: envia SET e espera por UA. */
int llOpenTx(LinkLayer llParameters) {
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }
    if (configureAlarmHandler() < 0) {
        closeSerialPort();
        return -1;
    }

    const unsigned char setFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_TX, C_SET, (unsigned char)(A_TX ^ C_SET), FLAG
    };
    int connectionEstablished = FALSE;
    int totalAttempts = llParameters.nRetransmissions + 1;
    maxRetransmissions = llParameters.nRetransmissions;
    timeoutSeconds = llParameters.timeout;

    for (int attempt = 1; attempt <= totalAttempts; attempt++) {
        printf("TX: sending SET (attempt %d/%d).\n", attempt, totalAttempts);
        alarmCount = 0;
        int receivedFrame = sendFrameTX(setFrame, SUPERVISION_FRAME_SIZE,
                                        llParameters.timeout);
        if (receivedFrame == FRAME_OK) {
            printf("TX: UA received; connection established.\n");
            connectionEstablished = TRUE;
            break;
        }
        if (receivedFrame == FRAME_TIMEOUT) {
            printf("TX: timeout.\n");
            continue;
        }
        fprintf(stderr, "TX: error while establishing the connection.\n");
        closeSerialPort();
        return -1;
    }

    if (!connectionEstablished) {
        fprintf(stderr, "TX: could not establish the connection.\n");
        closeSerialPort();
        return -1;
    }

    txSequence = 0;
    return 0;
}

/* Espera por SET valido e responde com a trama UA. */
int llOpenRx(LinkLayer llParameters) {
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }

    printf("RX: waiting for the SET frame.\n");
    if (receiveSupervisionFrame(A_TX, C_SET) != FRAME_OK)
    {
        fprintf(stderr, "RX: error while receiving the SET frame.\n");
        closeSerialPort();
        return -1;
    }

    /* O UA e uma resposta enviada pelo Recetor, logo A = A_TX (0x03) */
    if (sendSupervisionFrame(A_TX, C_UA) < 0)
    {
        fprintf(stderr, "RX: error while sending the UA frame.\n");
        closeSerialPort();
        return -1;
    }

    printf("RX: UA sent; connection established.\n");
    rxExpectedSequence = 0;
    return 0;
}

/* Envia um pacote numa trama I e espera por RR ou REJ (Stop-and-Wait).
   A trama I e repetida apos um REJ ou apos um timeout. */
int llSend(const unsigned char *buf, int bufSize)
{
    if (buf == NULL || bufSize < 0 || bufSize > MAX_PAYLOAD_SIZE)
    {
        fprintf(stderr, "llSend: invalid packet size.\n");
        return -1;
    }

    int totalAttempts = maxRetransmissions + 1;
    for (int attempt = 1; attempt <= totalAttempts; attempt++)
    {
        if (sendInformationFrame(buf, bufSize, txSequence) < 0)
            return -1;

        /* O temporizador comeca depois de enviar a trama I */
        alarmCount = 0;
        alarm((unsigned int)timeoutSeconds);

        int sendAgain = FALSE;
        while (!sendAgain)
        {
            unsigned char control;
            int result = receiveSupervisionResponse(&control);

            if (result == READ_ERROR)
            {
                alarm(0);
                return -1;
            }
            if (result == READ_TIMEOUT)
            {
                printf("TX: timeout (attempt %d/%d).\n", attempt, totalAttempts);
                sendAgain = TRUE;
                break;
            }

            unsigned char expectedRR = (unsigned char)(C_RR_BASE | (txSequence ^ 1));
            unsigned char expectedREJ = (unsigned char)(C_REJ_BASE | txSequence);
            if (control == expectedRR)
            {
                /* Confirmacao valida: desativa o temporizador */
                alarm(0);
                txSequence ^= 1;
                return bufSize;
            }
            if (control == expectedREJ)
            {
                printf("TX: REJ received; the I frame will be sent again.\n");
                sendAgain = TRUE;
                break;
            }
            /* Uma resposta antiga nao confirma o pacote atual. */
        }
        alarm(0);
    }

    fprintf(stderr, "TX: maximum number of attempts reached.\n");
    return -1;
}

/* Recebe um pacote; confirma tramas novas e descarta duplicados. */
int llReceive(unsigned char *packet)
{
    if (packet == NULL)
        return -1;

    while (1)
    {
        unsigned char receivedData[MAX_PAYLOAD_SIZE];
        unsigned char sequence = 0;
        int dataLength = 0;
        int dataError = 0;

        int result = receiveInformationFrame(receivedData, &dataLength, &sequence,
                                             &dataError);
        if (result < 0)
            return -1;

        if (result == 1)
        {
            /* O Tx enviou SET outra vez, logo nao recebeu o nosso UA */
            if (sendSupervisionFrame(A_TX, C_UA) < 0)
                return -1;
            continue;
        }

        if (dataError)
        {
            if (sequence == rxExpectedSequence)
            {
                /* Trama nova com erro nos dados: pede a repeticao. */
                if (sendSupervisionFrame(A_TX,
                        (unsigned char)(C_REJ_BASE | rxExpectedSequence)) < 0)
                    return -1;
            }
            else
            {
                /* Trama duplicada com erro nos dados: apenas confirma. */
                if (sendSupervisionFrame(A_TX,
                        (unsigned char)(C_RR_BASE | rxExpectedSequence)) < 0)
                    return -1;
            }
            continue;
        }

        if (sequence == rxExpectedSequence)
        {
            memcpy(packet, receivedData, (size_t)dataLength);
            rxExpectedSequence ^= 1;
            /* Confirma o proximo numero de sequencia esperado. */
            if (sendSupervisionFrame(A_TX,
                    (unsigned char)(C_RR_BASE | rxExpectedSequence)) < 0)
                return -1;
            return dataLength;
        }

        /* Trama duplicada: descarta os dados, confirma e espera pela seguinte. */
        if (sendSupervisionFrame(A_TX,
                (unsigned char)(C_RR_BASE | rxExpectedSequence)) < 0)
            return -1;
    }
}

/* Fecha a porta serie aberta pelo transmissor. */
int llCloseTx(void)
{
    return closeSerialPort();
}

/* Fecha a porta serie aberta pelo recetor. */
int llCloseRx(void)
{
    return closeSerialPort();
}
