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
#define SUPERVISION_FRAME_SIZE 5

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    // O transmissor envia SET para pedir a ligação.
    unsigned char setFrame[SUPERVISION_FRAME_SIZE] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};

    int bytesWritten = writeBytesSerialPort(setFrame, SUPERVISION_FRAME_SIZE);
    if (bytesWritten != SUPERVISION_FRAME_SIZE)
    {
        printf("Erro ao enviar a trama SET.\n");
        closeSerialPort();
        return -1;
    }
    printf("SET enviada: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", setFrame[0], setFrame[1], setFrame[2], setFrame[3], setFrame[4]);

    // Lê os cinco bytes da resposta UA.
    unsigned char uaFrame[SUPERVISION_FRAME_SIZE];
    int bytesReceived = 0;
    while (bytesReceived < SUPERVISION_FRAME_SIZE)
    {
        int result = readByteSerialPort(&uaFrame[bytesReceived]);
        if (result < 0)
        {
            perror("readByteSerialPort");
            closeSerialPort();
            return -1;
        }
        if (result == 1)
        {
            bytesReceived++;
        }
    }

    printf("Resposta recebida: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", uaFrame[0], uaFrame[1], uaFrame[2], uaFrame[3], uaFrame[4]);

    // Confirma se a resposta recebida é um UA válido.
    if (uaFrame[0] != FLAG || uaFrame[1] != A_RX || uaFrame[2] != C_UA ||
        uaFrame[3] != (A_RX ^ C_UA) || uaFrame[4] != FLAG)
    {
        printf("Erro: a trama recebida nao e um UA valido.\n");
        closeSerialPort();
        return -1;
    }

    printf("Ligacao estabelecida no transmissor.\n");

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return 0;
}

int llOpenRx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    // O recetor espera pelos cinco bytes da trama SET.
    unsigned char setFrame[SUPERVISION_FRAME_SIZE];
    int bytesReceived = 0;
    while (bytesReceived < SUPERVISION_FRAME_SIZE)
    {
        int result = readByteSerialPort(&setFrame[bytesReceived]);
        if (result < 0)
        {
            perror("readByteSerialPort");
            closeSerialPort();
            return -1;
        }
        if (result == 1)
        {
            bytesReceived++;
        }
    }

    printf("SET recebida: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", setFrame[0], setFrame[1], setFrame[2], setFrame[3], setFrame[4]);

    // Confirma se o que recebeu é uma trama SET válida.
    if (setFrame[0] != FLAG || setFrame[1] != A_TX || setFrame[2] != C_SET ||
        setFrame[3] != (A_TX ^ C_SET) || setFrame[4] != FLAG)
    {
        printf("Erro: a trama recebida nao e um SET valido.\n");
        closeSerialPort();
        return -1;
    }

    // Se o SET estiver certo, responde com UA.
    unsigned char uaFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};

    int bytesWritten = writeBytesSerialPort(uaFrame, SUPERVISION_FRAME_SIZE);
    if (bytesWritten != SUPERVISION_FRAME_SIZE)
    {
        printf("Erro ao enviar a trama UA.\n");
        closeSerialPort();
        return -1;
    }
    printf("UA enviada: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", uaFrame[0], uaFrame[1], uaFrame[2], uaFrame[3], uaFrame[4]);

    printf("Ligacao estabelecida no recetor.\n");

    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

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
    // TODO: Implement this function

    return 0;
}

int llCloseRx()
{
    // TODO: Implement this function

    return 0;
}
