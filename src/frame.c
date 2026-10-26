// RCOM 2026/2027
//
// Frame building, sending and receiving

#include "frame.h"
#include "alarm.h"
#include "serial_port.h"
#include <errno.h>
#include <stdio.h>

typedef enum {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC1_OK,
    DATA,
    ESC_RCV
} State;

////////////////////////////////////////////////
// FRAME HELPERS
////////////////////////////////////////////////

const char *frameName(unsigned char c) {
    switch (c) {
    case C_SET:
        return "SET";
    case C_UA:
        return "UA";
    case C_DISC:
        return "DISC";
    case C_RR(0):
        return "RR0";
    case C_RR(1):
        return "RR1";
    case C_REJ(0):
        return "REJ0";
    case C_REJ(1):
        return "REJ1";
    case C_I(0):
        return "I0";
    case C_I(1):
        return "I1";
    default:
        return "unknown frame";
    }
}

int isInformationFrame(unsigned char c) {
    return (c & 0x7F) == 0; 
}

static unsigned char calcBCC2(const unsigned char *data, int dataSize) {
    unsigned char bcc2 = 0;

    for (int i = 0; i < dataSize; i++)
        bcc2 ^= data[i];

    return bcc2;
}


static int stuffByte(unsigned char *dst, unsigned char byte) {
    if (byte == FLAG || byte == ESC)
    {
        dst[0] = ESC;
        dst[1] = byte ^ ESC_XOR;
        return 2;
    }

    dst[0] = byte;
    return 1;
}


int buildInformationFrame(unsigned char *frame, const unsigned char *data, int dataSize, unsigned char ns) {
    unsigned char c = C_I(ns);
    int size = 0;

    frame[size++] = FLAG;
    frame[size++] = A_TX;
    frame[size++] = c;
    frame[size++] = A_TX ^ c;

    for (int i = 0; i < dataSize; i++)
        size += stuffByte(frame + size, data[i]);

    size += stuffByte(frame + size, calcBCC2(data, dataSize));

    frame[size++] = FLAG;

    return size;
}

////////////////////////////////////////////////
// SENDING AND RECEIVING FRAMES
////////////////////////////////////////////////

int writeFrame(const unsigned char *frame, int frameSize) {
    int written = 0;

    while (written < frameSize) {
        int bytes = writeBytesSerialPort(frame + written, frameSize - written);
        if (bytes < 0)
            return -1;
        written += bytes;
    }

    return written;
}


int buildSupervisionFrame(unsigned char *frame, unsigned char a, unsigned char c) {
    frame[0] = FLAG;
    frame[1] = a;
    frame[2] = c;
    frame[3] = a ^ c;
    frame[4] = FLAG;

    return SU_FRAME_SIZE;
}


int sendSupervisionFrame(unsigned char a, unsigned char c) {
    unsigned char frame[SU_FRAME_SIZE];
    buildSupervisionFrame(frame, a, c);

    int bytes = writeFrame(frame, SU_FRAME_SIZE);

    printf("Enviado %s:", frameName(c));
    for (int i = 0; i < SU_FRAME_SIZE; i++)
        printf(" 0x%02X", frame[i]);
    printf(" (%d bytes)\n", bytes);

    return bytes;
}


static int addDataByte(Frame *frame, unsigned char byte) {
    if (frame->dataSize == sizeof(frame->data))
        return -1;

    frame->data[frame->dataSize++] = byte;
    return 0;
}


FrameResult readFrame(Frame *frame) {
    State state = START;

    while (TRUE) {
        if (alarmTriggered)
            return FRAME_TIMEOUT;

        unsigned char byte;
        int bytes = readByteSerialPort(&byte);
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            return FRAME_IO_ERROR;
        }
        if (bytes == 0)
            continue;

        switch (state) {
        case START:
            if (byte == FLAG)
                state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if (byte == A_TX || byte == A_RX) {
                frame->a = byte;
                state = A_RCV;
            }
            else if (byte != FLAG)
                state = START;
            break;

        case A_RCV:
            if (byte == FLAG)
                state = FLAG_RCV;
            else {
                frame->c = byte;
                state = C_RCV;
            }
            break;

        case C_RCV:
            if (byte == (frame->a ^ frame->c)) {
                frame->dataSize = 0;
                state = BCC1_OK;
            }
            else if (byte == FLAG)
                state = FLAG_RCV;
            else
                state = START;
            break;

        case BCC1_OK:
            if (!isInformationFrame(frame->c)) {
                if (byte == FLAG)
                    return FRAME_OK;
                state = START;
                break;
            }
            state = DATA;

        case DATA:
            if (byte == FLAG) {
                if (frame->dataSize == 0)
                    return FRAME_DATA_ERROR;
                frame->dataSize--;
                if (calcBCC2(frame->data, frame->dataSize) != frame->data[frame->dataSize])
                    return FRAME_DATA_ERROR;
                return FRAME_OK;
            }
            if (byte == ESC) {
                state = ESC_RCV;
                break;
            }
            if (addDataByte(frame, byte) < 0)
                return FRAME_DATA_ERROR;
            break;

        case ESC_RCV:
            if (byte == FLAG)
                return FRAME_DATA_ERROR;
            if (addDataByte(frame, byte ^ ESC_XOR) < 0)
                return FRAME_DATA_ERROR;
            state = DATA;
            break;
        }
    }
}


int receiveSupervisionFrame(unsigned char a, unsigned char c) {
    Frame frame;

    while (TRUE) {
        FrameResult result = readFrame(&frame);
        if (result == FRAME_TIMEOUT || result == FRAME_IO_ERROR)
            return -1;

        if (result == FRAME_OK && frame.a == a && frame.c == c) {
            printf("Recebido %s\n", frameName(c));
            return 0;
        }
    }
}
