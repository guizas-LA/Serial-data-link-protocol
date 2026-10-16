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

static volatile sig_atomic_t alarmCount = 0;

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

/* Abre a porta e estabelece a ligaction: envia SET e espera por UA. */
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

    /* Formato: FLAG, endereco, controlo, BCC1, FLAG. */
    const unsigned char setFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_TX, C_SET, (unsigned char)(A_TX ^ C_SET), FLAG
    };

    int ligactionEstabelecida = FALSE;
    int totalAttempts = llParameters.nRetransmissions + 1;

    for (int attempt = 1; attempt <= totalAttempts; attempt++)
    {
        printf("TX: envio de SET (attempt %d/%d).\n",
               attempt, totalAttempts);

        alarmCount = 0;
        int receivedFrame = sendFrameTX(setFrame, SUPERVISION_FRAME_SIZE,
                                    llParameters.timeout);

        if (receivedFrame == FRAME_OK)
        {
            printf("TX: trama UA recebida; ligaction estabelecida.\n");
            ligactionEstabelecida = TRUE;
            break;
        }

        if (receivedFrame == FRAME_TIMEOUT)
        {
            printf("TX: tempo de espera esgotado.\n");
            continue;
        }

        fprintf(stderr, "TX: erro durante o estabelecimento da ligaction.\n");
        closeSerialPort();
        return -1;
    }

    if (!ligactionEstabelecida)
    {
        fprintf(stderr, "TX: nao foi possivel estabelecer a ligaction.\n");
        closeSerialPort();
        return -1;
    }

    /* A porta permanece aberta ate llCloseTx(). */
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

    printf("RX: trama SET valida recebida.\n");

    const unsigned char uaFrame[SUPERVISION_FRAME_SIZE] = {
        FLAG, A_RX, C_UA, (unsigned char)(A_RX ^ C_UA), FLAG
    };

    int bytesWritten = writeBytesSerialPort(uaFrame, SUPERVISION_FRAME_SIZE);
    if (bytesWritten != SUPERVISION_FRAME_SIZE)
    {
        fprintf(stderr, "RX: erro ao enviar a trama UA.\n");
        closeSerialPort();
        return -1;
    }

    printf("RX: trama UA enviada; ligaction estabelecida.\n");

    /* A porta permanece aberta ate llCloseRx(). */
    return 0;
}

/* Estas funcoes pertencem ao M3 e ficam por implementar nesta etapa. */
int llSend(const unsigned char *buf, int bufSize)
{
    (void)buf;
    (void)bufSize;
    fprintf(stderr, "llSend ainda nao foi implementada (M3).\n");
    return -1;
}

int llReceive(unsigned char *packet)
{
    (void)packet;
    fprintf(stderr, "llReceive ainda nao foi implementada (M3).\n");
    return -1;
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
