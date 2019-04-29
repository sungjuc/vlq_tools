//
// Created by SungJu Cho on 4/29/19.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

void error(const char *msg) {
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[]) {
    char *hostname = "127.0.0.1";
    int portno = 8080;
    int sockfd = 0, n = 0;
    struct sockaddr_in addr;
    char request[1024], response[4096];

    memset(response, '0', sizeof(response));

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Error : Could not create socket \n");
        return 1;
    }

    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(portno);

    if (inet_pton(AF_INET, hostname, &addr.sin_addr) <= 0) {
        printf("\n inet_pton error occurred\n");
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        printf("\n Error : Connect Failed \n");
        return 1;
    }

    int total = strlen(request);
    int sent = 0;
    int bytes = 0;

    do {
        bytes = write(sockfd, request + sent, total - sent);
        if (bytes < 0) {
            error("Error writing message to socket");
        }
        if (bytes == 0) {
            break;
        }
        sent += bytes;
    } while (sent < total);
    int received = 0;

    do {
        bytes = read(sockfd, response + received, total - received);
        if (bytes < 0) {
            error("ERROR reading response from socket");
        }
        if (bytes == 0) {
            break;
        }
        received += bytes;
    } while (received < total);

    if (received == total) {
        error("ERROR storing complete response from socket");
    }

    close(sockfd);

    printf("Response: \n%s\n", response);

    return 0;
}