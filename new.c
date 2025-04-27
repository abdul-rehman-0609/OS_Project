//STEP 1: Please run: " gcc new.c -o output -lpthread -lncurses " on terminal.
//STEP 2: Create a new file in your directory where source code is placed using: " touch warehouse.log " use the provided file name only as it is used in the code. 
//STEP 3: For output, please run: " ./output " on terminal.
//STEP 4: Press " Ctrl + C " for forced termination else wait for the simulation to be ended automatically.

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <ncurses.h>

#define BUFFER_SIZE 10
#define MAX_PRIORITY 2

char last_action[100] = "Waiting...";
volatile sig_atomic_t simulation_running = 1;

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

int simulation_count;
int total_produced = 0;
int total_consumed = 0;

// Function prototypes
void refresh_screen();
void log_error(const char* error);
void update_statistics(int produced, int consumed);
void* supplier(void* arg);
void* retailer(void* arg);
void add_product(int item, int priority);
int extract_product();
void print_final_statistics();  
void open_log_file();
void close_log_file();
void log_event_file(const char* event, int id, int item, const char* type);
void sigint_handler(int sig);

void refresh_screen() {
    clear();
    box(stdscr, 0, 0);

    mvprintw(1, 2, "Warehouse Simulation (Suppliers: %d, Retailers: %d)", NUM_PRODUCERS, NUM_CONSUMERS);
    mvprintw(3, 2, "Normal Items in Buffer: %d", (in - out + BUFFER_SIZE) % BUFFER_SIZE);
    mvprintw(4, 2, "Urgent Items in Buffer: %d", urgent_count);
    mvprintw(6, 2, "Total Produced: %d", total_produced);
    mvprintw(7, 2, "Total Consumed: %d", total_consumed);
    mvprintw(9, 2, "Last Action: %s", last_action);

    int normal_count = (in - out + BUFFER_SIZE) % BUFFER_SIZE;
    int total_stock = normal_count + urgent_count;

    if (total_stock <= LOW_STOCK_THRESHOLD) {
        mvprintw(11, 2, "[STOCK ALERT] LOW stock: %d items!", total_stock);
    } else if (total_stock >= HIGH_STOCK_THRESHOLD) {
        mvprintw(11, 2, "[STOCK ALERT] HIGH stock: %d items!", total_stock);
    }
    refresh();
}

void print_final_statistics() {
    printf("\nSimulation ended.\n");
    printf("Final statistics:\n");
    printf("Total Produced: %d, Total Consumed: %d\n", total_produced, total_consumed);
    printf("Final stock status: Normal items = %d, Urgent items = %d\n", (in - out + BUFFER_SIZE) % BUFFER_SIZE, urgent_count);

    printf("Exiting program...\n");
    fflush(stdout);
    sleep(1); // Give time for logs to flush
    printf("Goodbye!\n");
    fflush(stdout);
    sleep(1); // Give time for logs to flush
}

// Signal handler for Ctrl+C
void sigint_handler(int sig) {

    simulation_running = 0;
    endwin(); // End ncurses mode

    printf("\n\nSignal handler triggered!\n");
    printf("\nCaught signal %d (Ctrl+C). Exiting simulation...\n", sig);
    fflush(stdout);

    // Set simulation_count to 0 to signal threads to exit
    pthread_mutex_lock(&mutex);
    simulation_count = 0;
    pthread_mutex_unlock(&mutex);

    // Unblock any threads waiting on semaphores
    for (int i = 0; i < NUM_PRODUCERS; i++) sem_post(&empty);
    for (int i = 0; i < NUM_CONSUMERS; i++) sem_post(&full);

    print_final_statistics();
    close_log_file(); 

    exit(0);
}

void log_error(const char* error) {
    printf("[ERROR] %s\n", error);
}

void update_statistics(int produced, int consumed) {
    total_produced += produced;
    total_consumed += consumed;
}

FILE* log_fp = NULL;

void open_log_file() {
    log_fp = fopen("warehouse.log", "a");
    if (!log_fp) {
        printf("[ERROR] Could not open log file!\n");
        exit(1);
    }
}

void close_log_file() {
    if (log_fp) fclose(log_fp);
}

void log_event_file(const char* event, int id, int item, const char* type) {
    if (!log_fp) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(log_fp, "[%04d-%02d-%02d %02d:%02d:%02d] [LOG] %s: Thread %d %s item %d\n",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec,
        event, id, type, item);
    fflush(log_fp);
}

