#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <sys/time.h>

#define MAX_ORDERS 100
#define MAX_OVEN_CAPACITY 6
#define MAX_DELIVERY_CAPACITY 3
#define MAX_CLIENTS 100

typedef struct Order {
    int client_socket;
    int order_id;
    int x, y;
    pid_t client_pid;
    struct Order* next;
} Order;

typedef struct {
    Order* front;
    Order* rear;
    int size;
    pthread_mutex_t mutex;  // Add mutex to protect the queue
} OrderQueue;

typedef struct {
    int id;
    bool available;
    pthread_cond_t cond;
    int orders_processed; // Keep track of orders processed
} Worker;

typedef struct {
    pid_t pid;
    int numberOfClients;
    bool announced;
    int orders_to_serve;
} ClientInfo;

pthread_mutex_t mutex_orders;
pthread_mutex_t mutex_oven;
pthread_mutex_t mutex_delivery;
pthread_mutex_t mutex_workers;
pthread_mutex_t mutex_clients;

pthread_cond_t cond_orders;
pthread_cond_t cond_oven;
pthread_cond_t cond_delivery;

OrderQueue order_queue;
OrderQueue delivery_queue;

ClientInfo clients[MAX_CLIENTS];
int client_count = 0;

int current_order_id = 0;
int oven_count = 0;

int total_orders = 0;
int delivered_orders = 0;

Worker* cooks;
Worker* couriers;
int cook_thread_pool_size;
int delivery_thread_pool_size;
FILE *log_file;

bool running = true;

void enqueue(OrderQueue* queue, Order* order);
Order* dequeue(OrderQueue* queue);

void *cook_thread(void *arg);
void *delivery_thread(void *arg);
void handle_sigint(int sig);
int calculate_pseudo_inverse();
void cleanup_queue(OrderQueue* queue);
void cleanup_resources();
void thank_most_orders(Worker* workers, int size, const char* role);

