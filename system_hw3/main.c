#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#define OWNER_T_NUMBER 20

// Semaphores and Mutexes
sem_t newPickup, inChargeforPickup;
sem_t newAutomobile, inChargeforAutomobile;
pthread_mutex_t mutex_automobile, mutex_pickup;
pthread_mutex_t main_lot_mutex;

pthread_t pickupAttendantThread, automobileAttendantThread;
pthread_t freePickupThread, freeAutomobileThread;
pthread_t ownerThreads[OWNER_T_NUMBER];

// Temporary parking spot counters (initially 1 for both pickups and automobiles)
int tempFree_automobile = 1;
int tempFree_pickup = 1;

// Main parking lot counters (initially 8 for automobiles and 4 for pickups)
int mFree_automobile = 8;
int mFree_pickup = 4;

// Signal handler to clean up and exit
void handle_sigint(int sig) {
    printf("\nExiting the Program!!!\n");

    pthread_cancel(pickupAttendantThread);
    pthread_cancel(automobileAttendantThread);
    pthread_cancel(freePickupThread);
    pthread_cancel(freeAutomobileThread);

    pthread_join(pickupAttendantThread, NULL);
    pthread_join(automobileAttendantThread, NULL);
    pthread_join(freePickupThread, NULL);
    pthread_join(freeAutomobileThread, NULL);

    sem_destroy(&newPickup);
    sem_destroy(&inChargeforPickup);
    sem_destroy(&newAutomobile);
    sem_destroy(&inChargeforAutomobile);
    pthread_mutex_destroy(&mutex_automobile);
    pthread_mutex_destroy(&mutex_pickup);
    pthread_mutex_destroy(&main_lot_mutex);

    for (int i = 0; i < OWNER_T_NUMBER; i++) {
        pthread_join(ownerThreads[i], NULL);
    }

    exit(0);
}

// Car Owner thread function
void* carOwner(void* arg) {
    int vehicleType = *((int*)arg);
    free(arg);

    if (vehicleType % 2 == 1) {  // Pickup
        pthread_mutex_lock(&mutex_pickup);
        if (tempFree_pickup > 0) {
            tempFree_pickup--;
            printf("-Pickup OWNER ARRIVED, temporary pickup spots left: %d\n", tempFree_pickup);
            sem_post(&newPickup);
            pthread_mutex_unlock(&mutex_pickup);
            sem_wait(&inChargeforPickup);
            pthread_mutex_lock(&main_lot_mutex);
            if (mFree_pickup > 0) {
                mFree_pickup--;
                printf("Pickup PARKED in the main lot, main pickup spots left: %d\n", mFree_pickup);
            } else {
                printf("Main parking lot for pickups is full. Pickup cannot be parked.\n");
            }
            pthread_mutex_unlock(&main_lot_mutex);
        } else {
            printf("-Pickup OWNER ARRIVED but no temporary spot available\n");
            pthread_mutex_unlock(&mutex_pickup);
        }
    } else {  // Automobile
        pthread_mutex_lock(&mutex_automobile);
        if (tempFree_automobile > 0) {
            tempFree_automobile--;
            printf("-Automobile OWNER ARRIVED, temporary automobile spots left: %d\n", tempFree_automobile);
            sem_post(&newAutomobile);
            pthread_mutex_unlock(&mutex_automobile);
            sem_wait(&inChargeforAutomobile);
            pthread_mutex_lock(&main_lot_mutex);
            if (mFree_automobile > 0) {
                mFree_automobile--;
                printf("Automobile PARKED in the main lot, main automobile spots left: %d\n", mFree_automobile);
            } else {
                printf("Main parking lot for automobiles is full. Automobile cannot be parked.\n");
            }
            pthread_mutex_unlock(&main_lot_mutex);
        } else {
            printf("-Automobile OWNER ARRIVED but no temporary spot available\n");
            pthread_mutex_unlock(&mutex_automobile);
        }
    }
    return NULL;
}

// Car Attendant thread function for pickups
void* pickupAttendant(void* arg) {
    while (1) {
        sem_wait(&newPickup);
        pthread_mutex_lock(&mutex_pickup);
        tempFree_pickup++;
        printf("Pickup ATTENDANT MOVED a pickup, temporary pickup spots left: %d\n", tempFree_pickup);
        pthread_mutex_unlock(&mutex_pickup);
        sem_post(&inChargeforPickup);
    }
    return NULL;
}

