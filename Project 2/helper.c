#include "helper.h"

// Parse the URL
int parse(char *link, struct URL *url) {
    // Must start with "ftp://"
    const char *prefix = "ftp://";
    if (strncmp(link, prefix, strlen(prefix)) != 0) {
        printf("URL must start with 'ftp://'.\n");
        exit(-1);
    }

    // Pointer p to do the parsing (skip the 'ftp://' part)
    char *p = link + strlen(prefix);

    // Initialize URL fields to 0
    memset(url->user, 0, MAX_LENGTH);
    memset(url->password, 0, MAX_LENGTH);
    memset(url->host, 0, MAX_LENGTH);
    memset(url->file_path, 0, MAX_LENGTH);
    memset(url->ip, 0, MAX_LENGTH);

    // Check if we have user and password
    char *at = strchr(p, '@');
    char *slash = strchr(p, '/');
    if (!slash) {
        printf("No '/' found after host.\n");
        exit(-1);
    }

    // If we have @, extract user and password
    if (at && at < slash) {
        char *colon = strchr(p, ':');
        if (!colon || colon > at) {
            printf("Invalid user:password@ format.\n");
            return -1;
        }
    
        // Extract the user using the pointers
        size_t user_len = colon - p;
        strncpy(url->user, p, user_len);
        url->user[user_len] = '\0';

        // Extract the password
        size_t pass_len = at - (colon + 1);
        strncpy(url->password, colon + 1, pass_len);
        url->password[pass_len] = '\0';

        // Extract the host
        size_t host_len = slash - (at + 1);
        strncpy(url->host, at + 1, host_len);
        url->host[host_len] = '\0';
    } else {
        // Use default when no user or pass is given
        strcpy(url->user, DEFAULT_USR);
        strcpy(url->password, DEFAULT_PASSWORD);

        // Extract host (different pointers used)
        size_t host_len = slash - p;
        strncpy(url->host, p, host_len);
        url->host[host_len] = '\0';
    }
    // file_path is everything after the slash, but we include slash for absolute path 
    strcpy(url->file_path, slash);

    // Resolve host to IP using the getIP function
    getIP(url->host, url->ip);

    // Check if all fields are defined and filled
    if (!(strlen(url->host) && strlen(url->user) && strlen(url->password) && strlen(url->file_path))) {
        printf("Not all fields are defined!\n");
        return -1;
    }

    return 0;
}

// Get the ip address 
int getIP(const char *hostname, char *ip) {
    struct hostent *h;
    struct in_addr **addr_list;
    int i;

    if ((h = gethostbyname(hostname)) == NULL) {
        printf("Error in gethostbyname() request.\n");
        return -1;
    }

    addr_list = (struct in_addr **) h->h_addr_list;

    for (i = 0; addr_list[i] != NULL; i++) {
        //Return the first one;
        strcpy(ip, inet_ntoa(*addr_list[i]));
        return 0;
    }

    return -1;
}

int openSocket(const char *ip, int port){
    int sockfd;
    struct sockaddr_in server_addr;

    /* Adress Handling */
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);  
    server_addr.sin_port = htons(port); 
    
    /* Open a socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }

    /* Connect to the server */
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }

    return sockfd;
}

int authenticate(const int fd_control_socket, const char* user, const char* password)
{
    /* Create the commands to send */
    char userCommand[5+strlen(user)+1]; 
    sprintf(userCommand, "USER %s\r\n", user);
    char passwordCommand[5+strlen(password)+1]; 
    sprintf(passwordCommand, "pass %s\r\n", password);

    char answer[MAX_LENGTH];
    
    /* Send the user and await a response of acceptance */
    write(fd_control_socket, userCommand, strlen(userCommand));
    int ans = readAnswer(fd_control_socket, answer);
    if ((ans != USR_ACCEPTED) && (ans != AUTH_ACCEPTED)) {
        printf("Unknown user.\n");
        return -1;
    }

    /* Write the password and return the answer */
    write(fd_control_socket, passwordCommand, strlen(passwordCommand));
    return readAnswer(fd_control_socket, answer);
}

