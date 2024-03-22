#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <stddef.h>
#include <string.h>  

typedef struct {
    int length;
    int speed;
    int id;
    char* starting_point;
    char* destination_point;
    char* arrival_time;
    char* departure_time;
} Train;

typedef struct TrainNode {
    Train train;
    struct TrainNode *next;
} TrainNode;

TrainNode *head_AC = NULL;
TrainNode *head_BC = NULL;
TrainNode *head_ED = NULL;
TrainNode *head_FD = NULL;

// Global variables
double p = 0.5; // Default probability for train arrival at A or E/F
int simulation_time = 60; 
int tunnel_length = 100;  
int train_speed = 100;     
int max_trains = 10;
int current_trains_count = 0;
int train_counter = 0;  // To assign train IDs

// Mutexes and condition variables
pthread_mutex_t mutex_queues = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_tunnel = PTHREAD_COND_INITIALIZER;

// System overload state
int system_overloaded = 0;
time_t overload_start_time;
time_t overload_end_time;

// Tunnel state
int tunnel_occupied = 0;
volatile sig_atomic_t shutdown_flag = 0;

char* get_timestamp() {
    time_t rawtime;
    time(&rawtime);
    struct tm *timeinfo = localtime(&rawtime);

    static char time_string[9];
    strftime(time_string, sizeof(time_string), "%H:%M:%S", timeinfo);

    return time_string;
}

int get_queue_size(TrainNode *head) {
    int size = 0;
    TrainNode *current = head;
    while (current != NULL) {
        size++;
        current = current->next;
    }
    return size;
}

// Keeping Logs START
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *train_log_file;
FILE *control_center_log_file;

void close_log_files() {
    fclose(train_log_file);
    fclose(control_center_log_file);
}

void extract_train_IDs(TrainNode *head, int *trainIDs, int *index) {
    TrainNode *current = head;
    while (current != NULL) {
        trainIDs[(*index)++] = current->train.id;
        current = current->next;
    }
}

char* concatenateWithCommas(const int* array, int size) {
    // No waiting train case
    if (size <= 0 || array == NULL) {
        return NULL;
    }

    // Each element needs space for digits and a comma, except the last element
    int totalLength = 0;
    for (int i = 0; i < size - 1; ++i) {
        totalLength += snprintf(NULL, 0, "%d,", array[i]);
    }
    // Add space for the last element and the null terminator
    totalLength += snprintf(NULL, 0, "%d", array[size - 1]) + 1;

    char* result = (char*)malloc(totalLength * sizeof(char));
    if (result == NULL) {
        return NULL; // Memory allocation failed
    }

    int offset = 0;
    for (int i = 0; i < size - 1; ++i) {
        offset += snprintf(result + offset, totalLength - offset, "%d,", array[i]);
    }
    // Add the last element
    snprintf(result + offset, totalLength - offset, "%d", array[size - 1]);

    return result;
}

// To get Train Ids from the TrainNodes, sort them and create the trains waiting passage string
// Trains waiting passage's order is ascending order according to their train IDs
char* create_trains_waiting_string() {
    
    int totalTrains = get_queue_size(head_AC) + get_queue_size(head_BC) + get_queue_size(head_ED) + get_queue_size(head_FD);
    
    int *trainIDs = (int*)malloc(totalTrains * sizeof(int));

    int currentIndex = 0;

    extract_train_IDs(head_AC, trainIDs, &currentIndex);
    extract_train_IDs(head_BC, trainIDs, &currentIndex);
    extract_train_IDs(head_ED, trainIDs, &currentIndex);
    extract_train_IDs(head_FD, trainIDs, &currentIndex);

    // Sort the IDs in ascending order
    for (int i = 0; i < totalTrains - 1; i++) {
        for (int j = 0; j < totalTrains - i - 1; j++) {
            if (trainIDs[j] > trainIDs[j + 1]) {
                int temp = trainIDs[j];
                trainIDs[j] = trainIDs[j + 1];
                trainIDs[j + 1] = temp;
            }
        }
    }

    char *result = concatenateWithCommas(trainIDs, totalTrains);

    free(trainIDs);
    return result;
}

void log_train_event(int id, const char* starting_point, const char* destination_point, int length, const char* arrival_time, const char* departure_time) {
    pthread_mutex_lock(&log_mutex);

    fprintf(train_log_file, "%-10d %-15s %-15s %-10d %-20s %-20s\n", id, starting_point,
            destination_point, length, arrival_time, departure_time);

    pthread_mutex_unlock(&log_mutex);
}

