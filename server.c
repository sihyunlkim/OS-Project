#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "common.h"

#define PORT 8080
#define BUFFER_SIZE 4096

extern ExecutionPlan *parse_line(char *line);

// mutex to prevent a race condition where two threads receive the same client number
static pthread_mutex_t client_count_mutex = PTHREAD_MUTEX_INITIALIZER; 

// variable to hold the number of clients 
static int global_num_client = 0; 

// struct to hold the info of client 
typedef struct {
    // file descriptor for socket 
    int client_fd; 
    // IPv4 address string
    char client_ip[INET_ADDRSTRLEN];
    // port number
    int client_port; 
    // client ID 
    int client_num; 
    // store the same value as client_num
    int thread_num; 
} ClientArgs; 

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

// function to handle incoming clients 
void *handling_client(void *arg){
    ClientArgs *client_arg = (ClientArgs *)arg; 

    // store the right prefix into the label as suggested by the project document 
    char label[128]; 
    snprintf(label, sizeof(label), "[Client #%d - %s:%d]", client_arg->client_num, client_arg->client_ip, client_arg->client_port); 

    // store the data from the client 
    char buffer[BUFFER_SIZE]; 

    while(1){
        // fill the entire buffer with 0 for cleaning up
        memset(buffer, 0, BUFFER_SIZE); 

        // receive the data from the client
        int received_bytes = recv(client_arg->client_fd, buffer, BUFFER_SIZE - 1, 0); 

        // break if the client disconnected or error occured on the socket
        if (received_bytes <= 0){
            printf("[INFO] %s Client disconnected.\n", label);
            fflush(stdout);
            break;  
        }

        // add the null terminator 
        buffer[received_bytes] = '\0'; 
        // remove the newline character at the end 
        buffer[strcspn(buffer, "\n")] = 0; 

        // print the message when the client sends the command to the server 
        printf("[RECEIVED] %s Received command: \"%s\"\n", label, buffer);
        fflush(stdout); 

        // if the client prompted exit, then disconnect 
        if (strcmp(buffer, "exit") == 0){
            printf("[INFO] %s Client requested disconnect. Closing connection.\n", label);
            fflush(stdout);
            // send the diconnection message to the client
            const char *message = "Disconnected from server.\n";
            send(client_arg->client_fd, message, strlen(message), 0); 
            break; 
        }

        // print executing command message 
        printf("[EXECUTING] %s Executing command: \"%s\"\n", label, buffer);
        fflush(stdout); 

        // pass it to parse_line to parse the commands and return ExecutionPlan struct as a pointer
        ExecutionPlan *plan = parse_line(buffer);
        char *output; 

        // if plan is null, then error. Else, execute the plan
        if (!plan){
            output = strdup("Error: invalid command.\n"); 
            printf("[ERROR] %s Failed to parse command: \"%s\"\n", label, buffer);
            fflush(stdout); 
        } else{
            // execute the plan and get the output
            output = execute_and_capture(plan); 
            // free memory 
            free_plan(plan); 
        }

        // set to 1 if any errors are found in the output 
        int contain_error = (strstr(output, "Not Found") != NULL || strstr(output, "not found") != NULL || strstr(output, "No such file") != NULL || strstr(output, "Error") != NULL || strstr(output, "error") != NULL); 

        // send error messages to the client if 
        if (contain_error){
            printf("[ERROR] %s Command not found: \"%s\"\n", label, buffer);
            printf("[OUTPUT] %s Sending error message to client: \"%s\"\n", label, output);
        } else {
            printf("[OUTPUT] %s Sending output to client:\n%s\n", label, output);
        }

        fflush(stdout); 

        // send a newline character at least when executing commands does not produce any outputs
        // since client gets stuck on recv()
        if (strlen(output) == 0){
            send(client_arg->client_fd, "\n", 1, 0); 
        } else{
            // send the output to the client
            send(client_arg->client_fd, output, strlen(output), 0);
        }

        // free the memory
        free(output); 
    }

    // close the socket 
    close(client_arg->client_fd); 
    // send disconnection message
    printf("[INFO] Client #%d disconnected.\n", client_arg->client_num);
    fflush(stdout); 
    // free the memory for the client 
    free(client_arg); 

    return NULL; 
}

int main() {
    int server_fd;
    struct sockaddr_in address;

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

    // start listening for incoming connections (max 10 queued)
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    // print the message indicating that we wait for the clients coming in
    printf("[INFO] Server started, waiting for client connections...\n");
    fflush(stdout);

    // accept one client connection at a time in a loop
    while (1) {
        // make struct to stor the client's info
        struct sockaddr_in client_addr; 
        socklen_t client_addrlen = sizeof(client_addr); 

        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&client_addrlen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        // lock the mutex so that no other thread is able to enter this block at the same time
        pthread_mutex_lock(&client_count_mutex); 
        global_num_client++; 
        // set the global number of clients to the ID of the current client
        int cur_client_num = global_num_client; 
        // unlock the mutex to allow other threads to proceed 
        pthread_mutex_unlock(&client_count_mutex);

        // dynamically allocate the memory for the struct
        ClientArgs *client_arg = malloc(sizeof(ClientArgs));
        // store the socket file descriptor 
        client_arg->client_fd = client_fd; 
        // store the current client num
        client_arg->client_num = cur_client_num; 
        // store the same value 
        client_arg->thread_num = cur_client_num; 
        // extract the client's port number and convert it into host's byte order 
        client_arg->client_port = ntohs(client_addr.sin_port); 
        // convert the binary form of IP address into human-readable form 
        inet_ntop(AF_INET, &client_addr.sin_addr, client_arg->client_ip, sizeof(client_arg->client_ip)); 

        // print the client info 
        printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n", client_arg->client_num, client_arg->client_ip, client_arg->client_port, client_arg->thread_num);
        fflush(stdout);

        // set up the configuration of the thread before we create it 
        pthread_t tid; 
        pthread_attr_t attributes; 
        pthread_attr_init(&attributes);
        // set the thread to detached mode so that resources are automatically freed by the OS
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED); 

        // error handling 
        if (pthread_create(&tid, &attributes, handling_client, client_arg) != 0){
            perror("pthread_create() failed"); 
            close(client_fd); 
            free(client_arg); 
        }

        // free the memory 
        pthread_attr_destroy(&attributes);
    }

    close(server_fd);
    return 0;
}