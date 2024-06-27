#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#define MAX_PATH 4096

typedef struct {
    char src_path[MAX_PATH];
    char dest_path[MAX_PATH];
} file_pair_t;

file_pair_t *buffer;
int buffer_size;
int buffer_count = 0;
int done_flag = 0;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;
pthread_barrier_t worker_barrier;

int num_regular_files = 0;
int num_fifo_files = 0;
int num_directories = 0;
long total_bytes_copied = 0;

void handle_signal(int signal);
void *manager_function(void *args);
void traverse_directory(const char *source_dir, const char *dest_dir);
void *worker_function(void *args);
void copy_file(const char *src_path, const char *dest_path);
void add_to_buffer(const char *src_path, const char *dest_path);
int get_from_buffer(char *src_path, char *dest_path);
void set_done_flag();
int done_flag_set();
void print_usage_and_exit(const char *prog_name);
void print_statistics(int num_workers, int buffer_size, struct timeval start, struct timeval end);

int main(int argc, char *argv[]) {
    if (argc != 5) {
        print_usage_and_exit(argv[0]);
    }

    buffer_size = atoi(argv[1]);
    int num_workers = atoi(argv[2]);
    char *source_dir = argv[3];
    char *dest_dir = argv[4];

    if (buffer_size <= 0 || num_workers <= 0) {
        print_usage_and_exit(argv[0]);
    }

    buffer = malloc(buffer_size * sizeof(file_pair_t));
    if (buffer == NULL) {
        perror("Failed to allocate buffer");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handle_signal);

    pthread_t manager_thread;
    pthread_t worker_threads[num_workers];

    if (pthread_barrier_init(&worker_barrier, NULL, num_workers + 1) != 0) {
        perror("Failed to initialize barrier");
        free(buffer);
        exit(EXIT_FAILURE);
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    if (pthread_create(&manager_thread, NULL, manager_function, (void *)argv) != 0) {
        perror("Failed to create manager thread");
        free(buffer);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker_function, NULL) != 0) {
            perror("Failed to create worker thread");
            free(buffer);
            exit(EXIT_FAILURE);
        }
    }

    pthread_barrier_wait(&worker_barrier);
    printf("Main thread waits for all workers\n");// Main thread waits for all workers
    pthread_join(manager_thread, NULL);

    for (int i = 0; i < num_workers; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    gettimeofday(&end, NULL);

    print_statistics(num_workers, buffer_size, start, end);

    pthread_barrier_destroy(&worker_barrier);
    free(buffer);
    return 0;
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        printf("SIGINT received, shutting down...\n");
        set_done_flag();
    }
}

void *manager_function(void *args) {
    char **argv = (char **)args;
    char *source_dir = argv[3];
    char *dest_dir = argv[4];

    traverse_directory(source_dir, dest_dir);
    set_done_flag();
    return NULL;
}

void traverse_directory(const char *source_dir, const char *dest_dir) {
    DIR *src_dir = opendir(source_dir);
    if (!src_dir) {
        perror("Failed to open source directory");
        return;
    }

    if (mkdir(dest_dir, 0755) < 0 && errno != EEXIST) {
        perror("Failed to create destination directory");
        closedir(src_dir);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(src_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char src_path[MAX_PATH];
        char dest_path[MAX_PATH];
        snprintf(src_path, MAX_PATH, "%s/%s", source_dir, entry->d_name);
        snprintf(dest_path, MAX_PATH, "%s/%s", dest_dir, entry->d_name);

        if (entry->d_type == DT_REG) {
            num_regular_files++;
            add_to_buffer(src_path, dest_path);
        } else if (entry->d_type == DT_FIFO) {
            num_fifo_files++;
        } else if (entry->d_type == DT_DIR) {
            num_directories++;
            traverse_directory(src_path, dest_path);
        }
    }

    closedir(src_dir);
}

void *worker_function(void *args) {
    while (!done_flag_set() || buffer_count > 0) {
        char src_path[MAX_PATH];
        char dest_path[MAX_PATH];
        if (get_from_buffer(src_path, dest_path)) {
            if (src_path[0] != '\0' && dest_path[0] != '\0') {
                copy_file(src_path, dest_path);
            } else {
                fprintf(stderr, "Invalid paths received by worker\n");
            }
        }
    }
    pthread_barrier_wait(&worker_barrier);
    printf("Worker thread reached the barrier\n");// Each worker waits at the barrier
    return NULL;
}

void copy_file(const char *src_path, const char *dest_path) {
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        perror("Failed to open source file");
        return;
    }

    int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd < 0) {
        perror("Failed to open destination file");
        close(src_fd);
        return;
    }

    char buffer[8192];

    ssize_t bytes_read, bytes_written;

    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("Failed to write to destination file");
            close(src_fd);
            close(dest_fd);
            return;
        }
        total_bytes_copied += bytes_written;
    }
    printf("Copied file from %s to %s\n", src_path, dest_path); // Dosya kopyalama mesajı eklendi
    close(src_fd);
    close(dest_fd);
}

