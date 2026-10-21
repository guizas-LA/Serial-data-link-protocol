// RCOM 2026/2027
//
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <string.h>

#define TEST_MILESTONE 3

#define NUMBER_OF_TEST_PACKETS 6

static const int testPacketSizes[NUMBER_OF_TEST_PACKETS] = {1, 2, 50, 300, 7, MAX_PAYLOAD_SIZE};

/* M1: o Tx escreve uma string na porta serie e o Rx le-a. */
static int testM1(int isTx, const char *serialPort, int baudRate)
{
    if (openSerialPort(serialPort, baudRate) < 0)
        return -1;

    if (isTx)
    {
        const char *message = "Hello RCOM\n";
        int written = writeBytesSerialPort((const unsigned char *)message, (int)strlen(message));
        printf("TX: %d bytes written.\n", written);
    }
    else
    {
        unsigned char byte;
        printf("RX: received string: ");
        while (1)
        {
            int result = readByteSerialPort(&byte);
            if (result < 0)
                break;
            if (result == 0)
                continue;
            printf("%c", byte);
            if (byte == '\n')
                break;
        }
    }

    closeSerialPort();
    return 0;
}

/* M2: apenas abre e fecha a ligacao (SET / UA). */
static int testM2(int isTx, LinkLayer llParameters)
{
    int result;

    if (isTx)
        result = llOpenTx(llParameters);
    else
        result = llOpenRx(llParameters);

    if (result < 0)
        return -1;

    if (isTx)
        return llCloseTx();
    return llCloseRx();
}

/* Preenche um pacote de teste. Os primeiros bytes sao os valores da flag e do
   escape, para testar o byte stuffing. */
static void fillTestPacket(unsigned char *packet, int size, int number)
{
    for (int i = 0; i < size; i++)
        packet[i] = (unsigned char)(number * 37 + i * 11);

    if (size > 0)
        packet[0] = 0x7E;
    if (size > 1)
        packet[1] = 0x7D;
}

/* M3: o Tx envia pacotes de teste com llSend e o Rx recebe-os com llReceive. */
static int testM3(int isTx, LinkLayer llParameters)
{
    unsigned char packet[MAX_PAYLOAD_SIZE];
    unsigned char expected[MAX_PAYLOAD_SIZE];

    if (isTx)
    {
        if (llOpenTx(llParameters) < 0)
            return -1;

        for (int i = 0; i < NUMBER_OF_TEST_PACKETS; i++)
        {
            fillTestPacket(packet, testPacketSizes[i], i);
            int written = llSend(packet, testPacketSizes[i]);
            if (written != testPacketSizes[i])
            {
                fprintf(stderr, "TX: llSend failed on packet %d.\n", i);
                llCloseTx();
                return -1;
            }
            printf("TX: packet %d sent (%d bytes).\n", i, written);
        }

        return llCloseTx();
    }

    if (llOpenRx(llParameters) < 0)
        return -1;

    for (int i = 0; i < NUMBER_OF_TEST_PACKETS; i++)
    {
        int size = llReceive(packet);
        fillTestPacket(expected, testPacketSizes[i], i);

        if (size != testPacketSizes[i] || memcmp(packet, expected, (size_t)size) != 0)
        {
            fprintf(stderr, "RX: packet %d is different from the expected one.\n", i);
            llCloseRx();
            return -1;
        }
        printf("RX: packet %d received (%d bytes) - OK.\n", i, size);
    }

    return llCloseRx();
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    (void)filename;

    LinkLayer llParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout
    };
    snprintf(llParameters.serialPort, sizeof(llParameters.serialPort), "%s", serialPort);

    int isTx;
    if (strcmp(role, "tx") == 0)
        isTx = 1;
    else if (strcmp(role, "rx") == 0)
        isTx = 0;
    else
    {
        fprintf(stderr, "Invalid role: use 'tx' or 'rx'.\n");
        return;
    }

    int result;
    if (TEST_MILESTONE == 1)
        result = testM1(isTx, serialPort, baudRate);
    else if (TEST_MILESTONE == 2)
        result = testM2(isTx, llParameters);
    else
        result = testM3(isTx, llParameters);

    if (result == 0)
        printf("Test M%d finished successfully.\n", TEST_MILESTONE);
    else
        fprintf(stderr, "Test M%d failed.\n", TEST_MILESTONE);
}