void log_control_center_event(const char* event, const char* event_time, char* id) {
    pthread_mutex_lock(&log_mutex);

    char* trains_waiting_passage = create_trains_waiting_string();
    if(trains_waiting_passage == NULL){
        trains_waiting_passage = " ";
    }
   
    fprintf(control_center_log_file, "%-20s %-20s %-10s %-s\n", event, event_time, id, trains_waiting_passage);

    pthread_mutex_unlock(&log_mutex);
}

void log_control_center_event_tunnel_clear(const char* event, const char* event_time, char* id, int overload_duration_sec) {
    pthread_mutex_lock(&log_mutex);

    char* trains_waiting_passage = create_trains_waiting_string();
    if(trains_waiting_passage == NULL){
        trains_waiting_passage = " ";
    }

    char tunnel_clear_string[50];
    snprintf(tunnel_clear_string, sizeof(tunnel_clear_string), "# Time to clear: %d secs", overload_duration_sec);

    fprintf(control_center_log_file, "%-20s %-20s %-10s %-s\n", event, event_time, id, tunnel_clear_string);

    pthread_mutex_unlock(&log_mutex);
}
// Keeping Logs FINISH

void signal_handler(int signum) {
    if (signum == SIGINT) {
        printf("Interrupt signal (SIGINT) received. Initiating shutdown...\n");
        shutdown_flag = 1;
    }
}

void process_train(char queue_id, int train_length, Train train) {
    char idString[10];
    sprintf(idString, "%d", train.id);

    log_control_center_event("Tunnel Passing", get_timestamp(), idString);

    int passage_time = (train_length + tunnel_length) / train_speed;
    printf("[%s] Train with ID %d from queue %c is entering the tunnel.\n", get_timestamp(), train.id, queue_id);

    // PART 3 START
    double breakdown_probability = (double)rand() / RAND_MAX;
    if (breakdown_probability < 0.1) {
        // Breakdown happened
        passage_time += 4;
        printf("[%s] Breakdown! for train with ID %d from queue %c.\n", get_timestamp(), train.id, queue_id);
        log_control_center_event("Breakdown", get_timestamp(), idString);
    }
    // PART 3 FINISH

    sleep(passage_time); 
    printf("[%s] Train with ID %d from queue %c has exited the tunnel.\n", get_timestamp(), train.id, queue_id);

    // Logged the train information after it exits the tunnel, right now we have all the information for the logging
    log_train_event(train.id, train.starting_point, train.destination_point, train.length, train.arrival_time, get_timestamp());
}

void enqueue(TrainNode **head, Train train) {
    TrainNode *new_node = (TrainNode *)malloc(sizeof(TrainNode));
    if (new_node == NULL) {
        fprintf(stderr, "Failed to allocate memory for a new train node.\n");
        return;
    }
    new_node->train = train;
    new_node->next = NULL;

    if (*head == NULL) {
        *head = new_node;
    } else {
        TrainNode *current = *head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_node;
    }
}

TrainNode *dequeue(TrainNode **head) {
    if (head == NULL || *head == NULL) {
        return NULL;
    }
    TrainNode *node_to_remove = *head;
    *head = (*head)->next;
    return node_to_remove;
}

