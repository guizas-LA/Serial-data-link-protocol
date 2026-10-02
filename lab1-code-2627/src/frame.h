// Funcoes e valores usados nas tramas da camada de ligacao.

#ifndef FRAME_H
#define FRAME_H

#define FLAG 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_UA 0x07
#define SUPERVISION_FRAME_SIZE 5

#define FRAME_OK 0
#define FRAME_TIMEOUT 1
#define FRAME_ERROR -1

// Envia uma trama, ativa o alarme e espera pela resposta UA.
int sendFrameTX(const unsigned char frame[], int size, int timeout);

int receiveSupervisionFrame(unsigned char expectedAddress, unsigned char expectedControl);

#endif
