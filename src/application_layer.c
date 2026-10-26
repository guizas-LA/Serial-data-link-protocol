// RCOM 2026/2027
//
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>

#define DATA_SIZE (MAX_PAYLOAD_SIZE - 3)   
#define C_START 1
#define C_DATA  2
#define C_END   3

#define T_SIZE  0

static int buildControlPacket(unsigned char *pkt, unsigned char c, long fileSize)
{
    int i = 0;
    pkt[i++] = c;                          

    pkt[i++] = T_SIZE;
    pkt[i++] = 4;
    pkt[i++] = (fileSize >> 24) & 0xFF;
    pkt[i++] = (fileSize >> 16) & 0xFF;
    pkt[i++] = (fileSize >> 8) & 0xFF;
    pkt[i++] = fileSize & 0xFF;

    return i;                              
}


static long parseFileSize(const unsigned char *pkt, int len)
{
    int i = 1;                             
    while (i + 1 < len)
    {
        unsigned char t = pkt[i];
        unsigned char l = pkt[i + 1];
        if (i + 2 + l > len)               
            break;

        if (t == T_SIZE)
        {
            long size = 0;
            for (int j = 0; j < l; j++)    
                size = size * 256 + pkt[i + 2 + j];
            return size;
        }

        i += 2 + l;                        
    }
    return -1;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // ----------------------------------------------------
    // TODO: Adapt and extend this code according to the specifications of the project.
    // ----------------------------------------------------

    LinkLayer llParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout,
    };
    strcpy(llParameters.serialPort, serialPort);

    if (strcmp(role, "tx") == 0)
    {
        if (llOpenTx(llParameters) < 0)
        {
            printf("llOpenTx falhou\n");
            return;
        }

        FILE *file = fopen(filename, "rb");
        if (file == NULL)
        {
            perror("fopen");
            llCloseTx();
            return;
        }
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        rewind(file);

        unsigned char packet[MAX_PAYLOAD_SIZE];

        int len = buildControlPacket(packet, C_START, fileSize);
        if (llSend(packet, len) < 0)
        {
            printf("Falha ao enviar o pacote START\n");
            fclose(file);
            return;
        }

        int k;
        while ((k = fread(packet + 3, 1, DATA_SIZE, file)) > 0)
        {
            packet[0] = C_DATA;
            packet[1] = k / 256;   // L2
            packet[2] = k % 256;   // L1
            if (llSend(packet, k + 3) < 0)
            {
                printf("Falha ao enviar o pacote DATA\n");
                fclose(file);
                return;
            }
        }

        len = buildControlPacket(packet, C_END, fileSize);
        if (llSend(packet, len) < 0)
        {
            printf("Falha ao enviar o pacote END\n");
            fclose(file);
            return;
        }

        fclose(file);
        llCloseTx();
    }
        else if (strcmp(role, "rx") == 0)
    {
        if (llOpenRx(llParameters) < 0)
        {
            printf("llOpenRx falhou\n");
            return;
        }

        unsigned char packet[MAX_PAYLOAD_SIZE];
        FILE *file = NULL;
        long expectedSize = -1;
        long receivedBytes = 0;

        while (1)
        {
            int n = llReceive(packet);
            if (n < 0)
            {
                printf("llReceive falhou\n");
                if (file != NULL)
                    fclose(file);
                return;
            }
            if (n == 0)
                continue;

            if (packet[0] == C_START)
            {
                expectedSize = parseFileSize(packet, n);
                printf("START recebido: tamanho do ficheiro = %ld bytes\n", expectedSize);

                file = fopen(filename, "wb");
                if (file == NULL)
                {
                    perror("fopen");
                    return;
                }
            }
            else if (packet[0] == C_DATA)
            {
                if (file == NULL)
                {
                    printf("Pacote DATA recebido antes do START, ignorado\n");
                    continue;
                }

                int k = packet[1] * 256 + packet[2];
                if (k + 3 != n)
                {
                    printf("Pacote DATA com tamanho errado (L2 L1 = %d, recebidos %d), ignorado\n", k, n - 3);
                    continue;
                }

                fwrite(packet + 3, 1, k, file);
                receivedBytes += k;
            }
            else if (packet[0] == C_END)
            {
                long endSize = parseFileSize(packet, n);
                if (endSize != expectedSize)
                    printf("Aviso: tamanho no END (%ld) diferente do tamanho no START (%ld)\n", endSize, expectedSize);
                break;
            }
            else
            {
                printf("Tipo de pacote desconhecido 0x%02X, ignorado\n", packet[0]);
            }
        }

        if (file != NULL)
            fclose(file);

        // Check that the whole file was received
        if (receivedBytes == expectedSize)
            printf("Ficheiro recebido com sucesso: %ld bytes\n", receivedBytes);
        else
            printf("Tamanho do ficheiro não coincide: esperados %ld bytes, recebidos %ld\n", expectedSize, receivedBytes);

        llCloseRx();
    }
    else
    {
        printf("Papel inválido: %s. Deve ser 'tx' ou 'rx'.\n", role);
        return;
    }
}
