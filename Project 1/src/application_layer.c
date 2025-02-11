// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#define MAX_FILE_NAME 255 // the length of filename needs to fit into 1 byte, and for almost all practical purposes, it does

int sendControlPacket(int controlValue, const char *filename, int fileSize);
int sendDataPackets(FILE *file, int fileSize);
int receiveDataPackets(const char *filename, int fileSize);
double calculateAverageFrameSizeBits(void);
// Declare these variables as external if they are defined elsewhere (e.g., in link_layer.c)
extern int frameCount;
extern int totalFrameSize;

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // get filename length
    int filenameLength = strlen(filename);
    // check filename to avoid overflow on control packets
    if (filenameLength > 255)
    {
        printf("Filename too long!");
        return;
    }

    // based on the function parameters, create the connection parameters to be fed to the link layer
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // open the port 
    int fd = llopen(connectionParameters);
    if (fd < 0)
    {
        printf("Error opening link layer.\n");
        return;
    }

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    // transmitter opens file, sends control packet(START), sends data, and another control packet (END)
    if (connectionParameters.role == LlTx)
    {
        // open the specified file to send, with read permissions, binary mode
        FILE *file = fopen(filename, "rb");
        if (file == NULL)
        {
            printf("Error opening file.\n");
            llclose(0);
            return;
        }

        // get file size through pointers
        fseek(file, 0, SEEK_END);
        int fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        // flow for sending the needed packets
        if (sendControlPacket(1, filename, fileSize) < 0)
        {
            printf("Send START control packet error, transmitter side!\n");
            llclose(0);
            return;
        }
        if (sendDataPackets(file, fileSize))
        {
            printf("Send data packet error, receiver side!\n");
            llclose(0);
            return;
        }
        if (sendControlPacket(3, filename, fileSize) < 0)
        {
            printf("Send END control packet error, transmitter side!\n");
            llclose(0);
            return;
        }

        fclose(file);
    }
    // receiver processes the control packet START, reads the data PACKETS, and processes control packet END
    else if (connectionParameters.role == LlRx)
    {
        // receive and process START control packet
        int packetSize = 1 + (2 + sizeof(int)) + (2 + filenameLength);
        unsigned char receivedControlPacket[packetSize];
        if (llread(receivedControlPacket) < 0)
        {
            printf("Error reading START control packet!\n");
            llclose(0);
            return;
        }
        // check if it it START packet
        if(receivedControlPacket[0] != 1)
        {
            printf("Wrong control value for START!\n");
            llclose(0);
            return;
        }

        // get the file size from the control packet
        int fileSize;
        memcpy(&fileSize, &receivedControlPacket[1 + 2], sizeof(fileSize));

        // process data packets
        if (receiveDataPackets(filename, fileSize) < 0) 
        {
            printf("Error on receive data packets!\n");
            llclose(0);
            return;
        }

        // receive and process END control packet
        if (llread(receivedControlPacket) < 0)
        {
            printf("Error reading START control packet!\n");
            llclose(0);
            return;
        }
        // check if it it END packet
        if(receivedControlPacket[0] != 3)
        {
            printf("Wrong control value for END!\n");
            llclose(0);
            return;
        }
    }
    else
    {
        printf("Invalid role!\n");
    }

    // both of them close the port after they are finished
    llclose(1);

    gettimeofday(&end_time, NULL);
    double transmission_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec) / 1e6;
    int avgFrameSizeBits = calculateAverageFrameSizeBits();
    double Tframe = avgFrameSizeBits / transmission_time;
    // Calculate throughput
    int totalBytesSent = totalFrameSize; // In bytes
    double throughput = (totalBytesSent * 8.0) / transmission_time;
    printf("Time: %2f segundos\n", transmission_time);
    printf("Average Frame Size: %d bits\n", avgFrameSizeBits);
    printf("TFrame: %2f bits\n", Tframe);
    printf("Throughput: %2f bits/second\n", throughput);
}

double calculateAverageFrameSizeBits()
{
    if (frameCount == 0)
    {
        return 0.0;  // Avoid division by zero if no frames were sent
    }

    // Calculate the average frame size in bits
    double avgFrameSizeBytes = (double)totalFrameSize / frameCount;
    double avgFrameSizeBits = avgFrameSizeBytes * 8;  // Convert to bits
    return avgFrameSizeBits;
}

