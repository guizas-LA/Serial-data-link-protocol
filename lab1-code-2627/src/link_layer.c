// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <errno.h>
#include <stdio.h>
#include <sys/select.h>
#include <time.h>

// Valores usados nas tramas SET e UA.
#define FLAG 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_REJ1 0x55
#define SUPERVISION_FRAME_SIZE 5
#define MAX_FRAME_CONTENT_SIZE (2 * (MAX_PAYLOAD_SIZE + 1) + 4)

// A porta fica aberta entre llOpen*() e llClose*().
static int linkIsOpen = 0;
static int linkIsTx = 0;
static unsigned char nextTxSequence = 0;
static unsigned char expectedRxSequence = 0;
static LinkLayer activeParameters;

static int makeDeadline(struct timespec *deadline, int timeoutSeconds)
{
    if (clock_gettime(CLOCK_MONOTONIC, deadline) < 0)
        return -1;
    deadline->tv_sec += timeoutSeconds;
    return 0;
}

// Retorna 1 se leu um byte, 0 se expirou e -1 em erro.
static int readByteUntil(unsigned char *byte, const struct timespec *deadline)
{
    if (deadline == NULL)
        return readByteSerialPort(byte);

    extern int fd; // descritor mantido pela biblioteca serial_port.c
    while (1)
    {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
            return -1;

        time_t seconds = deadline->tv_sec - now.tv_sec;
        long nanoseconds = deadline->tv_nsec - now.tv_nsec;
        if (nanoseconds < 0)
        {
            seconds--;
            nanoseconds += 1000000000L;
        }
        if (seconds < 0 || (seconds == 0 && nanoseconds <= 0))
            return 0;

        struct timeval timeout = {
            .tv_sec = seconds,
            .tv_usec = (nanoseconds + 999) / 1000};
        if (timeout.tv_usec >= 1000000)
        {
            timeout.tv_sec++;
            timeout.tv_usec -= 1000000;
        }
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd, &readSet);
        int ready = select(fd + 1, &readSet, NULL, NULL, &timeout);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            return -1;
        if (ready == 0)
            return 0;
        return readByteSerialPort(byte);
    }
}

static int writeAll(const unsigned char *bytes, int length)
{
    int written = 0;
    while (written < length)
    {
        int result = writeBytesSerialPort(bytes + written, length - written);
        if (result <= 0)
            return -1;
        written += result;
    }
    return 0;
}

static int appendStuffedByte(unsigned char byte, unsigned char *frame, int *length)
{
    if (byte == FLAG || byte == 0x7D)
    {
        frame[(*length)++] = 0x7D;
        frame[(*length)++] = byte ^ 0x20;
    }
    else
    {
        frame[(*length)++] = byte;
    }
    return 0;
}

static unsigned char rrControl(unsigned char nr)
{
    return nr ? C_RR1 : C_RR0;
}

static unsigned char rejControl(unsigned char nr)
{
    return nr ? C_REJ1 : C_REJ0;
}

static int sendSupervision(unsigned char address, unsigned char control)
{
    unsigned char frame[SUPERVISION_FRAME_SIZE] = {
        FLAG, address, control, address ^ control, FLAG};
    return writeAll(frame, SUPERVISION_FRAME_SIZE);
}

// Lê uma trama delimitada por FLAG e remove o byte stuffing.
// Retorna 0 numa trama, 1 numa trama inválida, 2 em timeout e -1 em erro I/O.
static int readFrame(unsigned char *content, int capacity, int *contentLength,
                     int timeoutSeconds)
{
    unsigned char byte;
    int length = 0;
    int malformed = 0;
    int escaped = 0;
    struct timespec deadline;
    const struct timespec *deadlinePointer = NULL;
    if (timeoutSeconds > 0)
    {
        if (makeDeadline(&deadline, timeoutSeconds) < 0)
            return -1;
        deadlinePointer = &deadline;
    }

    while (1)
    {
        int result = readByteUntil(&byte, deadlinePointer);
        if (result < 0)
            return -1;
        if (result == 0)
            return 2;
        if (result == 1 && byte == FLAG)
            break;
    }

    while (1)
    {
        int result = readByteUntil(&byte, deadlinePointer);
        if (result < 0)
            return -1;
        if (result == 0)
            return 2;

        if (byte == FLAG)
        {
            if (length == 0 && !malformed)
                continue; // FLAGs consecutivos: continua à procura do conteúdo.
            *contentLength = length;
            return malformed || escaped ? 1 : 0;
        }

        if (escaped)
        {
            if (byte != 0x5E && byte != 0x5D)
                malformed = 1;
            if (length < capacity)
                content[length++] = byte ^ 0x20;
            else
                malformed = 1;
            escaped = 0;
        }
        else if (byte == 0x7D)
        {
            escaped = 1;
        }
        else if (length < capacity)
        {
            content[length++] = byte;
        }
        else
        {
            malformed = 1;
        }
    }
}

