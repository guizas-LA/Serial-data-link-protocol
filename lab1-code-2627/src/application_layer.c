// RCOM 2026/2027


#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>

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

    int result;

    if (strcmp(role, "tx") == 0)
    {
        result = llOpenTx(llParameters);
        if (result == 0)
            llCloseTx();
    }
    else if (strcmp(role, "rx") == 0)
    {
        result = llOpenRx(llParameters);
        if (result == 0)
            llCloseRx();
    }
    else
    {
        fprintf(stderr, "Role invalido: use 'tx' ou 'rx'.\n");
        return;
    }

    if (result == 0)
        printf("Teste M2 concluido com sucesso.\n");
    else
        fprintf(stderr, "Teste M2 falhou.\n");
}
