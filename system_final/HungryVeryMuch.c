#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>

int client_socket;

void handle_sigint(int sig) {
    printf("\nHungryVeryMuch client shutting down...\n");
    close(client_socket);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s [server_ip] [portnumber] [numberOfClients] [p] [q]\n", argv[0]);
        exit(1);
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int numberOfClients = atoi(argv[3]);
    int p = atoi(argv[4]);
    int q = atoi(argv[5]);

    signal(SIGINT, handle_sigint);

    // Print the PID at the start
    pid_t pid = getpid();
    printf("HungryVeryMuch client PID: %d\n", pid);

    for (int i = 0; i < numberOfClients; i++) {
        struct sockaddr_in server_addr;
        if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("Socket creation failed");
            exit(1);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = inet_addr(server_ip);

        if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("Connect failed");
            close(client_socket);
            continue;
        }

        // Send numberOfClients and PID to the server
        send(client_socket, &numberOfClients, sizeof(int), 0);
        send(client_socket, &pid, sizeof(pid_t), 0);

        srand(time(NULL) + i);
        int x = rand() % p;
        int y = rand() % q;

        send(client_socket, &x, sizeof(int), 0);
        send(client_socket, &y, sizeof(int), 0);

        printf("Order placed from location (%d, %d)\n", x, y);
        sleep(1); // Simulate order placement interval
        close(client_socket); // Close the socket after placing the order
    }

    return 0;
}
