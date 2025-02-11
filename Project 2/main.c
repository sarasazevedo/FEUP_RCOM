#include "helper.h"

int main(int argc, char **argv) {
    /* Check input */
    if (argc != 2) {
        printf("Wrong number of arguments provided.\n Usage: ./download ftp://[URL]\n");
        return -1;
    }
    
    /* Parse the given URL */
    struct URL url;
    if (parse(argv[1], &url) != 0) {
        printf("Parsing failed.\n");
        return -1;
    }

    /* Print URL info for debug*/
    printf("User: %s\n", url.user);
    printf("Password: %s\n", url.password);
    printf("Host: %s\n", url.host);
    printf("File path: %s\n", url.file_path);

    /* Open the socket for control */
    int fd_control_socket = openSocket(url.ip, FTP_PORT);
    
    /* Get the answer from sv */
    char answer[MAX_LENGTH];
    int answerInt = readAnswer(fd_control_socket, answer);
    /* Check readAnswer, must be valid */
    if (answerInt != READY_NEW_USR) {
        printf("Non-valid answer from server.\n");
        return -1;
    }

    /* Try to authenticate */
    if (authenticate(fd_control_socket, url.user, url.password) != AUTH_ACCEPTED) {
        printf("Failed authentication.\n");
        exit(-1);
    }

    int passivePort;
    char passiveIP[MAX_LENGTH];

    /* Set to passive mode */
    if (writePasv(fd_control_socket, passiveIP, &passivePort) != PASSIVE_MODE) {
        printf("Passive mode failed\n");
        return -1;
    }

    /* Open the socket for data */
    int fd_data_socket = openSocket(passiveIP, passivePort);
    if (fd_data_socket < 0) {
        printf("Sv socket failed.\n");
        return -1;
    }

    /* Ask for the file */
    char fileCommand[6+strlen(url.file_path)];

    /* Use 'retr' to request the file in the path */
    sprintf(fileCommand, "RETR %s\n", url.file_path);
    write(fd_control_socket, fileCommand, sizeof(fileCommand));

    /* Proccess the answer */
    if (readAnswer(fd_control_socket, answer) != RETR_OK) {
      printf("Server can't get to the file, RETR not okay!\n");
      return -1;
    }

    /* Receive file, get filename through url */
    const char *filename = strrchr(url.file_path, '/') + 1;
    if (filename == NULL) {
        filename = url.file_path;
    }
    if (getFile(fd_data_socket, filename) != 0) {
        printf("Error in transfering file.\n");
        return -1;
    }

    /* Attempt to close the sockets*/
    if (closeSockets(fd_control_socket, fd_data_socket) != CLOSE_OK) {
      printf("Error closing sockets.\n");
      return -1;
    }
    return 0;
}