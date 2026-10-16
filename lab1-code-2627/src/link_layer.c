// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>

// Valores usados nas tramas SET e UA.
#define FLAG 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define SUPERVISION_FRAME_SIZE 5

// A porta fica aberta entre llOpen*() e llClose*().
static int linkIsOpen = 0;

// Procura o FLAG inicial e lê o resto de uma trama de supervisão.
// Por agora, SET/UA têm tamanho fixo de cinco bytes.
static int readSupervisionFrame(unsigned char *frame)
{
    unsigned char byte;

    while (1)
    {
        int result = readByteSerialPort(&byte);
        if (result < 0)
            return -1;
        if (result == 1 && byte == FLAG)
            break;
    }

    frame[0] = FLAG;
    for (int i = 1; i < SUPERVISION_FRAME_SIZE; i++)
    {
        int result;
        do
        {
            result = readByteSerialPort(&frame[i]);
            if (result < 0)
                return -1;
        } while (result == 0);
    }

    return 0;
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
    if (linkIsOpen)
        return -1;

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }
    linkIsOpen = 1;

    printf("Serial port %s opened\n", llParameters.serialPort);

    // O transmissor envia SET para pedir a ligação.
    unsigned char setFrame[SUPERVISION_FRAME_SIZE] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};

    int bytesWritten = writeBytesSerialPort(setFrame, SUPERVISION_FRAME_SIZE);
    if (bytesWritten != SUPERVISION_FRAME_SIZE)
    {
        printf("Erro ao enviar a trama SET.\n");
        return closeLinkOnError();
    }
    printf("SET enviada: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", setFrame[0], setFrame[1], setFrame[2], setFrame[3], setFrame[4]);

    // Lê e sincroniza com o FLAG inicial da resposta UA.
    unsigned char uaFrame[SUPERVISION_FRAME_SIZE];
    if (readSupervisionFrame(uaFrame) < 0)
    {
        perror("readByteSerialPort");
        return closeLinkOnError();
    }

    printf("Resposta recebida: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", uaFrame[0], uaFrame[1], uaFrame[2], uaFrame[3], uaFrame[4]);

    // Confirma se a resposta recebida é um UA válido.
    if (uaFrame[0] != FLAG || uaFrame[1] != A_RX || uaFrame[2] != C_UA ||
        uaFrame[3] != (A_RX ^ C_UA) || uaFrame[4] != FLAG)
    {
        printf("Erro: a trama recebida nao e um UA valido.\n");
        return closeLinkOnError();
    }

    printf("Ligacao estabelecida no transmissor.\n");

    return 0;
}

int llOpenRx(LinkLayer llParameters)
{
    if (linkIsOpen)
        return -1;

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }
    linkIsOpen = 1;

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
        FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};

    int bytesWritten = writeBytesSerialPort(uaFrame, SUPERVISION_FRAME_SIZE);
    if (bytesWritten != SUPERVISION_FRAME_SIZE)
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
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    if (!linkIsOpen)
        return -1;

    // Tx inicia o fecho com DISC; Rx responde com DISC e Tx termina com UA.
    unsigned char discFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};
    if (writeBytesSerialPort(discFrame, SUPERVISION_FRAME_SIZE) != SUPERVISION_FRAME_SIZE)
    {
        printf("Erro ao enviar a trama DISC.\n");
        return closeLinkOnError();
    }

    unsigned char receivedFrame[SUPERVISION_FRAME_SIZE];
    if (readSupervisionFrame(receivedFrame) < 0)
    {
        perror("readByteSerialPort");
        return closeLinkOnError();
    }
    if (receivedFrame[1] != A_RX || receivedFrame[2] != C_DISC ||
        receivedFrame[3] != (A_RX ^ C_DISC) || receivedFrame[4] != FLAG)
    {
        printf("Erro: a trama recebida nao e um DISC valido.\n");
        return closeLinkOnError();
    }

    unsigned char uaFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_TX, C_UA, A_TX ^ C_UA, FLAG};
    if (writeBytesSerialPort(uaFrame, SUPERVISION_FRAME_SIZE) != SUPERVISION_FRAME_SIZE)
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
    if (!linkIsOpen)
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
    if (writeBytesSerialPort(discFrame, SUPERVISION_FRAME_SIZE) != SUPERVISION_FRAME_SIZE)
    {
        printf("Erro ao enviar a trama DISC.\n");
        return closeLinkOnError();
    }

    if (readSupervisionFrame(receivedFrame) < 0)
    {
        perror("readByteSerialPort");
        return closeLinkOnError();
    }
    if (receivedFrame[1] != A_TX || receivedFrame[2] != C_UA ||
        receivedFrame[3] != (A_TX ^ C_UA) || receivedFrame[4] != FLAG)
    {
        printf("Erro: a trama recebida nao e um UA valido.\n");
        return closeLinkOnError();
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