int readAnswer(const int fd_control_socket, char *answer) {
    char byte;
    int state = 0, idx = 0;
    memset(answer, 0, MAX_LENGTH);

    // State machine for reading answer
    while (state != 3) {
        read(fd_control_socket, &byte, 1);
        switch (state) {
            // Read answer part
            case 0:
                if(byte == ' ') {
                    state = 1;
                }
                else if (byte == '-') {
                    state = 2;
                }
                else if (byte == '\n') {
                    state = 3;
                }
                else {
                    answer[idx++] = byte;
                }
                break;
            case 1:
                if (byte == '\n') {
                    state = 3;
                }
                else {
                    answer[idx++] = byte;
                }
                break;
            case 2:
                if (byte == '\n') {
                    memset(answer, 0, MAX_LENGTH);
                    state = 0;
                    idx = 0;
                } else {
                    answer[idx++] = byte;
                }
                break;
            default:
                break;
        }
    }

    printf("Answer from server is: %s\n", answer);
    // Extract 3 bytes and turn them to int to get answer
    char temp[4];
    strncpy(temp, answer, 3);
    temp[3] = '\n';
    return atoi(temp);
}

int writePasv(int fd_control_socket, char *ip, int *port) {
    char answer[MAX_LENGTH];
    int ipPart1, ipPart2, ipPart3, ipPart4;
    int port1, port2;

    // Send PASV command 
    write(fd_control_socket, "PASV\n", 5);
    
    if(readAnswer(fd_control_socket, answer) != PASSIVE_MODE) {
        printf("Wrong answer on Passive Mode.\n");
        return -1;
    }

    // Skip until '('
    char *start = strchr(answer, '(');
    if (start == NULL) {
        printf("Error: No '(' found in response.\n");
        return -1;
    }
    start++;

    if (sscanf(start, "%d,%d,%d,%d,%d,%d", &ipPart1, &ipPart2, &ipPart3, &ipPart4, &port1, &port2) != 6) {
        printf("Error on sscanf Passive mode.\n");
        return -1;
    }

    // Build the IP
    sprintf(ip, "%d.%d.%d.%d", ipPart1, ipPart2, ipPart3, ipPart4);

    // Get the port through the two last values
    *port = port1 * 256 + port2;

    return PASSIVE_MODE;
}

int getFile(const int fd_data_socket, const char *filename) {
    // Debug info
    printf("File Transfer Debugging:\n");
    printf("Socket FD: %d\n", fd_data_socket);
    printf("Target Filename: %s\n", filename);

    // Create the file with write permissions
    FILE *f = fopen(filename, "wb");

    // Handle errors in creating files
    if (f == NULL) {
        printf("Error in fopen().\n");
        return -1;
    }

    char buffer[MAX_LENGTH];
    size_t total_bytes = 0, bytes_read;

    // Write into the file the data from the server, and when no more is being read close file
    while ((bytes_read = read(fd_data_socket, buffer, sizeof(buffer))) > 0) {
        if (bytes_read != fwrite(buffer, 1, bytes_read, f)) {
            printf("Error writing to file.\n");
            fclose(f);
            return -1;
        }
        total_bytes += bytes_read;
    }

    printf("Total bytes transfered: %zu\n", total_bytes);
    fclose(f);

    return 0;
}

// Close both sockets and get the answer
int closeSockets(const int fd_control_socket, const int fd_data_socket) {
    // Disconnect the socket and read the answer
    char answer[MAX_LENGTH];
    write(fd_control_socket, "QUIT\n", 5);
    int read_answer = readAnswer(fd_control_socket, answer);

    // Close both ports
    if (close(fd_control_socket != 0)) {
        printf("Error closing control socket.\n");
        return -1;
    }
    if (close(fd_data_socket != 0)) {
        printf("Error closing data socket.\n");
        return -1;
    }
    return read_answer;
}