void add_to_buffer(const char *src_path, const char *dest_path) {
    pthread_mutex_lock(&buffer_mutex);

    while (buffer_count == buffer_size && !done_flag) {
        pthread_cond_wait(&buffer_not_full, &buffer_mutex);
    }

    if (done_flag) {
        pthread_mutex_unlock(&buffer_mutex);
        return;
    }

    strncpy(buffer[buffer_count].src_path, src_path, MAX_PATH);
    strncpy(buffer[buffer_count].dest_path, dest_path, MAX_PATH);
    buffer_count++;

    pthread_cond_signal(&buffer_not_empty);
    pthread_mutex_unlock(&buffer_mutex);
}

int get_from_buffer(char *src_path, char *dest_path) {
    pthread_mutex_lock(&buffer_mutex);

    while (buffer_count == 0 && !done_flag) {
        pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
    }

    if (buffer_count == 0 && done_flag) {
        pthread_mutex_unlock(&buffer_mutex);
        return 0;
    }

    buffer_count--;
    strncpy(src_path, buffer[buffer_count].src_path, MAX_PATH);
    strncpy(dest_path, buffer[buffer_count].dest_path, MAX_PATH);

    pthread_cond_signal(&buffer_not_full);
    pthread_mutex_unlock(&buffer_mutex);

    return 1;
}

void set_done_flag() {
    pthread_mutex_lock(&buffer_mutex);
    done_flag = 1;
    pthread_cond_broadcast(&buffer_not_empty);
    pthread_cond_broadcast(&buffer_not_full);
    pthread_mutex_unlock(&buffer_mutex);
}

int done_flag_set() {
    pthread_mutex_lock(&buffer_mutex);
    int flag = done_flag;
    pthread_mutex_unlock(&buffer_mutex);
    return flag;
}

void print_usage_and_exit(const char *prog_name) {
    fprintf(stderr, "Usage: %s <buffer_size> <num_workers> <source_dir> <dest_dir>\n", prog_name);
    exit(EXIT_FAILURE);
}

void print_statistics(int num_workers, int buffer_size, struct timeval start, struct timeval end) {
    long seconds = (end.tv_sec - start.tv_sec);
    long useconds = (end.tv_usec - start.tv_usec);
    long mtime = ((seconds) * 1000 + useconds / 1000.0) + 0.5;
    long minutes = mtime / 60000;
    seconds = (mtime % 60000) / 1000;
    long miliseconds = mtime % 1000;

    printf("\n---------------STATISTICS--------------------\n");
    printf("Consumers: %d - Buffer Size: %d\n", num_workers, buffer_size);
    printf("Number of Regular File: %d\n", num_regular_files);
    printf("Number of FIFO File: %d\n", num_fifo_files);
    printf("Number of Directory: %d\n", num_directories);
    printf("TOTAL BYTES COPIED: %ld\n", total_bytes_copied);
    printf("TOTAL TIME: %02ld:%02ld.%03ld (min:sec.mili)\n", minutes, seconds, miliseconds);
}
