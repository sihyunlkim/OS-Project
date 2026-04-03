#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define sizeOfBuffer 4096 

int main(int argc, char *argv[]){
    int sock_fd; 
    struct sockaddr_in server_address;
    char input[sizeOfBuffer]; 
    char response[sizeOfBuffer]; 

    // use the IP address that is passed as an argument or set it to localhost 
    const char *server_ip = (argc >= 2) ? argv[1] : "127.0.0.1"; 

    // create TCP socket 
    sock_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sock_fd < 0){
        perror("socket() failed"); 
        exit(1); 
    }

    // set the address family to IPv4
    server_address.sin_family = AF_INET; 
    // convert the port number to big-endian 
    server_address.sin_port = htons(PORT); 

    // convert the IP address into binary form
    int result = inet_pton(AF_INET, server_ip, &server_address.sin_addr); 
    if (result <= 0){
        fprintf(stderr, "This is invalid IP address : %s\n", server_ip); 
        close(sock_fd); 
        exit(1); 
    }

    // create the connection with the server 
    if (connect(sock_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0){
        perror("connect() failed"); 
        close(sock_fd); 
        exit(1); 
    }

    // read the user input, send it to the server, and then display the response
    while (1){
        // display the shell prompt 
        printf("$ "); 
        fflush(stdout);

        // read the user input from stdin
        if(!fgets(input, sizeof(input), stdin)){
            break; 
        }

        // remove the newline character at the end 
        input[strcspn(input, "\n")] = 0;

        // skip the empty line 
        if (strlen(input) == 0){
            continue; 
        }

        // send the user input over the socket 
        if (send(sock_fd, input, strlen(input), 0) < 0){
            perror("send() failed"); 
            break; 
        }

        // end the program if the user enters exit
        if (strcmp(input, "exit") == 0){
            break; 
        }

        // receive the response from the server using while loop
        while (1){
            // fill the response buffer with 0 for cleaning  
            memset(response, 0, sizeOfBuffer); 
            // receive the response from the server
            int num_bytes_received = recv(sock_fd, response, sizeOfBuffer - 1, 0); 

            // error handling 
            if (num_bytes_received < 0){
                perror("recv() failed"); 
                break; 
            }

            // exit if the server has disconnected
            if (num_bytes_received == 0){
                printf("Server has disconnected.\n"); 
                close(sock_fd); 
                exit(1); 
            }

            // null terminate to treat response as C string
            response[num_bytes_received] = '\0'; 
            printf("%s", response); 
            fflush(stdout); 

            // break if we received all the data 
            if (num_bytes_received < sizeOfBuffer - 1){
                break; 
            }
        }
    }

    // close the socket 
    close(sock_fd); 
    return 0; 
}