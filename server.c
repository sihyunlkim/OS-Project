#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include "common.h"

#define PORT 8080
#define BUFFER_SIZE 4096

extern ExecutionPlan *parse_line(char *line);

// execute the parsed plan and capture all stdout and stderr output into a
// dynamically allocated buffer so it can be sent back to the client over the socket.
// the caller is responsible for freeing the returned buffer.
// if execution produces no output, an empty string is returned.
char *execute_and_capture(ExecutionPlan *plan) {
    // create a pipe to capture the output of the child processes
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return strdup("Internal error: pipe failed.\n");
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return strdup("Internal error: fork failed.\n");
    }

    if (pid == 0) {
        // child process: redirect both stdout and stderr to the write end of the pipe,
        // then execute the plan normally using the Phase 1 executor
        close(pipefd[0]); // close unused read end
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execute_plan(plan);
        exit(0);
    }

    // parent process: close the write end and read all output from the pipe
    close(pipefd[1]);

    // read the output in chunks and store it in a dynamically growing buffer
    char *output = malloc(BUFFER_SIZE);
    int total = 0;
    int capacity = BUFFER_SIZE;
    int bytes;
    char chunk[BUFFER_SIZE];

    while ((bytes = read(pipefd[0], chunk, sizeof(chunk))) > 0) {
        // grow the buffer if needed to accommodate more output
        if (total + bytes >= capacity) {
            capacity *= 2;
            output = realloc(output, capacity);
        }
        memcpy(output + total, chunk, bytes);
        total += bytes;
    }
    output[total] = '\0';
    close(pipefd[0]);

    // wait for the child wrapper process to finish
    wait(NULL);

    return output;
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    // create a TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    // allow reuse of the port immediately after the server is restarted,
    // preventing "address already in use" errors during development
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // bind the socket to all available network interfaces on the specified port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(1);
    }

    // start listening for incoming connections (max 5 queued)
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(1);
    }

    printf("[INFO] Server started, waiting for client connections...\n");
    fflush(stdout);

    // accept one client connection at a time in a loop
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("[INFO] Client connected.\n");
        fflush(stdout);

        // handle commands from the connected client in a loop
        while (1) {
            memset(buffer, 0, BUFFER_SIZE);

            // receive a command from the client
            int bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                // client disconnected or error occurred
                printf("[INFO] Client disconnected.\n");
                fflush(stdout);
                break;
            }

            buffer[bytes_received] = '\0';

            // remove trailing newline if present
            buffer[strcspn(buffer, "\n")] = 0;

            printf("[RECEIVED] Received command: \"%s\" from client.\n", buffer);
            fflush(stdout);

            // handle the exit command by closing the client connection
            if (strcmp(buffer, "exit") == 0) {
                printf("[INFO] Client requested exit. Closing connection.\n");
                fflush(stdout);
                break;
            }

            printf("[EXECUTING] Executing command: \"%s\"\n", buffer);
            fflush(stdout);

            // parse and execute the command, capturing the output
            ExecutionPlan *plan = parse_line(buffer);
            char *output;

            if (!plan) {
                // parse_line already printed the error to stderr,
                // so send a generic error message back to the client
                output = strdup("Error: invalid command.\n");
                printf("[ERROR] Failed to parse command: \"%s\"\n", buffer);
                fflush(stdout);
            } else {
                output = execute_and_capture(plan);
                free_plan(plan);
            }

            // send the captured output back to the client
            printf("[OUTPUT] Sending output to client:\n%s\n", output);
            fflush(stdout);

            // if the output is empty, then return new character line to the client (avoiding nothing getting returned)
            if (strlen(output) == 0){
                send(client_fd, "\n", 1, 0);
            } else { 
                send(client_fd, output, strlen(output), 0);
            }
            free(output);
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}