int numberOfClients;

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s [portnumber] [CookthreadPoolSize] [DeliveryPoolSize] [k]\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    cook_thread_pool_size = atoi(argv[2]);
    delivery_thread_pool_size = atoi(argv[3]);
    int speed = atoi(argv[4]);

    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    pthread_t cook_threads[cook_thread_pool_size];
    pthread_t delivery_threads[delivery_thread_pool_size];

    signal(SIGINT, handle_sigint);

    pthread_mutex_init(&mutex_orders, NULL);
    pthread_mutex_init(&mutex_oven, NULL);
    pthread_mutex_init(&mutex_delivery, NULL);
    pthread_mutex_init(&mutex_workers, NULL);
    pthread_mutex_init(&mutex_clients, NULL);

    pthread_cond_init(&cond_orders, NULL);
    pthread_cond_init(&cond_oven, NULL);
    pthread_cond_init(&cond_delivery, NULL);

    order_queue.front = order_queue.rear = NULL;
    order_queue.size = 0;
    pthread_mutex_init(&order_queue.mutex, NULL);  // Initialize queue mutex
    delivery_queue.front = delivery_queue.rear = NULL;
    delivery_queue.size = 0;
    pthread_mutex_init(&delivery_queue.mutex, NULL);  // Initialize queue mutex

    cooks = malloc(cook_thread_pool_size * sizeof(Worker));
    for (int i = 0; i < cook_thread_pool_size; i++) {
        cooks[i].id = i + 1;
        cooks[i].available = true;
        cooks[i].orders_processed = 0; // Initialize orders processed
        pthread_cond_init(&cooks[i].cond, NULL);
    }

    couriers = malloc(delivery_thread_pool_size * sizeof(Worker));
    for (int i = 0; i < delivery_thread_pool_size; i++) {
        couriers[i].id = i + 1;
        couriers[i].available = true;
        couriers[i].orders_processed = 0; // Initialize orders processed
        pthread_cond_init(&couriers[i].cond, NULL);
    }

    log_file = fopen("pideshop.log", "a");
    if (log_file == NULL) {
        perror("Log file opening failed");
        exit(1);
    }

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Socket bind failed");
        exit(1);
    }

    if (listen(server_socket, 10) == -1) {
        perror("Listen failed");
        exit(1);
    }

    printf("PideShop active waiting for connections...\n");

    for (int i = 0; i < cook_thread_pool_size; i++) {
        pthread_create(&cook_threads[i], NULL, cook_thread, NULL);
    }
    for (int i = 0; i < delivery_thread_pool_size; i++) {
        pthread_create(&delivery_threads[i], NULL, delivery_thread, (void *)(intptr_t)speed);
    }

    while (running) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            if (running) {
                perror("Accept failed");
            }
            continue;
        }

        pid_t client_pid;

        recv(client_socket, &numberOfClients, sizeof(int), 0);
        recv(client_socket, &client_pid, sizeof(pid_t), 0);

        pthread_mutex_lock(&mutex_clients);
        bool found = false;
        for (int i = 0; i < client_count; i++) {
            if (clients[i].pid == client_pid) {
                found = true;
                break;
            }
        }
        if (!found && client_count < MAX_CLIENTS) {
            clients[client_count].pid = client_pid;
            clients[client_count].numberOfClients = numberOfClients;
            clients[client_count].announced = false;
            clients[client_count].orders_to_serve = 0;
            client_count++;
        }
        pthread_mutex_unlock(&mutex_clients);

        pthread_mutex_lock(&mutex_orders);
        if (order_queue.size < MAX_ORDERS) {
            Order* new_order = (Order*)malloc(sizeof(Order));
            new_order->client_socket = client_socket;
            new_order->order_id = ++current_order_id;
            recv(client_socket, &new_order->x, sizeof(int), 0);
            recv(client_socket, &new_order->y, sizeof(int), 0);
            new_order->client_pid = client_pid;
            new_order->next = NULL;

            pthread_mutex_lock(&order_queue.mutex);
            enqueue(&order_queue, new_order);
            pthread_mutex_unlock(&order_queue.mutex);

            pthread_mutex_lock(&mutex_clients);
            for (int i = 0; i < client_count; i++) {
                if (clients[i].pid == client_pid && !clients[i].announced) {
                    printf("%d new customers... Serving\n", clients[i].numberOfClients);
                    fprintf(log_file, "%d new customers... Serving\n", clients[i].numberOfClients);
                    fflush(log_file);
                    clients[i].announced = true;
                }
                if (clients[i].pid == client_pid) {
                    clients[i].orders_to_serve++;
                    break;
                }
            }
            pthread_mutex_unlock(&mutex_clients);
            printf("Order %d placed from location (%d, %d) by client PID %d\n", new_order->order_id, new_order->x, new_order->y, new_order->client_pid);
            fprintf(log_file, "Order %d placed from location (%d, %d) by client PID %d\n", new_order->order_id, new_order->x, new_order->y, new_order->client_pid);
            fflush(log_file);
            total_orders++;
            pthread_cond_signal(&cond_orders);
        } else {
            close(client_socket);
        }
        pthread_mutex_unlock(&mutex_orders);
    }

    for (int i = 0; i < cook_thread_pool_size; i++) {
        pthread_cancel(cook_threads[i]);
        pthread_join(cook_threads[i], NULL);
    }
    for (int i = 0; i < delivery_thread_pool_size; i++) {
        pthread_cancel(delivery_threads[i]);
        pthread_join(delivery_threads[i], NULL);
    }

    close(server_socket);
    fclose(log_file);
    cleanup_resources();  // Cleanup resources here
    return 0;
}