// Procura o FLAG inicial e lê o resto de uma trama de supervisão.
// Por agora, SET/UA têm tamanho fixo de cinco bytes.
// Retorna 0 numa trama completa, 1 em timeout e -1 em erro I/O.
static int readSupervisionFrameTimeout(unsigned char *frame, int timeoutSeconds)
{
    unsigned char content[3];
    int contentLength = 0;
    int result = readFrame(content, sizeof(content), &contentLength, timeoutSeconds);
    if (result == 2)
        return 1; // Timeout.
    if (result != 0 || contentLength != 3)
        return result < 0 ? -1 : 1;

    frame[0] = FLAG;
    frame[1] = content[0];
    frame[2] = content[1];
    frame[3] = content[2];
    frame[4] = FLAG;
    return 0;
}

static int readSupervisionFrame(unsigned char *frame)
{
    int result;
    do
    {
        result = readSupervisionFrameTimeout(frame, 0);
    } while (result == 1);
    return result;
}

static int closeLinkOnError(void)
{
    closeSerialPort();
    linkIsOpen = 0;
    return -1;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters)
{
    if (linkIsOpen || llParameters.timeout <= 0 || llParameters.nRetransmissions < 0)
        return -1;

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }
    linkIsOpen = 1;
    linkIsTx = 1;
    activeParameters = llParameters;
    nextTxSequence = 0;
    expectedRxSequence = 0;

    printf("Serial port %s opened\n", llParameters.serialPort);

    // SET é repetido se não chegar um UA válido antes do timeout.
    unsigned char setFrame[SUPERVISION_FRAME_SIZE] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};
    for (int attempt = 0; attempt <= llParameters.nRetransmissions; attempt++)
    {
        if (writeAll(setFrame, SUPERVISION_FRAME_SIZE) < 0)
        {
            printf("Erro ao enviar a trama SET.\n");
            return closeLinkOnError();
        }
        printf("SET enviada (tentativa %d/%d).\n", attempt + 1,
               llParameters.nRetransmissions + 1);

        // Lê e sincroniza com o FLAG inicial da resposta UA.
        unsigned char uaFrame[SUPERVISION_FRAME_SIZE];
        int result = readSupervisionFrameTimeout(uaFrame, llParameters.timeout);
        if (result < 0)
        {
            perror("readByteSerialPort");
            return closeLinkOnError();
        }
        if (result == 0 && uaFrame[1] == A_TX && uaFrame[2] == C_UA &&
            uaFrame[3] == (A_TX ^ C_UA) && uaFrame[4] == FLAG)
        {
            printf("Ligacao estabelecida no transmissor.\n");
            return 0;
        }

        printf("Timeout ou UA invalido; vou repetir SET se ainda houver tentativas.\n");
    }

    printf("Nao foi possivel estabelecer a ligacao: limite de tentativas atingido.\n");
    return closeLinkOnError();
}

