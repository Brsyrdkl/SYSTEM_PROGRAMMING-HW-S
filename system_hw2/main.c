#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#define FIFO_1 "fifo_1"
#define FIFO_2 "fifo_2"
#define MAX_SIZE 1024

//static char FIFO_1[6];
//static char FIFO_2[6];
int SUM_SIZE = 1;
int child_count = 2; // Counter for child processes
int exited_children = 0; // Counter for exited child processes
const char MULTIPLY[] = "multiply";
int command_size = 8;
int sleeping_counter = 0;

int generate_random_number() {
    return rand() % 90000 + 10000;
}
// Function to handle SIGCHLD an SIGINT signal
void sigchld_handler(int signal) {
    pid_t pid;
    int status;
    if(signal == SIGCHLD){
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("SIGCHLD received. pid: %d\n", pid);
            exited_children++;
            child_count--;
            printf("Exited Children : %d\n", exited_children);
            char msg[100];
            sprintf(msg, "Child process %d exited with status %d\n", pid, status);
            write(STDOUT_FILENO, msg, strlen(msg));

        }
        if(exited_children == 2){
            // Close FIFOs
            unlink(FIFO_1);
            unlink(FIFO_2);

            exit(EXIT_SUCCESS);
        }
    }
    if(signal == SIGINT){
        printf("Killed PID : %d\n", getpid());
        unlink(FIFO_1);
        unlink(FIFO_2);
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char *argv[]) {

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);
    const int TERM_SIGNALS[2] = {SIGCHLD,SIGINT};

    for (int i = 0; i < 2; i++) {
        if (sigaction(TERM_SIGNALS[i], &sa, NULL) == -1) {
            perror("Error registering signal handler");
            exit(EXIT_FAILURE);
        }
    }


    if (argc != 2) {
        char usage_msg[] = "Usage: %s <integer>\n";
        write(STDERR_FILENO, usage_msg, strlen(usage_msg));
        exit(1);
    }

    int ARRAY_SIZE = atoi(argv[1]);

