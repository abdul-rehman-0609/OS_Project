#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>

#define BUFFER_SIZE 10
#define MAX_PRIORITY 2

// Thresholds for generating stock alerts
#define LOW_STOCK_THRESHOLD 1
#define HIGH_STOCK_THRESHOLD 9

int NUM_PRODUCERS;
int NUM_CONSUMERS;

// Shared buffer for normal items
int buffer[BUFFER_SIZE];
int in = 0, out = 0;

// Separate buffer for priority (urgent) items
int urgent_buffer[BUFFER_SIZE];
int urgent_in = 0, urgent_out = 0;
int urgent_count = 0; // Tracks number of urgent items

// Semaphores for tracking empty and full slots
sem_t empty, full;

// Mutex for critical section to prevent race conditions
pthread_mutex_t mutex;

int stop_simulation = 0; // 0 means running, 1 means stop

// Function prototypes
void handle_sigint(int sig);
void log_event(const char* event, int id, int item, const char* type);
void log_stock_status(int normal_count, int urgent_count);
void log_command(const char* command, int item, int priority);
void log_error(const char* error);
void update_statistics(int produced, int consumed);
void* supplier(void* arg);
void* retailer(void* arg);
void stock_alert();
void add_product(int item, int priority);
int extract_product();
void print_stock_status();
void* user_input(void* arg);

// Signal handler for Ctrl+C
void handle_sigint(int sig) {
    printf("\nCaught signal %d (Ctrl+C). Exiting simulation...\n", sig);
    stop_simulation = 1;

    // Unblock any threads waiting on semaphores
    for (int i = 0; i < NUM_PRODUCERS; i++) sem_post(&empty);
    for (int i = 0; i < NUM_CONSUMERS; i++) sem_post(&full);
}

// Logging functions
void log_event(const char* event, int id, int item, const char* type) {
    printf("[LOG] %s: Thread %d %s item %d\n", event, id, type, item);
}

void log_stock_status(int normal_count, int urgent_count) {
    printf("[LOG] Current stock: Normal items = %d, Urgent items = %d\n", normal_count, urgent_count);
}

void log_command(const char* command, int item, int priority) {
    printf("[LOG] Command: %s item %d with priority %d\n", command, item, priority);
}

void log_error(const char* error) {
    printf("[ERROR] %s\n", error);
}

int total_produced = 0, total_consumed = 0;

void update_statistics(int produced, int consumed) {
    total_produced += produced;
    total_consumed += consumed;
    printf("[STATS] Total Produced: %d, Total Consumed: %d\n", total_produced, total_consumed);
}

// Producer thread function
void* supplier(void* arg) {
    int id = (long)arg;
    while (!stop_simulation) {
        int item = rand() % 100;
        int priority = rand() % MAX_PRIORITY;
        sleep(1);

        if (stop_simulation) break;
        sem_wait(&empty);

        pthread_mutex_lock(&mutex);
        if (stop_simulation) {
            pthread_mutex_unlock(&mutex);
            sem_post(&empty);
            break;
        }

        add_product(item, priority);
        log_event("Produced", id, item, priority ? "(PRIORITY)" : "");
        update_statistics(1, 0);
        printf("Producer %d produced %d %s\n", id, item, priority ? "(PRIORITY)" : "");
        stock_alert();

        pthread_mutex_unlock(&mutex);
        sem_post(&full);
    }
    return NULL;
}

// Consumer thread function
void* retailer(void* arg) {
    int id = (long)arg;
    while (!stop_simulation) {
        if (stop_simulation) break;
        sem_wait(&full);

        pthread_mutex_lock(&mutex);
        if (stop_simulation) {
            pthread_mutex_unlock(&mutex);
            sem_post(&full);
            break;
        }

        int item = extract_product();
        log_event("Consumed", id, item, "");
        update_statistics(0, 1);
        printf("Consumer %d consumed %d\n", id, item);
        stock_alert();

        pthread_mutex_unlock(&mutex);
        sem_post(&empty);
        sleep(2);
    }
    return NULL;
}

// Add product to buffer
void add_product(int item, int priority) {
    if ((in + 1) % BUFFER_SIZE == out) {
        log_error("Buffer overflow");
        return;
    }

    if (priority) {
        urgent_buffer[urgent_in] = item;
        urgent_in = (urgent_in + 1) % BUFFER_SIZE;
        urgent_count++;
    } else {
        buffer[in] = item;
        in = (in + 1) % BUFFER_SIZE;
    }
}

