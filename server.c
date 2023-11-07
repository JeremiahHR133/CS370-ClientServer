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
#include <ctype.h>

#define PORT "9034"

int nextClientID = 0;

struct User
{
    int UID;
    int fd;
};


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

// Get a listening socket
int get_listener_socket(void)
{
    int listener;
    int yes = 1;
    int rv; // result of the getaddrinfo call, only for error checking
    
    struct addrinfo hints, // Data passed to the getaddrinfo function
                    *ai,   // Linked list result of addrinfo
                    *p;

    // Initialize hints to all 0
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Populate *ai with addrinfo from syscall
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0)
    {
        fprintf(stderr, "selectserver:%s\n", gai_strerror(rv));
        exit(1);
    }

    // Choose the first result from ai that works
    for(p = ai; p != NULL; p = p->ai_next)
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        // If this attempt failed try the next one in the linked list
        if (listener < 0)
        {
            continue;
        }

        // Get rid of "Address already in use" error message
        // setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        // If binding to this port fails, close the listener and try again with the next element in the linked list
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }

        // if we get here, we found and bound to a port sucessfully
        break;
    }

    // None of the elements in ai worked for binding
    if (p == NULL)
    {
        return -1;
    }
    
    // Don't need ai anymore
    freeaddrinfo(ai);

    if (listen(listener, 10) == -1)
    {
        return -1;
    }

    // return the fd
    return listener;
}

void add_to_poll_fd_arary(struct pollfd* pdfs[], int newfd, int* fd_count, int* fd_size, struct User* clientIDS[])
{
    // If there isn't room, add more space to the pdfs array
    if (*fd_count == *fd_size)
    {
        *fd_size *= 2; // double the array size
        *pdfs = realloc(*pdfs, sizeof(**pdfs) * (*fd_size));

        *clientIDS = realloc(*clientIDS, sizeof(**clientIDS) * (*fd_size));
    }

    (*pdfs)[*fd_count].fd = newfd;
    (*pdfs)[*fd_count].events = POLLIN;

    (*clientIDS)[*fd_count].fd = newfd;
    (*clientIDS)[*fd_count].UID = nextClientID;
    nextClientID++;

    (*fd_count)++;
}

void remove_from_poll_fd_array(struct pollfd pdfs[], int i, int *fd_count, struct User clientIDS[])
{
    // coppy the last element to index of removed, and reduce size
    pdfs[i] = pdfs[*fd_count - 1];

    clientIDS[i] = clientIDS[*fd_count - 1];

    (*fd_count)--;
}

void uid_list_to_string(char* ret, struct User* list, int len)
{
    memset(ret, 0, sizeof(ret));
    char* buff = malloc(sizeof(char) * 10);
    // Start at 1 to ignore server id
    for (int i = 1; i < len; i++)
    {
        if (i + 1 == len)
        {
            sprintf(buff, "%d", list[i].UID);
            strcat(ret, buff);
        }
        else
        {
            sprintf(buff, "%d, ", list[i].UID);
            strcat(ret, buff);
        }
    }
    free(buff);
}

int uid_to_fd(struct User* list, int len, int UID)
{
    for (int i = 0; i < len; i++)
    {
        if (UID == list[i].UID)
        {
            return list[i].fd;
        }
    }
    return -1;
}

int fd_to_uid(struct User* list, int len, int fd)
{
    for (int i = 0; i < len; i++)
    {
        if (fd == list[i].fd)
        {
            return list[i].UID;
        }
    }
    return -1;
}

void remove_preceding_code(char* str)
{
    if (str == NULL)
    {
        return;
    }

    int length = strlen(str);

    int i;
    for (i = 0; i < length; i++)
    {
        if (!isdigit(str[i]))
        {
            break;
        }
    }

    if (str[i] == ' ')
    {
        i++;
    }

    if (i > 0)
    {
        memmove(str, str + i, length - i + 1);
    }
}