//    srand(time(NULL));
//    int random_number1 = generate_random_number();
//    int random_number2 = generate_random_number();
//
//    // Rastgele sayıları karakter dizisine dönüştürün ve FIFO isimlerine ekleyin
//    snprintf(FIFO_1, 6, "%d", random_number1);
//    snprintf(FIFO_2, 6, "%d", random_number2);

    // Oluşturulan FIFO isimlerini yazdır
    printf("FIFO Name 1: %s\n", FIFO_1);
    printf("FIFO Name 2: %s\n", FIFO_2);

    // Create FIFOs if they don't exist
    if (mkfifo(FIFO_1, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo");
        exit(1);
    }
    if (mkfifo(FIFO_2, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo");
        exit(1);
    }

    // Register SIGCHLD handler with zombie protection
    //signal(SIGCHLD, sigchld_handler);

    srand(time(NULL));

    int random_array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        random_array[i] = rand() % 10;
    }

    /*--------------------------------------------------------*/
    // Fork child processes
    for (int i = 0; i < 2; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) { // Child process
            char msg[100];
            sprintf(msg, "Child Process (PID: %d): Created\n", getpid());
            write(STDOUT_FILENO, msg, strlen(msg));

            // Child process 1 (Summation)
            if (i == 0) {
                sleep(10);
                //printf("Child 1 keeps going\n");
                int rand_array[ARRAY_SIZE];
                int fifo1_r_child1 = open(FIFO_1, O_RDONLY);

                if (fifo1_r_child1 == -1) {
                    perror("Not Opened fifo1 In Child 1");
                    exit(1);
                }
                //Reading from fifo1
                if (read(fifo1_r_child1, rand_array, sizeof(rand_array)) < 0) {
                    perror("Not Reading fifo1 in Child 1");
                    exit(1);
                }
//                for(int i= 0; i < ARRAY_SIZE; i++){
//                    printf("T : %d\n", rand_array[i]);
//                }
                //Sum Result
                int sum_result = 0;
                for (int i = 0; i < ARRAY_SIZE; i++) {
                    sum_result += rand_array[i];
                }

                int fifo2_w_child1 = open(FIFO_2, O_WRONLY);
                if (fifo2_w_child1 == -1) {
                    perror("Not Opened fifo2 In Child 1");
                    exit(1);
                }
                int sum_array[SUM_SIZE];
                sum_array[0] = sum_result;
                char sum_buf[SUM_SIZE * sizeof(int)];
                memcpy(sum_buf, sum_array, sizeof(sum_buf));

                if (write(fifo2_w_child1, sum_buf, sizeof(sum_result)) < 0) {
                    perror("Not Writen Fifo2 In Child1");
                    exit(1);
                }

                close(fifo1_r_child1);
            }
                // Child process 2 (Multiplication)
            else if (i == 1) {
                sleep(10);
                //printf("Child 2 keeps going\n");
                char command[MAX_SIZE];
                char combined_string[MAX_SIZE];
                int rand_array[ARRAY_SIZE];
                int sum_array[1];
                int fifo2_r_child2 = open(FIFO_2, O_RDONLY);
                int multiply;

                if (fifo2_r_child2 == -1) {
                    perror("Not Opened fifo2 In Child 2");
                    exit(1);
                }

                if (read(fifo2_r_child2, sum_array, sizeof(sum_array)) < 0) {
                    perror("Not Reading Sum fifo1 in Child 1");
                    exit(1);
                }

                //printf("Sum Array : %d\n", sum_array[0]);
                if (read(fifo2_r_child2, combined_string, sizeof(combined_string)) < 0) {
                    perror("Not Reading Command fifo1 in Child 1");
                    exit(1);
                }
                //printf("Read combined string : %s\n", combined_string);
                int j = 0;
                for (int i = 0; i < command_size; i++) {
                    command[i] = combined_string[i];
                    j = i;
                }
                command[j + 1] = '\0';
                //printf("J : %d\n", j);
                i = 0;
                while (combined_string[j] != '\0') {
                    j++;
                    rand_array[i] = combined_string[j] - '0';
                    i++;
                }
                //printf(" i ve j : %d ve %d",i,j);
                //printf("Command : %s\n", command);

                if (strcmp(command, MULTIPLY) == 0) {
                    //printf("multiply\n");
                    int multiply_result = 1;
                    for (int i = 0; i < ARRAY_SIZE; i++) {
                        multiply_result *= rand_array[i];
                    }
                    multiply = multiply_result;
                }
                else{
                    perror("Command is not 'multiply'\n");
                    exit(1);
                }
                printf("Sum of Array : %d\n", sum_array[0]);
                printf("Multiply of Array : %d\n", multiply);
                printf("Total Result : %d\n", sum_array[0] + multiply);
//                for(int i= 0; i < ARRAY_SIZE; i++){
//                    printf("T : %d\n", rand_array[i]);
//                }

                close(fifo2_r_child2);
            }
            exit(0);
        }
    }
    //Parent
            //Opening Fifo1
            while (sleeping_counter < 5) {
                sleep(2); // Parent sleeps for 2 seconds
                char proceed_msg[] = "Proceeding...\n";
                write(STDOUT_FILENO, proceed_msg, strlen(proceed_msg));
                sleeping_counter++;
            }
            int fifo1_w_parent = open(FIFO_1, O_WRONLY);
            char array_msg[200];
            sprintf(array_msg, "Array initialized: [");
            for (int i = 0; i < ARRAY_SIZE; i++) {
                char num[5];
                sprintf(num, "%d, ", random_array[i]);
                strcat(array_msg, num);
            }
            strcat(array_msg, "\b]\n");
            write(STDOUT_FILENO, array_msg, strlen(array_msg));

            if (write(fifo1_w_parent, random_array, sizeof(random_array)) < 0) {
                perror("Not Writen Fifo1 In Parent");
                exit(1);
            }

            int fifo2_w_parent = open(FIFO_2, O_WRONLY);
            char random_number_string[ARRAY_SIZE + 1];
            char new_string[command_size + ARRAY_SIZE + 1];

            strcpy(new_string, MULTIPLY);
            //printf("new string : %s\n", new_string);
            for (int i = 0; i < ARRAY_SIZE; i++) {
                snprintf(random_number_string, ARRAY_SIZE + 1, "%1d", random_array[i]);
                strcat(new_string, random_number_string);
            }
            //printf("combined string in parent : %s\n", new_string);


            if (write(fifo2_w_parent, new_string, sizeof(new_string)) < 0) {
                perror("Not Writen Fifo2 In Parent");
                exit(1);
            }

    while(child_count > 0){
        sleep(1);
    }

    exit(0);
}
