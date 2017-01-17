//
//  main.c
//  socket-engineer
//
//  Created by Takashi Yoshida on 1/17/17.
//  Copyright © 2017 Takashi Yoshida. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "9034" // The port we are listening on

// Get sockaddr, IPv4 or IPv6
void* get_in_addr(struct sockaddr* sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char* argv[]) {
    fd_set master; // master file descriptor list
    fd_set read_fds; // temp file descriptor list for select()
    int fdmax; // maximum file descriptor number
    
    int listener; // listening socket descriptor
    int new_fd; // newly accepted socket descriptor
    struct sockaddr_storage remote_addr; // client address
    socklen_t addr_len;
    
    char buf[256]; // buffer for client data
    int n_bytes;
    
    char remote_ip[INET6_ADDRSTRLEN];
    
    int yes = 1; // for setsockopt SO REUSEADDR below
    int i, j, rv;
    
    struct addrinfo hints, *ai, *p;
    
    FD_ZERO(&master); // clear the master and temp sets
    FD_ZERO(&read_fds);
    
    // Get us a socket and bind it
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "select-server: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for (p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }
        // Lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }
    
    // If we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "select-server: failed to bind\n");
        exit(2);
    }
    
    freeaddrinfo(ai); // All done with this
    
    // Listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }
    
    // Add the listener to the master set
    FD_SET(listener, &master);
    
    // Keep track of the biggest file descriptor
    fdmax = listener; // So far, it's this one
    
    // Main loop
    for (;;) {
        read_fds = master; // copy it
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }
        
        // Run through the existing connection for data to read
        for (i = 0; i <= fdmax; ++i) {
            if (FD_ISSET(i, &read_fds)) { // We got one!
                if (i == listener) {
                    // Handle the new connection
                    addr_len = sizeof(remote_addr);
                    new_fd = accept(listener, (struct sockaddr*)&remote_addr,
                                    &addr_len);
                    
                    if (new_fd == -1) {
                        perror("accept");
                    }
                    else {
                        FD_SET(new_fd, &master); // add to the master set
                        if (new_fd > fdmax) {
                            fdmax = new_fd;
                        }
                        printf("select-server: new connection from %s on socket %d\n",
                               inet_ntop(remote_addr.ss_family, get_in_addr((struct sockaddr*)&remote_addr), remote_ip, INET6_ADDRSTRLEN),
                               new_fd);
                    }
                }
                else {
                    // Handle data from a client
                    if ((n_bytes = recv(i, buf, sizeof(buf), 0)) <= 0) {
                        // Got error or connection closed by client
                        if (n_bytes == 0) {
                            // Connection closed
                            printf("select-server: socket %d hung up\n", i);
                        }
                        else {
                            perror("recv");
                        }
                        close(i); //bye!
                        FD_CLR(i, &master); // Remove from the master set
                    }
                    else {
                        // We got some data from a client
                        for (j = 0; j <= fdmax; ++j) {
                            // Send to everyone!
                            if (FD_ISSET(j, &master)) {
                                // Except the listener and ourselves
                                if ((j != listener) && (j != i)) {
                                    if (send(j, buf, n_bytes, 0) == -1) {
                                        perror("send");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}
