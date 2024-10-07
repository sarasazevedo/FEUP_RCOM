// Read from serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define FLAG 0x7E
#define ADRESS_SENDER 0x03
#define ADRESS_RECEIVER 0x01
#define SET 0x03
#define UA 0x07

#define BUF_SIZE 5

void memdump (void *addr, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) {
        printf("%02x ", *((char*) addr + i));
    }
}

volatile int STOP = FALSE;

int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // Loop for input
    unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    while (STOP == FALSE)
    {
        int bytes = read(fd, buf, BUF_SIZE);

        if(bytes < 0 || bytes > BUF_SIZE){
            perror("read error");
            break;
        }

        printf("Flag = 0x%02x\n", buf[0]);
        printf("Adress = 0x%02x\n", buf[1]);
        printf("Control = 0x%02x\n", buf[2]);
        printf("BCC1 = 0x%02x\n", buf[3]);
        printf("Flag #2 = 0x%02x\n\n", buf[4]);

        if(buf[0] == FLAG && buf[1] == ADRESS_SENDER && buf[2] == SET && buf[3] == (ADRESS_SENDER ^ SET) && buf[4] == FLAG)
        {
            STOP = TRUE;
        }
    }

    sleep(1);

    // Answer to Receiver

    unsigned char buf_answer[BUF_SIZE] = {0};

    buf_answer[0] = FLAG;
    buf_answer[1] = ADRESS_RECEIVER;
    buf_answer[2] = UA;
    buf_answer[3] = ADRESS_RECEIVER ^ UA;
    buf_answer[4] = FLAG;

    int bytes = write(fd, buf_answer, BUF_SIZE);
    printf("%d bytes written to answer\n", bytes);

    memdump(buf_answer, 5);
    printf("\n");

    /*while (STOP == FALSE)
    {
        // Returns after 5 chars have been input
        int bytes = read(fd, buf, BUF_SIZE);
        buf[bytes] = '\0'; // Set end of string to '\0', so we can printf

        printf(":%s:%d\n", buf, bytes);
        if (buf[0] == 'z')
            STOP = TRUE;
    }*/

    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}