// Producer thread function
void* supplier(void* arg) {
    int id = (long)arg;
    while (1) {
        pthread_mutex_lock(&mutex);
        if (simulation_count <= 0) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);

        int item = rand() % 100;
        int priority = rand() % MAX_PRIORITY;
        sleep(1);

        sem_wait(&empty);
        pthread_mutex_lock(&mutex);

        add_product(item, priority);

        snprintf(last_action, sizeof(last_action), "Supplier %d produced item -> [%d] %s", id, item, priority ? "(PRIORITY)" : "");
        if (simulation_running) {
            refresh_screen();
        }

        log_event_file("Produced", id, item, priority ? "(PRIORITY)" : "");

        update_statistics(1, 0);

        pthread_mutex_unlock(&mutex);
        sem_post(&full);

        sleep(2); // Simulate time taken to produce
    }
    return NULL;
}

// Consumer thread function
void* retailer(void* arg) {
    int id = (long)arg;
    while (1) {
        pthread_mutex_lock(&mutex);
        if (simulation_count <= 0) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        simulation_count--;
        pthread_mutex_unlock(&mutex);
        
        sem_wait(&full);

        pthread_mutex_lock(&mutex);
        if (in == out && urgent_count == 0) {
            pthread_mutex_unlock(&mutex);
            sem_post(&full);
            continue; // No items to consume
        }
        // Simulate time taken to consume
        sleep(1);

        // Extract product from buffer
        int item = extract_product();
        

        snprintf(last_action, sizeof(last_action), "Retailer %d consumed item -> [%d]", id, item);
        if (simulation_running) {
            refresh_screen();
        }

        log_event_file("Consumed", id, item, "");

        if (item == -1) {
            pthread_mutex_unlock(&mutex);
            sem_post(&full);
            continue; // No items to consume
        }

        update_statistics(0, 1);

        pthread_mutex_unlock(&mutex);
        sem_post(&empty);
        
        sleep(3);
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

// Main function
int main() {
    srand(time(NULL));
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        printf("Error setting up signal handler for SIGINT\n");
        return 1;
    }
    open_log_file();
    

    printf("Welcome to the Warehouse Simulation!\n");
    sleep(2);
    printf("This simulation will run until you press Ctrl+C or your simulation counter is ended.\n");
    sleep(1);

    printf("Enter number of suppliers: ");
    while (scanf("%d", &NUM_PRODUCERS) != 1 || NUM_PRODUCERS <= 0) {
        printf("Invalid input. Enter a positive integer for number of suppliers: ");
        while (getchar() != '\n'); // clear input buffer
    }

    printf("Enter number of retailers: ");
    while (scanf("%d", &NUM_CONSUMERS) != 1 || NUM_CONSUMERS <= 0) {
        printf("Invalid input. Enter a positive integer for number of retailers: ");
        while (getchar() != '\n'); // clear input buffer
    }

    printf("Enter number of items to be consumed (to bound the simulation): ");
    while (scanf("%d", &simulation_count) != 1 || simulation_count <= 0) {
        printf("Invalid input. Enter a positive integer for number of items: ");
        while (getchar() != '\n'); // clear input buffer
    }

    initscr();      // Start ncurses mode
    cbreak();       // Disable line buffering
    noecho();       // Don't echo input
    curs_set(FALSE);// Hide the cursor

    sem_init(&empty, 0, BUFFER_SIZE);
    sem_init(&full, 0, 0);
    pthread_mutex_init(&mutex, NULL);

    pthread_t prod_threads[NUM_PRODUCERS], cons_threads[NUM_CONSUMERS];
    
    for (int i = 0; i < NUM_PRODUCERS; i++)
        pthread_create(&prod_threads[i], NULL, supplier, (void*)(long)(i+1));

    for (int i = 0; i < NUM_CONSUMERS; i++)
        pthread_create(&cons_threads[i], NULL, retailer, (void*)(long)(i+1));

    for (int i = 0; i < NUM_PRODUCERS; i++)
        pthread_join(prod_threads[i], NULL);

    for (int i = 0; i < NUM_CONSUMERS; i++)
        pthread_join(cons_threads[i], NULL);

    sem_destroy(&empty);
    sem_destroy(&full);
    pthread_mutex_destroy(&mutex);
    close_log_file();
    endwin(); // End ncurses mode
    print_final_statistics();
    return 0;
}
