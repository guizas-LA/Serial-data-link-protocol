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

static volatile sig_atomic_t alarmCount = 0;
static unsigned char txSequence = 0;
static unsigned char rxExpectedSequence = 0;
static int maxRetransmissions = 3;

/* O alarme interrompe a leitura quando o tempo de espera termina. */
static void alarmHandler(int signal)
{
    (void)signal;
    alarmCount = 1;
}

/* Configura o tratamento do alarme usado pelo transmissor. */
static int configureAlarmHandler(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = alarmHandler;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGALRM, &action, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    }

    return 0;
}

/* Escreve uma trama completa, mesmo que write envie apenas parte dela. */
static int writeAll(const unsigned char *data, int size)
{
    int totalWritten = 0;
    while (totalWritten < size)
    {
        int written = writeBytesSerialPort(data + totalWritten, size - totalWritten);
        if (written < 0)
        {
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

/* Envia uma trama de supervisao, como RR ou REJ. */
static int sendSupervisionFrame(unsigned char address, unsigned char control)
{
    unsigned char frame[SUPERVISION_FRAME_SIZE] = {
        FLAG, address, control, (unsigned char)(address ^ control), FLAG
    };
    return writeAll(frame, SUPERVISION_FRAME_SIZE) < 0 ? -1 : 0;
}

/* Le os bytes entre duas flags consecutivas, sem incluir as flags. */
static int readFrameBody(unsigned char *body, int capacity)
{
    int started = 0;
    int length = 0;
    unsigned char byte;

    while (1)
    {
        int result = readByteSerialPort(&byte);
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            perror("readByteSerialPort");
            return -1;
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
                /* Descarta uma trama demasiado grande e volta a sincronizar. */
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
                   unsigned char *output, int outputCapacity)
{
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
                                unsigned char sequence)
{
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

/* Le a resposta RR/REJ enviada pelo recetor e valida o cabecalho. */
static int receiveSupervisionResponse(unsigned char *control)
{
    unsigned char stuffed[FRAME_BUFFER_SIZE];
    unsigned char raw[FRAME_BUFFER_SIZE];

    while (1)
    {
        int length = readFrameBody(stuffed, sizeof(stuffed));
        if (length < 0)
            return -1;
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

/* Recebe uma trama I e devolve o seu numero de sequencia e os dados. */
static int receiveInformationFrame(unsigned char *data, int *dataLength,
                                   unsigned char *sequence, int *dataError)
{
    unsigned char stuffed[FRAME_BUFFER_SIZE];
    unsigned char raw[FRAME_BUFFER_SIZE];

    *dataError = 0;
    while (1)
    {
        int length = readFrameBody(stuffed, sizeof(stuffed));
        if (length < 0)
            return -1;

        int rawLength = destuff(stuffed, length, raw, sizeof(raw));
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
int llOpenTx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }
    if (configureAlarmHandler() < 0)
    {
        closeSerialPort();
        return -1;
    }

    const unsigned char setFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_TX, C_SET, (unsigned char)(A_TX ^ C_SET), FLAG
    };
    int connectionEstablished = FALSE;
    int totalAttempts = llParameters.nRetransmissions + 1;
    maxRetransmissions = llParameters.nRetransmissions;

    for (int attempt = 1; attempt <= totalAttempts; attempt++)
    {
        printf("TX: envio de SET (tentativa %d/%d).\n", attempt, totalAttempts);
        alarmCount = 0;
        int receivedFrame = sendFrameTX(setFrame, SUPERVISION_FRAME_SIZE,
                                        llParameters.timeout);
        if (receivedFrame == FRAME_OK)
        {
            printf("TX: trama UA recebida; ligacao estabelecida.\n");
            connectionEstablished = TRUE;
            break;
        }
        if (receivedFrame == FRAME_TIMEOUT)
        {
            printf("TX: tempo de espera esgotado.\n");
            continue;
        }
        fprintf(stderr, "TX: erro durante o estabelecimento da ligacao.\n");
        closeSerialPort();
        return -1;
    }

    if (!connectionEstablished)
    {
        fprintf(stderr, "TX: nao foi possivel estabelecer a ligacao.\n");
        closeSerialPort();
        return -1;
    }

    txSequence = 0;
    return 0;
}

/* Espera por SET valido e responde com a trama UA. */
int llOpenRx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("RX: a aguardar a trama SET.\n");
    if (receiveSupervisionFrame(A_TX, C_SET) != FRAME_OK)
    {
        fprintf(stderr, "RX: erro ao receber a trama SET.\n");
        closeSerialPort();
        return -1;
    }

    const unsigned char uaFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_RX, C_UA, (unsigned char)(A_RX ^ C_UA), FLAG
    };
    if (writeAll(uaFrame, SUPERVISION_FRAME_SIZE) < 0)
    {
        fprintf(stderr, "RX: erro ao enviar a trama UA.\n");
        closeSerialPort();
        return -1;
    }

    printf("RX: trama UA enviada; ligacao estabelecida.\n");
    rxExpectedSequence = 0;
    return 0;
}

/* Envia um pacote numa trama I e espera por um RR ou REJ. */
int llSend(const unsigned char *buf, int bufSize)
{
    if (buf == NULL || bufSize < 0 || bufSize > MAX_PAYLOAD_SIZE)
    {
        fprintf(stderr, "llSend: tamanho de pacote invalido.\n");
        return -1;
    }

    int totalAttempts = maxRetransmissions + 1;
    for (int attempt = 0; attempt < totalAttempts; attempt++)
    {
        if (sendInformationFrame(buf, bufSize, txSequence) < 0)
            return -1;

        while (1)
        {
            unsigned char control;
            if (receiveSupervisionResponse(&control) < 0)
                return -1;

            unsigned char expectedRR = (unsigned char)(C_RR_BASE | (txSequence ^ 1));
            unsigned char expectedREJ = (unsigned char)(C_REJ_BASE | txSequence);
            if (control == expectedRR)
            {
                txSequence ^= 1;
                return bufSize;
            }
            if (control == expectedREJ)
            {
                printf("TX: REJ recebido; sera repetida a trama I.\n");
                break;
            }
            /* Uma resposta antiga nao confirma o pacote atual. */
        }
    }

    fprintf(stderr, "TX: numero maximo de tentativas atingido.\n");
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

        if (receiveInformationFrame(receivedData, &dataLength, &sequence,
                                   &dataError) < 0)
            return -1;

        if (dataError)
        {
            /* Pede a repeticao do numero de sequencia esperado. */
            if (sendSupervisionFrame(A_TX,
                    (unsigned char)(C_REJ_BASE | rxExpectedSequence)) < 0)
                return -1;
            continue;
        }

        if (sequence == rxExpectedSequence)
        {
            memcpy(packet, receivedData, (size_t)dataLength);
            rxExpectedSequence ^= 1;
            /* Confirma a proxima sequencia esperada. */
            if (sendSupervisionFrame(A_TX,
                    (unsigned char)(C_RR_BASE | rxExpectedSequence)) < 0)
                return -1;
            return dataLength;
        }

        /* A trama e duplicada: descarta os dados e volta a esperar. */
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
