#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#define PORT "9034"

// Given a sockaddr, returns the IPv4 or IPv6 version of the sockaddr_in struct with the data formatted accordingly 
void* get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        // Return the ip4 verison of the struct
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    // Return the ip6 verison of the struct
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int get_connected_socket(char* server)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(server, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return -1;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: connecting to %s\n", s);

    freeaddrinfo(servinfo);

    return sockfd;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: client hostname\n");
        exit(1);
    }

    printf("Welcome to client program!\n");
    printf("Message format: <code> <message>\n");
    printf("   <code> = 0 asks the server for the list of currently connected users\n");
    printf("   <code> = <UID> sends the message to user <UID>\n");
    printf("   <code> = <YourUID> asks the server to echo the message back\n");

    int server_sockfd, numbytes;
    char send_buffer[256];
    char recv_buffer[256];

    int size_poll_fds = 2;
    struct pollfd* poll_fds = malloc(sizeof(*poll_fds) * size_poll_fds); // stdinput and server

    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;

    server_sockfd = get_connected_socket(argv[1]);
    if (server_sockfd == -1)
    {
        fprintf(stderr, "could not connect to socket");
        exit(1);
    }

    poll_fds[1].fd = server_sockfd;
    poll_fds[1].events = POLLIN;

    for(;;)
    {
        int poll_count = poll(poll_fds, size_poll_fds, -1);

        // Exit on poll erroring
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        for (int i = 0; i < size_poll_fds; i++)
        {
            if (poll_fds[i].revents & POLLIN)
            {
                // User has input to send
                if (poll_fds[i].fd == STDIN_FILENO)
                {
                    fgets(send_buffer, sizeof(send_buffer), stdin);

                    send_buffer[strlen(send_buffer) - 1] = '\0'; // remove the newline 

                    if (send(server_sockfd, send_buffer, strlen(send_buffer), 0) == -1)
                    {
                        perror("send");
                    }
                }
                // Server has mesasge for user
                else
                {
                    if ((numbytes = recv(server_sockfd, recv_buffer, sizeof(recv_buffer)-1, 0)) == -1)
                    {
                        perror("recv");
                        exit(1);
                    }

                    recv_buffer[numbytes] = '\0';

                    printf("client: received '%s'\n", recv_buffer);
                }
            }
        }
    }

    close(server_sockfd);

    return 0;
}