void *train_generator(void *param) {
    double p = *((double *)param);

    srand(time(NULL));

    while (!shutdown_flag) {
        current_trains_count = get_queue_size(head_AC) + get_queue_size(head_BC) + get_queue_size(head_ED) + get_queue_size(head_FD);

        if (system_overloaded && current_trains_count > 0) {
            printf("[%s] System is in overload state. Waiting for all trains to clear the tunnel.\n", get_timestamp());
            sleep(1);
            continue;
        }else if (current_trains_count > 10 && !system_overloaded) { // Check for system overloading condition
            printf("[%s] System overload happened now. Notifying trains to slow down.\n", get_timestamp());
            system_overloaded = 1;
            overload_start_time = time(NULL);
            // Log overload event
            log_control_center_event("System Overload", get_timestamp(), "#");    
        }else if (system_overloaded && current_trains_count == 0) {// Check if all trains have cleared the tunnel to exit overload state
            printf("[%s] System is no longer in overload state. Resuming train generation.\n", get_timestamp());
            system_overloaded = 0;
            overload_end_time = time(NULL);

            int overload_duration = difftime(overload_end_time, overload_start_time);

            // Log Tunnel Cleared Event
            log_control_center_event_tunnel_clear("Tunnel Cleared", get_timestamp(), "#", overload_duration); 
        }else if(!system_overloaded){
            Train *train = malloc(sizeof(Train));
            if (!train) {
                perror("Failed to allocate memory for a new train");
                continue; 
            }
        
            train->speed = 100;

            train->id = train_counter;
            train_counter++;

            double length_prob = (double)rand() / RAND_MAX;
            train->length = length_prob < 0.7 ? 100 : 200;

            char* cur_time = get_timestamp();
            time_t currentTime;
            currentTime = time(NULL);
            char *timeString = ctime(&currentTime);
            
            struct tm *timeInfo;
            timeInfo = localtime(&currentTime);

            char time_string[9];
            strftime(time_string, sizeof(time_string), "%H:%M:%S", timeInfo);

            train->arrival_time = time_string;

            // Randomly determine if a train arrives
            double arrival_prob = (double)rand() / RAND_MAX;
        
            // Decide which queue to add the train based on the arrival probability
            pthread_mutex_lock(&mutex_queues);
        
            if (arrival_prob < p) {
                // Train arrives at A, E, or F with probability p
                double direction_prob = (double)rand() / RAND_MAX;
                double destination_prob = (double)rand() / RAND_MAX;
                if (direction_prob < 1.0 / 3.0) {
                    // add starting and destination points to each of them
                    printf("[%s] Train with ID %d arrived at AC.\n", get_timestamp(), train->id);
                    train->starting_point = "A";
                    if(destination_prob < 0.5){
                        train->destination_point = "E";
                    }else{
                        train->destination_point = "F";
                    }
                    enqueue(&head_AC, *train); // Train from A to C
                } else if (direction_prob < 2.0 / 3.0) {
                    printf("[%s] Train with ID %d arrived at FD.\n", get_timestamp(), train->id);
                    train->starting_point = "F";
                    if(destination_prob < 0.5){
                        train->destination_point = "A";
                    }else{
                        train->destination_point = "B";
                    }
                    enqueue(&head_FD, *train); // Train from F to D
                } else {
                    printf("[%s] Train with ID %d arrived at ED.\n", get_timestamp(), train->id);
                    train->starting_point = "E";
                    if(destination_prob < 0.5){
                        train->destination_point = "A";
                    }else{
                        train->destination_point = "B";
                    }
                    enqueue(&head_ED, *train);  // Train from E to D
                }
            } else {
                // Train arrives at B with probability 1 - p
                double destination_prob = (double)rand() / RAND_MAX;
                printf("[%s] Train with ID %d arrived at BC.\n", get_timestamp(), train->id);
                train->starting_point = "B";
                if(destination_prob < 0.5){
                    train->destination_point = "E";
                }else{
                    train->destination_point = "F";
                }
                enqueue(&head_BC, *train); // Train from B to C
          
            }

            pthread_cond_signal(&cond_tunnel);
            pthread_mutex_unlock(&mutex_queues);
        
            free(train); 
            sleep(1);

        }

        if (shutdown_flag) {
            break;
        }
    }
    return NULL;  
}

