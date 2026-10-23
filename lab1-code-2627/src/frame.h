// RCOM 2026/2027
//
// Frame building, sending and receiving

#ifndef FRAME_H
#define FRAME_H

#include "link_layer.h"

#define FLAG 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_DISC 0x0B
#define C_RR(nr) (0xAA | (nr))
#define C_REJ(nr) (0x54 | (nr))
#define C_I(ns) ((ns) << 7)
#define C_UA 0x07
#define ESC 0x7D
#define ESC_XOR 0x20
#define SU_FRAME_SIZE 5
#define MAX_I_FRAME_SIZE (4 + 2 * (MAX_PAYLOAD_SIZE + 1) + 1)

typedef enum {
    FRAME_OK,
    FRAME_DATA_ERROR,
    FRAME_TIMEOUT,
    FRAME_IO_ERROR
} FrameResult;

typedef struct {
    unsigned char a;
    unsigned char c;
    unsigned char data[MAX_PAYLOAD_SIZE + 1];
    int dataSize;
} Frame;

const char *frameName(unsigned char c);

int isInformationFrame(unsigned char c);

int buildInformationFrame(unsigned char *frame, const unsigned char *data, int dataSize, unsigned char ns);

int writeFrame(const unsigned char *frame, int frameSize);

int sendSupervisionFrame(unsigned char a, unsigned char c);

// Reads one frame from the serial port; returns FRAME_TIMEOUT when the alarm fires
FrameResult readFrame(Frame *frame);

int receiveSupervisionFrame(unsigned char a, unsigned char c);

#endif // FRAME_H