void *cook_thread(void *arg) {
    Worker* cook = NULL;

    while (1) {
        pthread_mutex_lock(&mutex_workers);
        for (int i = 0; i < cook_thread_pool_size; i++) {
            if (cooks[i].available) {
                cook = &cooks[i];
                cook->available = false;
                break;
            }
        }
        pthread_mutex_unlock(&mutex_workers);

        pthread_mutex_lock(&mutex_orders);
        while (order_queue.size == 0) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&cond_orders, &mutex_orders, &ts);
            if (!running) {
                pthread_mutex_unlock(&mutex_orders);
                pthread_exit(NULL);
            }
        }
        pthread_mutex_lock(&order_queue.mutex);
        Order* order = dequeue(&order_queue);
        pthread_mutex_unlock(&order_queue.mutex);
        pthread_mutex_unlock(&mutex_orders);

        int prepare_time = calculate_pseudo_inverse();
        printf("Cook %d is cooking order %d...\n", cook->id, order->order_id);
        fprintf(log_file, "Cook %d is cooking order %d...\n", cook->id, order->order_id);
        fflush(log_file);
        usleep(prepare_time / 2000); // Half time of prepare using usleep
        printf("Cook %d completed cooking for order %d, taken out of the oven...\n", cook->id, order->order_id);
        fprintf(log_file, "Cook %d completed cooking for order %d, taken out of the oven...\n", cook->id, order->order_id);
        fflush(log_file);

        cook->orders_processed++; // Increment orders processed by the cook

        pthread_mutex_lock(&mutex_oven);
        while (oven_count >= MAX_OVEN_CAPACITY) {
            printf("Oven is full, waiting...\n");
            fprintf(log_file, "Oven is full, waiting...\n");
            fflush(log_file);
            pthread_cond_wait(&cond_oven, &mutex_oven);
        }

        oven_count++;
        pthread_mutex_unlock(&mutex_oven);

        usleep(200000); // Simulate oven time with shorter sleep

        pthread_mutex_lock(&mutex_oven);
        oven_count--;
        pthread_cond_signal(&cond_oven);
        pthread_mutex_unlock(&mutex_oven);

        printf("Order %d is ready for delivery.\n", order->order_id);
        fprintf(log_file, "Order %d is ready for delivery.\n", order->order_id);
        fflush(log_file);

        pthread_mutex_lock(&mutex_delivery);
        pthread_mutex_lock(&delivery_queue.mutex);
        enqueue(&delivery_queue, order);
        pthread_mutex_unlock(&delivery_queue.mutex);
        pthread_cond_signal(&cond_delivery);
        pthread_mutex_unlock(&mutex_delivery);

        pthread_mutex_lock(&mutex_workers);
        cook->available = true;
        pthread_cond_signal(&cook->cond);
        pthread_mutex_unlock(&mutex_workers);
    }
    return NULL;
}

