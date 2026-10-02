// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "frame.h"
#include "serial_port.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t alarmCount = 0;

static void alarmHandler(int signal)
{
    (void)signal;
    alarmCount++;
}

static int configureAlarmHandler(void)
{
    struct sigaction action = {0};
    action.sa_handler = alarmHandler;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGALRM, &action, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    }

    return 0;
}

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

    if (configureAlarmHandler() < 0)
    {
        closeSerialPort();
        return -1;
    }

    // O transmissor envia SET para pedir a ligacao.
    unsigned char setFrame[SUPERVISION_FRAME_SIZE] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};
    int connectionEstablished = FALSE;
    int totalAttempts = llParameters.nRetransmissions + 1;

    alarmCount = 0;

    for (int attempt = 1; attempt <= totalAttempts; attempt++)
    {
        printf("Sending SET (attempt %d of %d): 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
               attempt, totalAttempts, setFrame[0], setFrame[1], setFrame[2], setFrame[3], setFrame[4]);

        int receivedFrame = sendFrameTX(setFrame, SUPERVISION_FRAME_SIZE, llParameters.timeout);

        if (receivedFrame == FRAME_OK)
        {
            connectionEstablished = TRUE;
            printf("UA received. Connection established at transmitter.\n");
            break;
        }

        if (receivedFrame == FRAME_TIMEOUT)
        {
            printf("Timeout. SET will be retransmitted (alarm #%d).\n", (int)alarmCount);
            continue;
        }

        closeSerialPort();
        return -1;
    }

    if (!connectionEstablished)
    {
        printf("Could not establish connection after %d attempts.\n", totalAttempts);
        closeSerialPort();
        return -1;
    }

    // Fecha a porta depois de a ligacao estar confirmada.
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

    // A maquina de estados só termina quando receber uma SET valida.
    if (receiveSupervisionFrame(A_TX, C_SET) != FRAME_OK)
    {
        closeSerialPort();
        return -1;
    }

    printf("Valid SET received.\n");

    // Se o SET estiver certo, responde com UA.
    unsigned char uaFrame[SUPERVISION_FRAME_SIZE] = {FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};

    int bytesWritten = writeBytesSerialPort(uaFrame, SUPERVISION_FRAME_SIZE);
    if (bytesWritten != SUPERVISION_FRAME_SIZE)
    {
        printf("Error sending UA frame.\n");
        closeSerialPort();
        return -1;
    }
    printf("UA sent: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", uaFrame[0], uaFrame[1], uaFrame[2], uaFrame[3], uaFrame[4]);

    printf("Connection established at receiver.\n");

    // Fechar serial port após a comunicação
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
