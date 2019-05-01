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
#include <netdb.h>

#include "traffic.h"
#include "const.h"

void error(const char *msg) {
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[]) {
    int portno = 10172;
    int sockfd = 0, n = 0;
    struct hostent *server;
    struct sockaddr_in addr;
    char request[1024], response[409600];

    char *request_fmt = "GET /cloudsession/resources/networkSizes/urn:li:member:2?edgeType=MemberToMember&maxDegree=DISTANCE_3 HTTP/1.0\r\n\r\n";

    memset(response, '0', sizeof(response));

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Error : Could not create socket \n");
        return 1;
    }

    server = gethostbyname(hostname);
    if (server == NULL) error("ERROR, no such host");

    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(portno);
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

    sprintf(request, request_fmt);
    printf("Request:: \n%s\n", request);

    if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        printf("\n Error : Connect Failed \n");
        return 1;
    }

    printf("\n [Info] Connect Succeeded!!! \n");

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

    printf("\n [Info] Write Succeeded!!! \n");

    memset(response, 0, sizeof(response));
    total = sizeof(response) - 1;
    int received = 0;
    printf("\n [Info] Read Start!!! \n");

    do {
        bytes = read(sockfd, response + received, total - received);

        if (bytes < 0) {
            error("ERROR reading response from socket");
        }

        if (bytes == 0) {
            break;
        }

        received += bytes;
        printf("\n [Info] Read Start!!! \n");
    } while (received < total);

    printf("\n [Info] Read Succeeded!!! \n");

    if (received == total) {
        error("ERROR storing complete response from socket");
    }

    close(sockfd);

    printf("Response: \n%s\n", response);

    return 0;
}