// Car Attendant thread function for automobiles
void* automobileAttendant(void* arg) {
    while (1) {
        sem_wait(&newAutomobile);
        pthread_mutex_lock(&mutex_automobile);
        tempFree_automobile++;
        printf("Automobile ATTENDANT MOVED an automobile, temporary automobile spots left: %d\n", tempFree_automobile);
        pthread_mutex_unlock(&mutex_automobile);
        sem_post(&inChargeforAutomobile);
    }
    return NULL;
}

// Main lot freeing function for pickups
void* mainLotFreePickup(void* arg) {
    while (1) {
        usleep((rand() % 2000 + 1000) * 1000);  // Simulate some delay before freeing up spots
        pthread_mutex_lock(&main_lot_mutex);
        if (mFree_pickup < 4) {
            mFree_pickup++;
            printf("Freed up a spot in main lot for pickups, main pickup spots left: %d\n", mFree_pickup);
        }
        pthread_mutex_unlock(&main_lot_mutex);
    }
    return NULL;
}

// Main lot freeing function for automobiles
void* mainLotFreeAutomobile(void* arg) {
    while (1) {
        usleep((rand() % 2000 + 1000) * 1000);  // Simulate some delay before freeing up spots
        pthread_mutex_lock(&main_lot_mutex);
        if (mFree_automobile < 8) {
            mFree_automobile++;
            printf("Freed up a spot in main lot for automobiles, main automobile spots left: %d\n", mFree_automobile);
        }
        pthread_mutex_unlock(&main_lot_mutex);
    }
    return NULL;
}

int main() {
    signal(SIGINT, handle_sigint);

    sem_init(&newPickup, 0, 0);
    sem_init(&inChargeforPickup, 0, 0);
    sem_init(&newAutomobile, 0, 0);
    sem_init(&inChargeforAutomobile, 0, 0);

    pthread_mutex_init(&mutex_automobile, NULL);
    pthread_mutex_init(&mutex_pickup, NULL);
    pthread_mutex_init(&main_lot_mutex, NULL);

    pthread_create(&pickupAttendantThread, NULL, pickupAttendant, NULL);
    pthread_create(&automobileAttendantThread, NULL, automobileAttendant, NULL);
    pthread_create(&freePickupThread, NULL, mainLotFreePickup, NULL);
    pthread_create(&freeAutomobileThread, NULL, mainLotFreeAutomobile, NULL);

    srand(time(NULL)); // Random generator seed

    for (int i = 0; i < OWNER_T_NUMBER; i++) {
        int* vehicleType = malloc(sizeof(int));
        *vehicleType = rand() % 2;  // Randomly generate vehicle type (0 for automobile, 1 for pickup)
        pthread_create(&ownerThreads[i], NULL, carOwner, vehicleType);
        usleep(500000);  // Simulate time delay between vehicle arrivals
    }
    /*/Scenario 2 - Only automobiles
    for (int i = 0; i < 20; i++) {
        int* vehicleType = malloc(sizeof(int));
        *vehicleType = 0;  // Only automobiles
        pthread_t ownerThread;
        pthread_create(&ownerThread, NULL, carOwner, vehicleType);
        usleep(500000);  // Simulate time delay between vehicle arrivals
    }*/
    /*/Scenario 3 - Only pickups
    for (int i = 0; i < 20; i++) {
        int* vehicleType = malloc(sizeof(int));
        *vehicleType = 1;  // Only pickups
        pthread_t ownerThread;
        pthread_create(&ownerThread, NULL, carOwner, vehicleType);
        usleep(500000);  // Simulate time delay between vehicle arrivals
    }*/
    /*/Scenario 4 - Rand time
     for (int i = 0; i < 20; i++) {
        int* vehicleType = malloc(sizeof(int));
        *vehicleType = rand() % 2;  // Randomly generate 0 or 1
        pthread_t ownerThread;
        pthread_create(&ownerThread, NULL, carOwner, vehicleType);
        usleep((rand() % 5 + 1) * 200000);  // Random time delay between vehicle arrivals
    }*/

    pthread_join(pickupAttendantThread, NULL);
    pthread_join(automobileAttendantThread, NULL);
    pthread_join(freePickupThread, NULL);
    pthread_join(freeAutomobileThread, NULL);

    return 0;
}