int llOpenRx(LinkLayer llParameters)
{
    if (linkIsOpen || llParameters.timeout <= 0 || llParameters.nRetransmissions < 0)
        return -1;

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }
    linkIsOpen = 1;
    linkIsTx = 0;
    activeParameters = llParameters;
    nextTxSequence = 0;
    expectedRxSequence = 0;

    printf("Serial port %s opened\n", llParameters.serialPort);

    // O recetor sincroniza com o FLAG inicial da trama SET.
    unsigned char setFrame[SUPERVISION_FRAME_SIZE];
    if (readSupervisionFrame(setFrame) < 0)
    {
        perror("readByteSerialPort");
        return closeLinkOnError();
    }

    printf("SET recebida: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", setFrame[0], setFrame[1], setFrame[2], setFrame[3], setFrame[4]);

    // Confirma se o que recebeu é uma trama SET válida.
    if (setFrame[0] != FLAG || setFrame[1] != A_TX || setFrame[2] != C_SET ||
        setFrame[3] != (A_TX ^ C_SET) || setFrame[4] != FLAG)
    {
        printf("Erro: a trama recebida nao e um SET valido.\n");
        return closeLinkOnError();
    }

    // Se o SET estiver certo, responde com UA.
    unsigned char uaFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_TX, C_UA, A_TX ^ C_UA, FLAG};

    if (writeAll(uaFrame, SUPERVISION_FRAME_SIZE) < 0)
    {
        printf("Erro ao enviar a trama UA.\n");
        return closeLinkOnError();
    }
    printf("UA enviada: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", uaFrame[0], uaFrame[1], uaFrame[2], uaFrame[3], uaFrame[4]);

    printf("Ligacao estabelecida no recetor.\n");

    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize)
{
    if (!linkIsOpen || !linkIsTx || buf == NULL ||
        bufSize < 0 || bufSize > MAX_PAYLOAD_SIZE)
        return -1;

    unsigned char unstuffed[MAX_PAYLOAD_SIZE + 4];
    unsigned char txFrame[2 * (MAX_PAYLOAD_SIZE + 4) + 2];
    unsigned char reply[MAX_FRAME_CONTENT_SIZE];
    int unstuffedLength = 4 + bufSize;

    unstuffed[0] = A_TX;
    unstuffed[1] = nextTxSequence ? 0x80 : 0x00;
    unstuffed[2] = unstuffed[0] ^ unstuffed[1];
    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++)
    {
        unstuffed[3 + i] = buf[i];
        bcc2 ^= buf[i];
    }
    unstuffed[3 + bufSize] = bcc2;

    int txLength = 0;
    txFrame[txLength++] = FLAG;
    for (int i = 0; i < unstuffedLength; i++)
        appendStuffedByte(unstuffed[i], txFrame, &txLength);
    txFrame[txLength++] = FLAG;

    for (int attempt = 0; attempt <= activeParameters.nRetransmissions; attempt++)
    {
        if (writeAll(txFrame, txLength) < 0)
            return -1;

        while (1)
        {
            int replyLength = 0;
            int result = readFrame(reply, MAX_FRAME_CONTENT_SIZE, &replyLength,
                                   activeParameters.timeout);
            if (result < 0)
                return -1;
            if (result == 2)
                break; // Timeout: retransmite a trama I.
            if (result > 0 || replyLength != 3 || reply[0] != A_TX ||
                reply[2] != (reply[0] ^ reply[1]))
                continue;

            if (reply[1] == rrControl((unsigned char)(nextTxSequence ^ 1)))
            {
                nextTxSequence ^= 1;
                return bufSize;
            }
            if (reply[1] == rejControl(nextTxSequence))
                break; // REJ: volta ao ciclo externo e reenvia a mesma trama I.
        }
    }

    printf("llSend: limite de retransmissoes atingido.\n");
    return -1;

}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet)
{
    if (!linkIsOpen || linkIsTx || packet == NULL)
        return -1;

    unsigned char frame[MAX_FRAME_CONTENT_SIZE];
    while (1)
    {
        int length = 0;
        int result = readFrame(frame, MAX_FRAME_CONTENT_SIZE, &length, 0);
        if (result < 0)
            return -1;
        if (result > 0)
            continue;

        // Se o UA do llOpenRx se perdeu, o Tx repete SET. Responde novamente.
        if (length == 3 && frame[0] == A_TX && frame[1] == C_SET &&
            frame[2] == (frame[0] ^ frame[1]))
        {
            if (sendSupervision(A_TX, C_UA) < 0)
                return -1;
            continue;
        }

        if (length < 4 || frame[0] != A_TX ||
            frame[2] != (frame[0] ^ frame[1]))
            continue; // Cabeçalho inválido ou trama malformada: ignora.

        unsigned char ns;
        if (frame[1] == 0x00)
            ns = 0;
        else if (frame[1] == 0x80)
            ns = 1;
        else
            continue;

        int dataLength = length - 4; // A, C, BCC1 e BCC2 estão fora dos dados.
        unsigned char bcc2 = 0;
        for (int i = 0; i < dataLength; i++)
            bcc2 ^= frame[3 + i];

        if (bcc2 != frame[length - 1])
        {
            if (ns == expectedRxSequence)
            {
                if (sendSupervision(A_TX, rejControl(expectedRxSequence)) < 0)
                    return -1;
            }
            else if (sendSupervision(A_TX, rrControl(expectedRxSequence)) < 0)
            {
                return -1;
            }
            continue;
        }

        if (dataLength > MAX_PAYLOAD_SIZE)
            continue;

        if (ns != expectedRxSequence)
        {
            // Duplicado: não entrega os dados outra vez, mas confirma o estado atual.
            if (sendSupervision(A_TX, rrControl(expectedRxSequence)) < 0)
                return -1;
            continue;
        }

        for (int i = 0; i < dataLength; i++)
            packet[i] = frame[3 + i];
        expectedRxSequence ^= 1;
        if (sendSupervision(A_TX, rrControl(expectedRxSequence)) < 0)
            return -1;
        return dataLength;
    }

}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    if (!linkIsOpen || !linkIsTx)
        return -1;

    // Tx inicia o fecho com DISC; Rx responde com DISC e Tx termina com UA.
    unsigned char discFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};
    unsigned char receivedFrame[SUPERVISION_FRAME_SIZE];
    int gotReceiverDisc = 0;
    for (int attempt = 0; attempt <= activeParameters.nRetransmissions; attempt++)
    {
        if (writeAll(discFrame, SUPERVISION_FRAME_SIZE) < 0)
        {
            printf("Erro ao enviar a trama DISC.\n");
            return closeLinkOnError();
        }

        int result = readSupervisionFrameTimeout(receivedFrame, activeParameters.timeout);
        if (result < 0)
        {
            perror("readByteSerialPort");
            return closeLinkOnError();
        }
        if (result == 0 && receivedFrame[1] == A_RX && receivedFrame[2] == C_DISC &&
            receivedFrame[3] == (A_RX ^ C_DISC) && receivedFrame[4] == FLAG)
        {
            gotReceiverDisc = 1;
            break;
        }
        printf("Timeout ou DISC invalido; vou repetir DISC se ainda houver tentativas.\n");
    }
    if (!gotReceiverDisc)
    {
        printf("Fecho falhou: limite de tentativas atingido.\n");
        return closeLinkOnError();
    }

    unsigned char uaFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};
    if (writeAll(uaFrame, SUPERVISION_FRAME_SIZE) < 0)
    {
        printf("Erro ao enviar a trama UA.\n");
        return closeLinkOnError();
    }

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        linkIsOpen = 0;
        return -1;
    }
    linkIsOpen = 0;
    printf("Ligacao fechada no transmissor.\n");

    return 0;
}