void *tunnel_controller(void *param) {
    while (!shutdown_flag) {
        pthread_mutex_lock(&mutex_queues);

        // Wait until the tunnel is free and there is at least one train in any queue
        while (!shutdown_flag && tunnel_occupied || 
               (get_queue_size(head_AC) == 0 && get_queue_size(head_BC) == 0 &&
                get_queue_size(head_ED) == 0 && get_queue_size(head_FD) == 0)) {
            pthread_cond_wait(&cond_tunnel, &mutex_queues);
        }
        
         if (shutdown_flag) {
            pthread_mutex_unlock(&mutex_queues);
            break;
        }

        // Logic to select the train for the tunnel passage based on priority and count
        int max_trains = 0;
        char queue_id = '0';

         // Check each queue and select the train based on priority rules
        if (get_queue_size(head_AC) > max_trains) {
            max_trains = get_queue_size(head_AC);
            queue_id = 'A';
        }
        if (get_queue_size(head_BC) > max_trains) {
            max_trains = get_queue_size(head_BC);
            queue_id = 'B';
        }
        if (get_queue_size(head_ED) > max_trains) {
            max_trains = get_queue_size(head_ED);
            queue_id = 'E';
        }
        if (get_queue_size(head_FD) > max_trains) {
            max_trains = get_queue_size(head_FD);
            queue_id = 'F';
        }

        // Prioritize trains waiting at A > B > E > F in case of a tie
        if (max_trains == get_queue_size(head_AC)) queue_id = 'A';
        else if (max_trains == get_queue_size(head_BC) && queue_id != 'A') queue_id = 'B';
        else if (max_trains == get_queue_size(head_ED) && queue_id != 'A' && queue_id != 'B') queue_id = 'E';
        else if (max_trains == get_queue_size(head_FD) && queue_id != 'A' && queue_id != 'B' && queue_id != 'E') queue_id = 'F';


        // Remove the train from the selected queue and process it
        TrainNode* train_node_to_process = NULL;
        switch (queue_id) {
            case 'A':
                train_node_to_process = dequeue(&head_AC);
                break;
            case 'B':
                train_node_to_process = dequeue(&head_BC);
                break;
            case 'E':
                train_node_to_process = dequeue(&head_ED);
                break;
            case 'F':
                train_node_to_process = dequeue(&head_FD);
                break;
        }

        tunnel_occupied = 1;
        pthread_mutex_unlock(&mutex_queues);

        if (train_node_to_process) {
        process_train(queue_id, train_node_to_process->train.length, train_node_to_process->train); 
        free(train_node_to_process); 
    }

        // Free up the tunnel
        pthread_mutex_lock(&mutex_queues);
        tunnel_occupied = 0;
        pthread_cond_broadcast(&cond_tunnel); 
        pthread_mutex_unlock(&mutex_queues);
    }

    return NULL;
}

void *logging_controller(void *param) {
    // Open log files for writing
    train_log_file = fopen("train.log", "w");
    control_center_log_file = fopen("control-center.log", "w");

    if (!train_log_file || !control_center_log_file) {
        perror("Error opening log files");
        exit(EXIT_FAILURE);
    }

    // Title and Column headers to train.log
    fprintf(train_log_file, "train.log:\n");
    fprintf(train_log_file, "Simulation arguments: p = %f, simulation_time = %d\n", p, simulation_time);
    fprintf(train_log_file, "%-10s %-15s %-15s %-10s %-20s %-20s\n",
            "Train ID", "Starting Point", "Destination", "Length(m)", "Arrival Time", "Departure Time");

    // Title and Column headers to control-center.log
    fprintf(control_center_log_file, "control-center.log:\n");
    fprintf(control_center_log_file, "%-20s %-20s %-10s %-s\n",
            "Event", "Event Time", "Train ID", "Trains Waiting Passage");

    while (!shutdown_flag) {
        sleep(1);
    }

    // Close log files when the simulation ends
    close_log_files();

    return NULL;
}

int main(int argc, char *argv[]) {

    // Parse command-line arguments for simulation parameters
    if (argc > 1) {
        p = strtod(argv[1], NULL);
        if (p < 0.0 || p > 1.0) {
            fprintf(stderr, "Invalid probability value. Must be between 0 and 1.\n");
            return 1;
        }
    }

    if (argc > 2) {
        simulation_time = atoi(argv[2]);
    }

    if (argc > 3) {
        fprintf(stderr, "Usage: %s [probability] [simulation_time]\n", argv[0]);
        return 1;
    }

    time_t start_time = time(NULL); 
    srand(time(NULL));

    signal(SIGINT, signal_handler); 

    pthread_t train_gen_thread, tunnel_ctrl_thread;
    pthread_t logging_thread;
    
    // Create threads for train generator and tunnel controller
    if (pthread_create(&train_gen_thread, NULL, train_generator, &p)) {
        perror("Failed to create train generator thread");
        return 1;
    }
    if (pthread_create(&tunnel_ctrl_thread, NULL, tunnel_controller, NULL)) {
        perror("Failed to create tunnel controller thread");
        return 1;
    }

    // Create a thread for logging
    if (pthread_create(&logging_thread, NULL, logging_controller, NULL)) {
        perror("Failed to create logging thread");
        return 1;
    }

    while (!shutdown_flag) {
       if (difftime(time(NULL), start_time) >= simulation_time) {
            shutdown_flag = 1; 
        }
        sleep(1);
    }

    // Wait for the threads to finish
    pthread_join(train_gen_thread, NULL);
    pthread_join(tunnel_ctrl_thread, NULL);
    pthread_join(logging_thread, NULL);

    printf("Simulation ended.\n");
    return 0;
}