int main(void)
{
    printf("Welcome to the SERVER program!\n");

    int listener_socd; // The listening socket descriptor

    int new_socd;
    struct sockaddr_storage remote_addr;
    socklen_t remote_addr_len;

    char client_data_buffer[256];

    // Reserve space for ip6, can still store ip4
    char remote_IP[INET6_ADDRSTRLEN];
    
    int poll_fd_count = 0;
    int poll_fd_size = 5; // This can be increased later
    struct pollfd* poll_fds = malloc(sizeof(*poll_fds) * poll_fd_size);
    struct User* clientIDS = malloc(sizeof(*clientIDS) * poll_fd_size);

    listener_socd = get_listener_socket();

    if (listener_socd == -1)
    {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    // Add the listener to the set of listeners
    poll_fds[0].fd = listener_socd;
    poll_fds[0].events = POLLIN; // Report ready on event "incomming connection"

    clientIDS[0].fd = listener_socd;
    clientIDS[0].UID = nextClientID;
    nextClientID++;

    poll_fd_count = 1; // for the listener we just added

    // Main loop of program
    for(;;)
    {
        int poll_count = poll(poll_fds, poll_fd_count, -1); // -1 specifies no timeout
        
        // If something breaks with poll exit
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through list of existing connections to look for data to read
        for (int i = 0; i < poll_fd_count; i++)
        {
            // Check if data is ready to read
            if (poll_fds[i].revents & POLLIN)
            {
                // if the event belongs to the listener (someone trying to connect)
                if (poll_fds[i].fd == listener_socd)
                {
                    // Must handle a new connection

                    remote_addr_len = sizeof remote_addr;
                    new_socd = accept(listener_socd, (struct sockaddr*)&remote_addr, &remote_addr_len);

                    if (new_socd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {
                        add_to_poll_fd_arary(&poll_fds, new_socd, &poll_fd_count, &poll_fd_size, &clientIDS);

                        printf("pollserver: new connection from %s on socket %d, assigned user id is %d\n",
                            inet_ntop(remote_addr.ss_family, get_in_addr((struct sockaddr*)&remote_addr), remote_IP, INET6_ADDRSTRLEN),
                            new_socd, clientIDS[poll_fd_count - 1].UID);
                        

                        char* message = malloc(sizeof(char) * 200);
                        int messageLen = sprintf(message, "Welcome! Your UserID is %d\n", clientIDS[poll_fd_count - 1].UID);
                        if (send(new_socd, message, messageLen, 0) == -1)
                        {
                            perror("send");
                        }

                        char* uids = malloc(sizeof(char) * 50);
                        uid_list_to_string(uids, clientIDS, poll_fd_count);
                        messageLen = sprintf(message, "List of currently connected client ids: %s\n", uids);
                        if (send(new_socd, message, messageLen, 0) == -1)
                        {
                            perror("send");
                        }
                        free(uids);
                        free(message);
                    }
                }
                // Otherwise the event belongs to a client sending data
                else
                {
                    int nbytes = recv(poll_fds[i].fd, client_data_buffer, sizeof client_data_buffer, 0);

                    int sender_fd = poll_fds[i].fd;

                    // got an error or connection closed by client
                    if (nbytes <= 0)
                    {
                        // Connection closed
                        if (nbytes == 0)
                        {
                            printf("pollserver: socket %d (uid %d) hung up\n", sender_fd, clientIDS[i].UID);
                        }
                        else
                        {
                            perror("recv");
                        }

                        close(poll_fds[i].fd);
                        remove_from_poll_fd_array(poll_fds, i, &poll_fd_count, clientIDS);
                    }
                    // Otherwise we got some good data from the client
                    else
                    {
                        client_data_buffer[nbytes] = '\0';
                        printf("pollserver: received message from uid %d: '%s'\n", clientIDS[i].UID, client_data_buffer);

                        int code = -1;
                        
                        // Error parsing client message
                        if (sscanf(client_data_buffer, "%d", &code) < 1)
                        {
                            const char* message = "Could not parse message. Usage: <code> <message>\n";
                            if (send(poll_fds[i].fd, message, strlen(message), 0) == -1)
                            {
                                perror("send");
                            }
                        }
                        // Could parse client message
                        else
                        {
                            // Remove the code from the users message
                            remove_preceding_code(client_data_buffer);

                            // User is aksing for list of UIDS
                            if (code == 0)
                            {

                                char* message = malloc(sizeof(char) * 200);
                                char* uids = malloc(sizeof(char) * 50);
                                uid_list_to_string(uids, clientIDS, poll_fd_count);
                                int messageLen = sprintf(message, "List of currently connected client ids: %s\n", uids);
                                if (send(poll_fds[i].fd, message, messageLen, 0) == -1)
                                {
                                    perror("send");
                                }
                                free(uids);
                                free(message);
                            }
                            // User is sending message to themself
                            else if (code == fd_to_uid(clientIDS, poll_fd_count, poll_fds[i].fd))
                            {
                                char* message = malloc(sizeof(char) * 300);
                                memset(message, 0, sizeof(message));

                                // Inform sender that message was sent
                                strcat(message, "Server echos back your message: \'");
                                strcat(message, client_data_buffer);
                                strcat(message, "\'\n");
                                if (send(poll_fds[i].fd, message, strlen(message), 0) == -1)
                                {
                                    perror("send");
                                    printf("Error on echo users message to themselves\n");
                                }
                                free(message);
                            }
                            // User would like to send message to another user
                            else
                            {
                                int found = 0;
                                // Check to see if code is in UID list
                                for (int i = 1; i < poll_fd_count; i++)
                                {
                                    if (clientIDS[i].UID == code)
                                    {
                                        found = 1;
                                        break;
                                    }
                                }
                                // If we didn't find the UID in the list of UIDS
                                if (!found)
                                {
                                    char* message = malloc(sizeof(char) * 100);
                                    int len = sprintf(message, "Specified client with UID of %d could not be found\n", code);
                                    if (send(poll_fds[i].fd, message, len, 0) == -1)
                                    {
                                        perror("send");
                                    }
                                    free(message);
                                }
                                // Can now forward the message to the recieving client
                                else
                                {
                                    char* message = malloc(sizeof(char) * 300);
                                    memset(message, 0, sizeof(message));

                                    // Forward message
                                    sprintf(message, "Message from client %d: \'", fd_to_uid(clientIDS, poll_fd_count, poll_fds[i].fd));
                                    strcat(message, client_data_buffer);
                                    strcat(message, "\'\n");
                                    if (send(uid_to_fd(clientIDS, poll_fd_count, code), message, strlen(message), 0) == -1)
                                    {
                                        perror("send");
                                    }

                                    memset(message, 0, sizeof(message));

                                    // Inform sender that message was sent
                                    sprintf(message, "You sent this message to client %d: \'", code);
                                    strcat(message, client_data_buffer);
                                    strcat(message, "\'\n");
                                    if (send(poll_fds[i].fd, message, strlen(message), 0) == -1)
                                    {
                                        perror("send");
                                    }

                                    free(message);
                                }
                            }
                        }
                    }
                } // END handle data from client / adding new client
            } // END check if data is ready
        }// END looping through all FDs
    } // END for(;;) loop

    return 0;
}