void *delivery_thread(void *arg) {
    int speed = (intptr_t)arg;
    Worker* courier = NULL;

    while (1) {
        pthread_mutex_lock(&mutex_workers);
        for (int i = 0; i < delivery_thread_pool_size; i++) {
            if (couriers[i].available) {
                courier = &couriers[i];
                courier->available = false;
                break;
            }
        }
        pthread_mutex_unlock(&mutex_workers);

        pthread_mutex_lock(&mutex_delivery);
        while (delivery_queue.size == 0) {
            pthread_cond_wait(&cond_delivery, &mutex_delivery);
            if (!running) {
                pthread_mutex_unlock(&mutex_delivery);
                pthread_exit(NULL);
            }
        }

        Order* orders[MAX_DELIVERY_CAPACITY];
        int order_count = 0;

        printf("Moto %d is waiting for orders...\n", courier->id);
        fprintf(log_file, "Moto %d is waiting for orders...\n", courier->id);
        fflush(log_file);

        pthread_mutex_lock(&delivery_queue.mutex);
        while (order_count < MAX_DELIVERY_CAPACITY && delivery_queue.size > 0) {
            orders[order_count++] = dequeue(&delivery_queue);
            pthread_mutex_unlock(&delivery_queue.mutex);
            sleep(2); // Wait for 2 seconds to see if more orders arrive
            pthread_mutex_lock(&delivery_queue.mutex);
        }
        pthread_mutex_unlock(&delivery_queue.mutex);
        pthread_mutex_unlock(&mutex_delivery);

        printf("Moto %d is on the way with %d orders...\n", courier->id, order_count);
        fprintf(log_file, "Moto %d is on the way with %d orders...\n", courier->id, order_count);
        fflush(log_file);

        for (int i = 0; i < order_count; i++) {
            Order* order = orders[i];
            int client_pid = order->client_pid; // Client PID'yi burada alıyoruz
            printf("Delivering order %d to location (%d, %d)...\n", order->order_id, order->x, order->y);
            fprintf(log_file, "Delivering order %d to location (%d, %d)...\n", order->order_id, order->x, order->y);
            fflush(log_file);
            int distance = abs(order->x) + abs(order->y);
            int delivery_time = distance / speed;
            //printf("Delivery Time :  %d\n", delivery_time);
            sleep(delivery_time); // Simulate delivery time
            printf("Order %d delivered by Moto %d.\n", order->order_id, courier->id);
            fprintf(log_file, "Order %d delivered by Moto %d.\n", order->order_id, courier->id);
            fflush(log_file);
            delivered_orders++;
            close(order->client_socket);

            pthread_mutex_lock(&mutex_clients);
            for (int j = 0; j < client_count; j++) {
                if (clients[j].pid == client_pid) {
                    clients[j].orders_to_serve--;
                    if (clients[j].orders_to_serve == 0) {
                        printf("Done serving client PID %d\n", client_pid);
                        fprintf(log_file, "Done serving client PID %d\n", client_pid);
                        fflush(log_file);
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&mutex_clients);
            free(order);
        }

        courier->orders_processed += order_count; // Increment orders processed by the courier

        pthread_mutex_lock(&mutex_workers);
        courier->available = true;
        pthread_cond_signal(&courier->cond);
        pthread_mutex_unlock(&mutex_workers);
    }
    return NULL;
}

int calculate_pseudo_inverse() {
    clock_t start, end;
    start = clock();
    int i, j, k;
    double a[30][40], b[40][30], inverse[30][40];
    for (i = 0; i < 30; i++) {
        for (j = 0; j < 40; j++) {
            a[i][j] = rand() % 10;
        }
    }
    for (i = 0; i < 40; i++) {
        for (j = 0; j < 30; j++) {
            b[i][j] = rand() % 10;
        }
    }

    for (i = 0; i < 30; i++) {
        for (j = 0; j < 40; j++) {
            inverse[i][j] = 0;
            for (k = 0; k < 40; k++) {
                inverse[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    end = clock();
    return end - start;
}

void handle_sigint(int sig) {
    printf("\nShutting down PideShop...\n");
    running = false;
    printf("Total orders: %d, Delivered: %d\n", total_orders, delivered_orders);
    fprintf(log_file, "\nShutting down PideShop...\n");
    fprintf(log_file, "Total orders: %d, Delivered: %d\n", total_orders, delivered_orders);
    fflush(log_file);

    thank_most_orders(cooks, cook_thread_pool_size, "Cook");
    thank_most_orders(couriers, delivery_thread_pool_size, "Moto");

    pthread_cond_broadcast(&cond_orders);
    pthread_cond_broadcast(&cond_delivery);

    cleanup_resources();  // Call cleanup_resources() function
    exit(0);
}

void thank_most_orders(Worker* workers, int size, const char* role) {
    int max_orders = 0;
    for (int i = 0; i < size; i++) {
        if (workers[i].orders_processed > max_orders) {
            max_orders = workers[i].orders_processed;
        }
    }

    for (int i = 0; i < size; i++) {
        if (workers[i].orders_processed == max_orders) {
            printf("Thanks %s %d (Orders: %d)\n", role, workers[i].id, workers[i].orders_processed);
            fprintf(log_file, "Thanks %s %d (Orders: %d)\n", role, workers[i].id, workers[i].orders_processed);
        }
    }
    fflush(log_file);
}

void enqueue(OrderQueue* queue, Order* order) {
    if (queue->rear == NULL) {
        queue->front = queue->rear = order;
    } else {
        queue->rear->next = order;
        queue->rear = order;
    }
    queue->size++;
}

Order* dequeue(OrderQueue* queue) {
    if (queue->front == NULL) {
        return NULL;
    }
    Order* temp = queue->front;
    queue->front = queue->front->next;
    if (queue->front == NULL) {
        queue->rear = NULL;
    }
    queue->size--;
    return temp;
}

void cleanup_queue(OrderQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->front != NULL) {
        Order* temp = dequeue(queue);
        close(temp->client_socket);
        free(temp);
    }
    pthread_mutex_unlock(&queue->mutex);
}

void cleanup_resources() {
    cleanup_queue(&order_queue);
    cleanup_queue(&delivery_queue);
    for (int i = 0; i < cook_thread_pool_size; i++) {
        pthread_cond_destroy(&cooks[i].cond);
    }
    for (int i = 0; i < delivery_thread_pool_size; i++) {
        pthread_cond_destroy(&couriers[i].cond);
    }
    free(cooks);
    free(couriers);
    pthread_mutex_destroy(&mutex_orders);
    pthread_mutex_destroy(&mutex_oven);
    pthread_mutex_destroy(&mutex_delivery);
    pthread_mutex_destroy(&mutex_workers);
    pthread_mutex_destroy(&mutex_clients);
    pthread_cond_destroy(&cond_orders);
    pthread_cond_destroy(&cond_oven);
    pthread_cond_destroy(&cond_delivery);
    pthread_mutex_destroy(&order_queue.mutex);
    pthread_mutex_destroy(&delivery_queue.mutex);
}