int sendControlPacket(int controlValue, const char *filename, int fileSize)
{
    // get the length of filename
    int filenameLength = strlen(filename);
    if (filenameLength > MAX_FILE_NAME)
    {
        printf("Filename is too long!\n");
        return -1;
    }
    // 1 byte for control value, two bytes, one for type, and one for length and the actual values, for each (file name and file size)
    int packetSize = 1 + (2 + sizeof(fileSize)) + (2 + filenameLength);
    // fixed-size buffer allocated, large enough for general use
    unsigned char controlPacket[packetSize];

    //define an index to keep track of current position, always sum after defining
    int idx = 0;

    // define the control value to be sent (1 for START and 3 for END)
    controlPacket[idx++] = controlValue;

    // TLV for the file size
    controlPacket[idx++] = 0;
    controlPacket[idx++] = sizeof(fileSize);
    memcpy(&controlPacket[idx], &fileSize, sizeof(fileSize));
    idx += sizeof(fileSize);

    // TLV for the file name
    controlPacket[idx++] = 1;
    controlPacket[idx++] = filenameLength;
    memcpy(&controlPacket[idx], filename, filenameLength);
    idx += filenameLength;

    // Track the size of the control packet
    totalFrameSize += packetSize;
    frameCount++;

    // after building control packet, send it
    if (llwrite(controlPacket, idx) < 0)
    {
        printf("Write error on send control packets!\n");
        return -1;
    }
    return 0;
}

int sendDataPackets(FILE *file, int fileSize)
{
    // set file pointer to be at the start of the file
    fseek(file, 0, SEEK_SET);

    // define starting values for auxiliary variables
    int sequenceNumber = 0;
    int bytesRemaining = fileSize;

    // temporary buffer to hold the data that each packet will send
    unsigned char dataBuffer[MAX_PAYLOAD_SIZE];

    // define the control field, which is 2 for data
    dataBuffer[0] = 2;

    // loop through the file and keep sending packets until no more info left
    while (bytesRemaining > 0)
    {
        // get chunk size, will be 995 until the remaining bytes are more than 0 and less than 995, then it becomes the remaining bytes
        // 995 because we need a byte for C, S, L1 and L2 each, and one byte for bcc2 to be received in the end of the packet
        int chunkSize = (bytesRemaining > MAX_PAYLOAD_SIZE - 5) ? MAX_PAYLOAD_SIZE - 5 : bytesRemaining;

        // start filling the packet with idx
        int idx = 1;

        // define sequence number, and add 1 to it, with mod 100 for wrap around in case on more than 100 packets
        dataBuffer[idx++] = sequenceNumber;
        sequenceNumber = (sequenceNumber + 1) % 100;

        // assuming chunk size is smaller than 65536, which it is since payload size is at max 1000
        // get L1, which are 8 MSB of the size of the chunk 
        dataBuffer[idx++] = (chunkSize >> 8) & 0xFF;
        // and L2, 8 LSB of the size of the chunk
        dataBuffer[idx++] = chunkSize & 0xFF;

        // copy the data into the buffer, fread automatically reads the chunkSize ammount of chars into the data
        int bytesRead = fread(&dataBuffer[idx], sizeof(unsigned char), chunkSize, file);
        if (bytesRead != chunkSize) {
            printf("Error reading from file.\n");
            return -1;
        }

        // Track the size of the data packet
        totalFrameSize += idx + chunkSize;  // `idx` is the header size, `chunkSize` is the data size
        frameCount++;

        // send the packet
        if (llwrite(dataBuffer, idx + chunkSize) < 0)
        {
            printf("Write error on send data packet!\n");
            return -1;
        }
        
        // update remaining bytes
        bytesRemaining -= chunkSize;
    }
    return 0;
}

int receiveDataPackets(const char *filename, int fileSize)
{
    // create a new file with specified filename, to write in binary mode
    FILE *file = fopen(filename, "wb");
    if (file == NULL)
    {
        printf("Error creating file.\n");
        return -1;
    }

    // allocate fixed size array with max payload size to serve as the holder of the packet
    unsigned char packet[MAX_PAYLOAD_SIZE];
    int packetSize;

    // initialize sequence number
    int sequenceNumber = 0;
    
    // loop to read all packets until the whole file is read
    while (fileSize > 0)
    {
        // read the packet
        if((packetSize = llread(packet)) < 0)
        {
            printf("Error reading the received data packet!\n");
            fclose(file);
            return -1;
        }

        // if we get an error in read, re-try to read
        if (packetSize == 0)
        {
            printf("Rejected frame, try to re-read!\n");
            continue;
        }

        // check control value to see if it is correct
        if (packet[0] != 2)
        {
            printf("Unexpected control field value.\n");
            fclose(file);
            return -1;
        }

        // check the sequence number to ensure correct order
        if(sequenceNumber != packet[1])
        {
            printf("Sequence number is incorrect! It is %d and should be %u!\n", sequenceNumber, packet[1]);
            fclose(file);
            return -1;
        }

        // maintain increment and wrap around logic for sequence number 
        sequenceNumber = (sequenceNumber + 1) % 100;

        // through L1 and L2, get the chunk size
        int chunkSize = (packet[2] << 8) | packet[3];

        // write into the file the data components of the packet
        if (fwrite(&packet[1 + 1 + 2], sizeof(unsigned char), chunkSize, file) != chunkSize)
        {
            printf("Error writing to file.\n");
            fclose(file);
            return -1;
        }
        
        fileSize -= chunkSize;
        if (fileSize < 0)
        {
            printf("Oh no, received too much data!\n");
            return -1;
        }
    }
    return 0;
}