#include "frame.h"
#include "serial_port.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

// Envia uma trama SET e espera pela resposta UA do recetor.
int sendFrameTX(const unsigned char frame[], int size, int timeout)
{
    int bytesWritten = writeBytesSerialPort(frame, size);

    if (bytesWritten != size)
    {
        printf("Error sending frame.\n");
        return FRAME_ERROR;
    }

    alarm(timeout);
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

// Lê bytes ate receber uma trama valida com os campos esperados.
int receiveSupervisionFrame(unsigned char expectedAddress, unsigned char expectedControl)
{
    FrameState state = START;
    unsigned char byte;

    while (1)
    {
        int bytesRead = readByteSerialPort(&byte);

        if (bytesRead < 0)
        {
            if (errno == EINTR)
            {
                return FRAME_TIMEOUT;
            }

            perror("readByteSerialPort");
            return FRAME_ERROR;
        }

        if (bytesRead == 0)
        {
            continue;
        }

        switch (state)
        {
        case START:
            state = (byte == FLAG) ? FLAG_RCV : START;
            break;

        case FLAG_RCV:
            if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            else if (byte == expectedAddress)
            {
                state = A_RCV;
            }
            else
            {
                state = START;
            }
            break;

        case A_RCV:
            if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            else if (byte == expectedControl)
            {
                state = C_RCV;
            }
            else
            {
                state = START;
            }
            break;

        case C_RCV:
            if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            else if (byte == (unsigned char)(expectedAddress ^ expectedControl))
            {
                state = BCC_OK;
            }
            else
            {
                state = START;
            }
            break;

        case BCC_OK:
            if (byte == FLAG)
            {
                return FRAME_OK;
            }
            state = START;
            break;
        }
    }
}
