// RCOM 2026/2027
//
// Application layer protocol implementation

#include "frame.h"
#include "serial_port.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Envia uma trama e espera pela resposta UA ate ocorrer timeout. */
int sendFrameTX(const unsigned char frame[], int size, int timeout)
{
    int totalWritten = 0;

    /* write() pode enviar menos bytes do que os pedidos. */
    while (totalWritten < size)
    {
        int bytesWritten = writeBytesSerialPort(frame + totalWritten,
                                            size - totalWritten);

        if (bytesWritten < 0)
        {
            if (errno == EINTR)
                continue;

            perror("writeBytesSerialPort");
            return FRAME_ERROR;
        }

        if (bytesWritten == 0)
        {
            fprintf(stderr, "Nao foi possivel enviar a trama completa.\n");
            return FRAME_ERROR;
        }

        totalWritten += bytesWritten;
    }

    alarm((unsigned int)timeout);
    int receivedFrame = receiveSupervisionFrame(A_RX, C_UA);
    alarm(0);

    return receivedFrame;
}

typedef enum
{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK
} FrameState;

/* Le bytes ate encontrar uma trama com endereco, controlo e BCC validos. */
int receiveSupervisionFrame(unsigned char expectedAddress,
                            unsigned char expectedControl)
{
    FrameState state = START;
    unsigned char byte;

    while (1)
    {
        int bytesRead = readByteSerialPort(&byte);

        if (bytesRead < 0)
        {
            /* No transmissor, EINTR indica que terminou o timeout. */
            if (errno == EINTR)
                return FRAME_TIMEOUT;

            perror("readByteSerialPort");
            return FRAME_ERROR;
        }

        if (bytesRead == 0)
            continue;

        switch (state)
        {
        case START:
            if (byte == FLAG)
                state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if (byte == FLAG)
                state = FLAG_RCV;
            else if (byte == expectedAddress)
                state = A_RCV;
            else
                state = START;
            break;

        case A_RCV:
            if (byte == FLAG)
                state = FLAG_RCV;
            else if (byte == expectedControl)
                state = C_RCV;
            else
                state = START;
            break;

        case C_RCV:
            if (byte == (unsigned char)(expectedAddress ^ expectedControl))
                state = BCC_OK;
            else if (byte == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;

        case BCC_OK:
            if (byte == FLAG)
                return FRAME_OK;

            /* A trama era invalida; volta a procurar o inicio. */
            state = START;
            break;
        }
    }
}