int llCloseRx()
{
    if (!linkIsOpen || linkIsTx)
        return -1;

    // Rx recebe o DISC do Tx, responde com DISC e espera o UA final.
    unsigned char receivedFrame[SUPERVISION_FRAME_SIZE];
    if (readSupervisionFrame(receivedFrame) < 0)
    {
        perror("readByteSerialPort");
        return closeLinkOnError();
    }
    if (receivedFrame[1] != A_TX || receivedFrame[2] != C_DISC ||
        receivedFrame[3] != (A_TX ^ C_DISC) || receivedFrame[4] != FLAG)
    {
        printf("Erro: a trama recebida nao e um DISC valido.\n");
        return closeLinkOnError();
    }

    unsigned char discFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};
    for (int attempt = 0; attempt <= activeParameters.nRetransmissions; attempt++)
    {
        if (writeAll(discFrame, SUPERVISION_FRAME_SIZE) < 0)
        {
            printf("Erro ao enviar a trama DISC.\n");
            return closeLinkOnError();
        }

        int result = readSupervisionFrameTimeout(receivedFrame, activeParameters.timeout);
        if (result < 0)
        {
            perror("readByteSerialPort");
            return closeLinkOnError();
        }
        if (result == 0 && receivedFrame[1] == A_RX && receivedFrame[2] == C_UA &&
            receivedFrame[3] == (A_RX ^ C_UA) && receivedFrame[4] == FLAG)
            break;

        if (attempt == activeParameters.nRetransmissions)
        {
            printf("Fecho falhou: limite de tentativas atingido.\n");
            return closeLinkOnError();
        }
        printf("Timeout ou UA invalido; vou repetir DISC.\n");
    }

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        linkIsOpen = 0;
        return -1;
    }
    linkIsOpen = 0;
    printf("Ligacao fechada no recetor.\n");

    return 0;
}
