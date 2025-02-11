// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FLAG 0x7E
#define A_T 0x03
#define A_R 0x01
#define SET 0x03
#define UA 0x07
#define RR0 0xAA
#define RR1 0xAB
#define REJ0 0x54
#define REJ1 0x55
#define DISC 0x0B
#define ESC 0x7D

#define BUFFER_SIZE 5

typedef enum
{
    start,
    flagRCV,
    aRCV,
    cRCV,
    bccOK,
    data,
    done
} statusReceived;

bool alarmEnabled = FALSE;
int alarmCount = 0;

unsigned char readAnswer();
extern int fd; 

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
    printf("Alarm #%d\n", alarmCount);
}

int frameNumber = 0;
int nRetransmissions = 0;
int timeout = 0;
LinkLayerRole role;

// variable for the ammount of bytes read
int readBytes = 0;
int frameCount = 0;
int totalFrameSize = 0;

int llopenCount, llwriteCount, llreadCount, llcloseCount, bytestuffCount, byteCount = 0;

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    printf("Starting llopen.\n");

    fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0)
    {
        printf("Error on open serial port function on llopen!\n");
        return -1;
    }

    alarm(0);
    alarmCount = 0;
    alarmEnabled = FALSE;

    char byte;
    statusReceived status = start;
    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    role = connectionParameters.role;

    if(role == LlTx)
    {
        (void) signal(SIGALRM, alarmHandler);
        char buf[BUFFER_SIZE] = {FLAG, A_T, SET, A_T ^ SET, FLAG};

        while(nRetransmissions > alarmCount && status != done)
        {
            if (!alarmEnabled)
            {
                if(writeBytes((char *)buf, BUFFER_SIZE) < 0)
                {
                    printf("Write error (SET) by transmitter on llopen!\n");
                    return -1;
                }
                alarm(connectionParameters.timeout);
                alarmEnabled = TRUE;
                status = start;
            }
            while(status != done && alarmEnabled == TRUE)
            {
                readBytes = readByte(&byte);
                if (readBytes < 0)
                {
                    printf("Read byte error on llopen, transmitter side!\n");
                    return -1;
                }
                if (readBytes != 0)
                {
                    switch (status)
                    {
                        case start:
                            if (byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            break;
                        case flagRCV:
                            if (byte == A_T)
                            {
                                status = aRCV;
                            }
                            else if (byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        case aRCV:
                            if (byte == UA)
                            {
                                status = cRCV;
                            }
                            else if (byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        case cRCV:
                            if(byte == (A_T ^ UA))
                            {
                                status = bccOK;
                            }
                            else if (byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        case bccOK:
                            if (byte == FLAG)
                            {
                                status = done;
                                alarmEnabled = FALSE;
                                alarmCount = 0;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        default:
                            status = start;
                            break;
                    }
                }
            }
        }
        if (alarmCount >= connectionParameters.nRetransmissions) 
        {
            printf("Max retransmissions reached!\n");
            return -1;
        }
    }
    else if(role == LlRx)
    {
        while (status != done)
        {
            readBytes = readByte(&byte);
            if (readBytes < 0)
            {
                printf("Read byte error on receiver side on llopen!\n");
                return -1;
            }
            if (readBytes != 0)
            {
                switch (status)
                {
                    case start:
                        if (byte == FLAG)
                        {
                            status = flagRCV;
                        }
                        break;
                    case flagRCV:
                        if (byte == A_T)
                        {
                            status = aRCV;
                        }
                        else if (byte == FLAG)
                        {
                            status = flagRCV;
                        }
                        else
                        {
                            status = start;
                        }
                        break;
                    case aRCV:
                        if (byte == SET)
                        {
                            status = cRCV;
                        }
                        else if (byte == FLAG)
                        {
                            status = flagRCV;
                        }
                        else
                        {
                            status = start;
                        }
                        break;
                    case cRCV:
                        if (byte == (SET ^ A_T))
                        {
                            status = bccOK;
                        }
                        else if (byte == FLAG)
                        {
                            status = flagRCV;
                        }
                        else 
                        {
                            status = start;
                        }
                        break;
                    case bccOK:
                        if (byte == FLAG)
                        {
                            status = done;
                        }
                        else 
                        {
                            status = start;
                        }
                        break;
                    default:
                        status = start;
                        break;
                }
            }
        }
        char buf[BUFFER_SIZE] = {FLAG, A_T, UA, A_T ^ UA, FLAG};
        if (writeBytes((char *)buf, BUFFER_SIZE) < 0)
        {
            printf("Write bytes error on llopen answer!\n");
            return -1;
        }
    }
    else
    {
        printf("Invalid connection parameter role!\n");
        return -1;
    }
    printf("LLOPEN done!\n");
    llopenCount++;
    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////

int llwrite(const unsigned char *buf, int bufSize)
{
    // frame size is buffer size, plus the 4 initial bytes, FLAG, A, FRAME NUMBER, BCC1, and 2 final ones, BCC2 and FLAG
    int frameSize = 4 + bufSize + 2; 
    // counter for all bytes that need stuffing
    int bytesStuffed = 0;

    // get the ammount of bytes that need stuffing here (and in total), and increment the byte count for all written bytes
    for (int i = 0; i < bufSize; i++)
    {
        if(buf[i] == FLAG || buf[i] == ESC)
        {
            bytestuffCount++;
            bytesStuffed++;
        }
        byteCount++;
    }

    // get the bcc2 based on the buffer data
    unsigned char bcc2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }

    // careful with stuffing for bcc2
    if (bcc2 == FLAG || bcc2 == ESC)
    {
        bytesStuffed++;
    }

    // re-define the size of the frame, to sum the stuffing 
    frameSize += bytesStuffed;

    // create the frame and initialize the starting bytes
    unsigned char frame[frameSize];
    // flag to indicate start of frame
    frame[0] = FLAG;
    // address
    frame[1] = A_T;
    // frame number
    frame[2] = frameNumber << 7;
    // bcc1
    frame[3] = frame[1] ^ frame[2];

    // go byte by byte on the frame (i) and on the buffer (j), and do appropriate stuffing in case of need
    for(int i = 4, j = 0; i < frameSize && j < bufSize; i++, j++)
    {
        if(buf[j] == FLAG || buf[j] == ESC)
        {
            // increment i by one more, to keep track of stuffing
            frame[i++] = ESC;
            frame[i] = buf[j] ^ 0x20;
        }
        else
        {
            frame[i] = buf[j];
        }
    }

    // put correct values of bcc2
    if (bcc2 == FLAG || bcc2 == ESC)
    {
        frame[frameSize - 3] = ESC;
        frame[frameSize - 2] = bcc2 ^ 0x20;
    }
    else 
    {
        frame[frameSize - 2] = bcc2;
    }

    // terminate the frame
    frame[frameSize - 1] = FLAG;

    // reset the alarm
    alarm(0);
    alarmCount = 0;
    alarmEnabled = FALSE;

    // try to send the prepared I frame, with the connection parameters in mind
    while (alarmCount < nRetransmissions && alarmCount != -1)
    {
        // enable the alarm and re-write in case of timeout
        if (!alarmEnabled)
        {
            if (writeBytes((char *)frame, frameSize) < 0)
            {
                printf("Write byte error on llwrite!\n");
                return -1;
            }
            alarmEnabled = TRUE;
            alarm(timeout);
        }
        // if alarm is enabled, wait for the answer from the receiver and proccess it accordingly
        while (alarmEnabled == TRUE) 
        {
            // get the answer from receiver after writing frame.
            unsigned char answer = readAnswer(fd);

            // answer got a timeout, just continue and try again
            if (answer == 0)
            {
                continue;
            }
            
            // when we get a 1, we got error on function to get answer, so respond accordingly
            else if (answer == 1)
            {
                printf("Answer error!\n");
                return -1;
            }

            // if the frame is rejected, re-write
            else if ((answer == REJ0 && frameNumber == 0) || (answer == REJ1 && frameNumber == 1))
            {
                printf("Rejected frame, retrying to write.\n");
                // reset the alarm to re-write
                alarm(0);
                alarmCount = 0;
                alarmEnabled = FALSE;
                break;
            }
            // frame was accepted, receiver requesting next frame, flip frame number and set alarm count to -1 to exit loop
            else if ((answer == RR0 && frameNumber == 1) || (answer == RR1 && frameNumber == 0))
            {
                printf("Answer is %u.\n", answer);
                frameNumber = 1 - frameNumber;
                alarmCount = -1;
                break;
            }
            // dessincronized or unexpected behaviour
            else
            {
                printf("Answer is %u.\n", answer);
                printf("Something went reaaally wrong!\n");
                break;
            }
        }
    }

    // if the exit condition was max transmissions reached, print warning, close the port and return error
    if (alarmCount == nRetransmissions)
    {
        printf("Max retransmissions reached, aborting!\n");
        llclose(fd);
        return -1;
    }

    // update statistics
    llwriteCount++;
    printf("LLWRITE done!\n");
    return frameSize;
}

unsigned char readAnswer()
{
    // start state machine and set values to 0
    statusReceived status = start;
    unsigned char byte, answer = 0x00;
    // simple state machine that extracts answer out from a frame
    while (status != done)
    {
        readBytes = readByte((char *)&byte);
        if (readBytes > 0)
        {
            switch (status)
            {
                case start:
                    if (byte == FLAG)
                    {
                        status = flagRCV;
                    }
                    break;
                case flagRCV:
                    if (byte == A_T)
                    {
                        status = aRCV;
                    }
                    else if (byte == FLAG)
                    {
                        status = flagRCV;
                    }
                    else
                    {
                        status = start;
                    }
                    break;
                case aRCV:
                    if (byte == REJ0 || byte == REJ1 || byte == RR0 || byte == RR1)
                    {
                        status = cRCV;
                        answer = byte;
                    }
                    else if (byte == FLAG)
                    {
                        status = flagRCV;
                    }
                    else
                    {
                        status = start;
                    }
                    break;
                case cRCV:
                    if (byte == (A_T ^ answer))
                    {
                        status = bccOK;
                    }
                    else if (byte == FLAG)
                    {
                        status = flagRCV;
                    }
                    else 
                    {
                        status = start;
                    }
                    break;
                case bccOK:
                    if (byte == FLAG)
                    {
                        status = done;
                    }
                    else 
                    {
                        status = start;
                    }
                    break;
                default:
                    break;
            }
        }
        else if (readBytes == 0)
        {
            return 0;
        }
        else
        {
            printf("Read byte error in read answer!\n");
            return 1;
        }
    }
    return answer;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    unsigned char byte, controlField;
    statusReceived status = start;
    int packetSize = 0;
    bool destuff = FALSE;
    
    while(status != done)
    {
        // read the byte from the serial port
        readBytes = readByte(&byte);
        // return error in case of error
        if (readBytes < 0)
        {
            printf("Read byte error on llread!\n");
            return -1;
        }
        // only if we read a byte do we continue the switch case, otherwise just keep waiting
        if (readBytes > 0)
        {
            switch(status)
            {
                case start:
                    // start flag
                    if (byte == FLAG)
                    {
                        status = flagRCV;
                    }
                    break;
                case flagRCV:
                    // check for the adress field
                    if (byte == A_T)
                    {
                        status = aRCV;
                    }
                    else if (byte != FLAG)
                    {
                        status = start;
                    }
                    break;
                case aRCV:
                    // check for the control field, only accept matching frame numbers
                    if ((frameNumber == 0 && byte == 0x00) || (frameNumber == 1 && byte == 0x80)) // Frame = 0 or frame = 1
                    {
                        status = cRCV;
                        controlField = byte;
                    }
                    else if (byte == FLAG)
                    {
                        status = flagRCV;
                    }
                    else
                    {
                        status = start;
                    }
                    break;
                case cRCV:
                    // check bcc1
                    if (byte == (A_T ^ controlField))
                    {
                        status = data;
                    }
                    else if (byte == FLAG)
                    {
                        status = flagRCV;
                    }
                    else 
                    {
                        printf("BCC1 error!\n");
                        status = start;
                    }
                    break;
                case data:
                    // case to avoid overflow, reached max payload
                    if (packetSize > MAX_PAYLOAD_SIZE) 
                    {
                        printf("Buffer overflow on llread packet!\n");
                        return -1;
                    }
                    // if destuff flag is true, correctly destuff the byte and add to packet
                    if (destuff)
                    {
                        // valid frames should never have FLAG to be destuffed
                        if (byte == FLAG)
                        {
                            printf("Invalid frame: ESC character followed by FLAG!\n");
                            return -1;
                        }
                        byte ^= 0x20;
                        destuff = FALSE;
                        packet[packetSize++] = byte;
                        byteCount++;
                    }
                    // if byte encountered is ESC, we need to destuff the next byte and skip this one 
                    else if (byte == ESC)
                    {
                        destuff = TRUE;
                        bytestuffCount++;
                    }
                    // if we find a flag, check bcc2 to terminate
                    else if(byte == FLAG)
                    {
                        // check if it is valid packet with anyhting
                        if (packetSize <= 0)
                        {
                            printf("No data received before FLAG. Invalid frame.\n");
                            return -1;
                        }

                        // check bcc2 from transmitter
                        unsigned char bcc2 = packet[packetSize - 1];
                        packetSize--;
                        byteCount--;
                        // calculate bcc2 from received data
                        unsigned char receivedBcc2 = packet[0];
                        for(unsigned int i = 1; i < packetSize; i++){
                            receivedBcc2 = receivedBcc2 ^ packet[i];
                        }

                        // if they are equal, we are good! and can flip frame number to request the next frame with a reply
                        if(bcc2 == receivedBcc2){
                            status = done;
                            frameNumber = 1 - frameNumber;
                            unsigned char rrControlField = frameNumber ? RR1 : RR0;
                            unsigned char bcc = A_T ^ rrControlField;
                            unsigned char rrFrame[BUFFER_SIZE] = {FLAG, A_T, rrControlField, bcc, FLAG};
                            if (writeBytes((char *)rrFrame, sizeof(rrFrame)) < 0)
                            {
                                printf("Write bytes error on reply from rx, llread!\n");
                                return -1;
                            }
                            llreadCount++;
                            printf("Reading done!\n");
                            return packetSize;
                        }
                        else
                        {
                            // If BCC2 is incorrect then send REJ, don't flip frame number cuz we reject the old one
                            printf("BCC2 error!\n");
                            unsigned char rejControlField = frameNumber ? REJ1 : REJ0;
                            unsigned char bcc = A_T ^ rejControlField;
                            unsigned char rejFrame[BUFFER_SIZE] = {FLAG, A_T, rejControlField, bcc, FLAG};
                            if (writeBytes((char *)rejFrame, sizeof(rejFrame)) < 0)
                            {
                                printf("Write bytes error on rejection from rx, llread!\n");
                                return -1;
                            }
                            return 0;
                        }
                    }
                    // base case, add a normal byte to the packet
                    else
                    {
                        packet[packetSize++] = byte;
                        byteCount++;
                    }
                    break;
                default:
                    status = start;
                    break;
            }
        }
    }
    return -1;
}


////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // reset the alarm, good practice
    alarmEnabled = FALSE;
    alarmCount = 0;
    alarm(0);

    char byte = 0;
    statusReceived status = start;

    if(role == LlTx)
    {
        (void)signal(SIGALRM, alarmHandler);
        
        // try to send disc while you don't receive a DISC back
        while(nRetransmissions > alarmCount && status != done)
        {
            // build the frame
            unsigned char buf[BUFFER_SIZE] = {FLAG, A_T, DISC, (A_T ^ DISC), FLAG};
            if (writeBytes((char*)buf, BUFFER_SIZE) < 0)
            {
                printf("Error in llclose writebytes!\n");
                return -1;
            }
            // set alarm
            alarm(timeout);
            alarmEnabled = TRUE;
            // while alarm is enabled try to read the I frame (DISC) from the rx
            while(alarmEnabled)
            {
                // read the byte from the serial port 
                readBytes = readByte(&byte);
                // in case of error, return error
                if (readBytes < 0)
                {
                    printf("Read byte error on llclose transmitter side!\n");
                    return -1;
                }
                // only if we read a byte do we go to state machine
                if (readBytes != 0)
                {
                    switch (status)
                    {
                        case start:
                            if(byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            break;
                        case flagRCV:
                            if (byte == A_R)
                            {
                                status = aRCV;
                            }
                            else if (byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        case aRCV:
                            if (byte == DISC)
                            {
                                status = cRCV;
                            }
                            else if (byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        case cRCV:
                            if(byte == (A_R ^ DISC))
                            {
                                status = bccOK;
                            }
                            else if (byte == FLAG)
                            {
                                status = flagRCV;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        case bccOK:
                            if (byte == FLAG)
                            {
                                status = done;
                                alarmEnabled = FALSE;
                            }
                            else
                            {
                                status = start;
                            }
                            break;
                        default:
                            status = start;
                            break;
                    }
                }
            }
        }
        // send an acknowledgement after disconnecting
        unsigned char buf[BUFFER_SIZE] = {FLAG, A_R, UA, (A_R ^ UA), FLAG};
        if (writeBytes((char *)buf, BUFFER_SIZE) < 0)
        {
            printf("Error in llclose writebytes!\n");
            return -1;
        }
    }
    else if(role == LlRx)
    {
        (void)signal(SIGALRM, alarmHandler);

        // wait to receive an I (DISC) frame from tx
        while(status != done)
        {
            readBytes = readByte(&byte);
            if (readBytes < 0) 
            {
                printf("Read byte error on llclose receiver side!\n");
                return -1;
            }
            if (readBytes != 0) 
            {
                switch (status)
                {
                    case start:
                        if (byte == FLAG)
                        {
                            status = flagRCV;
                        }
                        break;
                    case flagRCV:
                        if (byte == A_T)
                        {
                            status = aRCV;
                        }
                        else if (byte != FLAG)
                        {
                            status = start;
                        }
                        break;
                    case aRCV:
                        if (byte == DISC)
                        {
                            status = cRCV;
                        }
                        else if (byte == FLAG)
                        {
                            status = flagRCV;
                        }
                        else
                        {
                            status = start;
                        }
                        break;
                    case cRCV:
                        if(byte == (A_T ^ DISC))
                        {
                            status = bccOK;
                        }
                        else if (byte == FLAG)
                        {
                            status = flagRCV;
                        }
                        else
                        {
                            status = start;
                        }
                        break;
                    case bccOK:
                        if (byte == FLAG)
                        {
                            status = done;
                        }
                        else
                        {
                            status = start;
                        }
                        break;
                    default:
                        status = start;
                        break;
                }
            }
        }

        // prepare DISC frame and status
        unsigned char buf[BUFFER_SIZE] = {FLAG, A_R, DISC, (A_R ^ DISC), FLAG};
        status = start;

        // after receiving one, send one back with you as author, waiting on the answer from the tx, keep sending them until UA
        while (nRetransmissions > alarmCount && status != done)
        {
            if (!alarmEnabled)
            {
                if (writeBytes((char *)buf, BUFFER_SIZE) < 0)
                {
                    printf("Error in llclose writebytes!\n");
                    return -1;
                }
                alarm(timeout);
                alarmEnabled = TRUE;
            }
            readBytes = readByte(&byte);
            if (readBytes < 0) 
            {
                printf("Read byte error on llclose receiver side waiting for UA!\n");
                return -1;
            }
            // read an UA frame to end with state machine, only if we receive
            if (readBytes != 0)
            {
                switch (status) 
                {
                    case start:
                        if (byte == FLAG) 
                        {
                            status = flagRCV;
                        }
                        break;
                    case flagRCV:
                        if (byte == A_R) 
                        {
                            status = aRCV;
                        } 
                        else if (byte != FLAG) 
                        {
                            status = start;
                        }
                        break;
                    case aRCV:
                        if (byte == UA) 
                        {
                            status = cRCV;
                        } 
                        else if (byte == FLAG) 
                        {
                            status = flagRCV;
                        } 
                        else 
                        {
                            status = start;
                        }
                        break;
                    case cRCV:
                        if (byte == (A_R ^ UA)) 
                        {
                            status = bccOK;
                        } 
                        else if (byte == FLAG) 
                        {
                            status = flagRCV;
                        } else {
                            status = start;
                        }
                        break;
                    case bccOK:
                        if (byte == FLAG) 
                        {
                            status = done;
                        } else 
                        {
                            status = start;
                        }
                        break;
                    default:
                        status = start;
                        break;
                }
            }
        }
    }
    else
    {
        printf("Invalid role on llclose!\n");
        return -1;
    }
    
    llcloseCount++;

    if (showStatistics)
    {
        printf("llopen was called %d times\n", llopenCount);
        printf("llwrite was called %d times\n", llwriteCount);
        printf("llread was called %d times\n", llreadCount);
        printf("llclose was called %d times\n", llcloseCount);
        printf("%d bytes were stuffed\n", bytestuffCount);
        printf("%d information bytes were read (not counting stuffing)\n", byteCount);
    }

    printf("LLCLOSE done!\n");
    return closeSerialPort();
}