// Extract product from buffer
int extract_product() {
    if (in == out && urgent_count == 0) {
        log_error("Buffer underflow");
        return -1;
    }

    int item;
    if (urgent_count > 0) {
        item = urgent_buffer[urgent_out];
        urgent_out = (urgent_out + 1) % BUFFER_SIZE;
        urgent_count--;
    } else {
        item = buffer[out];
        out = (out + 1) % BUFFER_SIZE;
    }
    return item;
}

// Stock alert function
void stock_alert() {
    int normal_count = (in - out + BUFFER_SIZE) % BUFFER_SIZE;
    int total_stock = normal_count + urgent_count;

    if (total_stock <= LOW_STOCK_THRESHOLD) {
        printf("[STOCK ALERT] LOW stock: %d items in warehouse!\n", total_stock);
    } else if (total_stock >= HIGH_STOCK_THRESHOLD) {
        printf("[STOCK ALERT] HIGH stock: %d items in warehouse!\n", total_stock);
    }
    log_stock_status(normal_count, urgent_count);
}

// Print stock status
void print_stock_status() {
    int normal_count = (in - out + BUFFER_SIZE) % BUFFER_SIZE;
    log_stock_status(normal_count, urgent_count);
}

// User input thread function
void* user_input(void* arg) {
    char command[10];
    while (!stop_simulation) {
        printf("\nEnter command: [add, remove, status, exit] ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            if (stop_simulation) break;
            continue;
        }

        command[strcspn(command, "\n")] = '\0';
        if (stop_simulation) break;

        if (strcmp(command, "status") == 0) {
            pthread_mutex_lock(&mutex);
            print_stock_status();
            pthread_mutex_unlock(&mutex);
        } else if (strcmp(command, "add") == 0) {
            char item_input[10], priority_input[10];
            int item, priority;

            printf("Enter item to add: ");
            if (fgets(item_input, sizeof(item_input), stdin) == NULL || stop_simulation) break;
            item = atoi(item_input);

            printf("Enter priority (0 for normal, 1 for urgent): ");
            if (fgets(priority_input, sizeof(priority_input), stdin) == NULL || stop_simulation) break;
            priority = atoi(priority_input);

            pthread_mutex_lock(&mutex);
            add_product(item, priority);
            log_command("add", item, priority);
            stock_alert();
            pthread_mutex_unlock(&mutex);
        } else if (strcmp(command, "remove") == 0) {
            pthread_mutex_lock(&mutex);
            if ((in - out + BUFFER_SIZE) % BUFFER_SIZE > 0 || urgent_count > 0) {
                int item = extract_product();
                log_command("remove", item, 0);
                stock_alert();
            } else {
                printf("No items to remove!\n");
            }
            pthread_mutex_unlock(&mutex);
        } else if (strcmp(command, "exit") == 0) {
            printf("Exiting simulation...\n");
            stop_simulation = 1;
            for (int i = 0; i < NUM_PRODUCERS; i++) sem_post(&empty);
            for (int i = 0; i < NUM_CONSUMERS; i++) sem_post(&full);
            break;
        } else {
            printf("Unknown command. Try again.\n");
        }
    }
    return NULL;
}

// Main function
int main() {
    srand(time(NULL));
    signal(SIGINT, handle_sigint);

    printf("Enter number of suppliers: ");
    scanf("%d", &NUM_PRODUCERS);

    printf("Enter number of retailers: ");
    scanf("%d", &NUM_CONSUMERS);

    sem_init(&empty, 0, BUFFER_SIZE);
    sem_init(&full, 0, 0);
    pthread_mutex_init(&mutex, NULL);

    pthread_t prod_threads[NUM_PRODUCERS], cons_threads[NUM_CONSUMERS], user_thread;

    for (int i = 0; i < NUM_PRODUCERS; i++)
        pthread_create(&prod_threads[i], NULL, supplier, (void*)(long)i);

    for (int i = 0; i < NUM_CONSUMERS; i++)
        pthread_create(&cons_threads[i], NULL, retailer, (void*)(long)i);

    pthread_create(&user_thread, NULL, user_input, NULL);

    for (int i = 0; i < NUM_PRODUCERS; i++)
        pthread_join(prod_threads[i], NULL);

    for (int i = 0; i < NUM_CONSUMERS; i++)
        pthread_join(cons_threads[i], NULL);

    pthread_join(user_thread, NULL);

    sem_destroy(&empty);
    sem_destroy(&full);
    pthread_mutex_destroy(&mutex);
    printf("hello world");
    return 0;
}
