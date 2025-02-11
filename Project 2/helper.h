#ifndef HELPER_H
#define HELPER_H

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

/* Define a max length for the components of url */
#define MAX_LENGTH 200

#define FTP_PORT 21
#define SERVER_ADDR "192.168.28.96"

/* Server responses */
#define READY_NEW_USR 220
#define USR_ACCEPTED 331
#define AUTH_ACCEPTED 230
#define PASSIVE_MODE 227
#define RETR_OK 150
#define TRANSFER_OK 226
#define CLOSE_OK 221

/* Default login for case 'ftp://<host>/<url-path>' */
#define DEFAULT_USR "anonymous"
#define DEFAULT_PASSWORD "anonymous@example.com"

/* URL struct for parsing */
struct URL {
    char user[MAX_LENGTH]; // 'username'
    char password[MAX_LENGTH]; // 'password'
    char host[MAX_LENGTH]; // 'ftp.up.pt'
    char file_path[MAX_LENGTH]; // 'path/to/file'
    char ip[MAX_LENGTH]; // ddd.ddd.dd.dd
};


// Function definitions
int parse(char *link, struct URL *parsed);
int getIP(const char *hostname, char *ip);
int openSocket(const char *ip, const int port);
int authenticate(const int fd_control_socket, const char* usr, const char* pass);
int readAnswer(const int fd_control_socket, char *answer);
int writePasv(const int fd_control_socket, char *ip, int *port);
int getFile(const int fd_data_socket, const char *file);
int closeSockets(const int fd_control_socket, const int fd_data_socket);

#endif
