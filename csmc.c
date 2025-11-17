#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

// ---Data Structure Helpers---
//Helper: Linked List Node
typedef struct Node {
    int student_id;
    struct Node* next;
} Node;

//Helper: Simple FIFO Queue (for arrival-queue & p-queue)
typedef struct Queue {
    Node* head;
    Node* tail;
    int count;
} Queue;

// ---Global Shared Resources---

//Queues
Queue arrival_fifo; // Students add themselves here (max size num_chairs)
Queue* p_queues;    // Priority queues (array of Queues, size max_help)

//Inputs
int num_students;
int num_tutors;
int num_chairs;
int max_help;

//FIFO Queue for Waiting Students
int* waiting_queue;
int waiting_count = 0;
int queue_front = 0;
int queue_rear = 0;

//Shared data
pthread_mutex_t mutex;
int num_empty_chairs;
int total_help_requests;        // requests to C
int total_sessions_tutored;     // sessions by T 
int current_tutoring_sessions;  // current sessions by T
int students_finished_count;    
int p_queue_total_count;

//Semaphores
sem_t sem_student_arrived;              // Student signals Coord they are in arrival_fifo
sem_t sem_student_waiting_in_pqueue;    // Coord signals Tutor a student is in p_queue
sem_t sem_tutors_available;             // Semaphore for tutor "slots"

//Thread Data (Global)
StudentInfo* student_data;
TutorInfo* tutor_data;

// ---Student-Specific Data---
typedef struct StudentInfo {
    int id;
    int visit_count; // 0-based (0 = 1st visit, 1 = 2nd, etc.)
    int current_tutor_id;
    sem_t sem_start_tutoring; // Tutor signals this
    sem_t sem_finish_tutoring; // Student signals this
} StudentInfo;

// ---Tutor-Specific Data---
typedef struct TutorInfo {
    int id;
} TutorInfo;

int main(int argc, char* argv[]) {
    // Validating and parsing inputs
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <num_students> <num_tutors> <num_chairs> <num_help>\n", argv[0]);
        return 1;
    }

    num_students = atoi(argv[1]);
    num_tutors = atoi(argv[2]);
    num_chairs = atoi(argv[3]);
    max_help = atoi(argv[4]);

    if (num_students <= 0 || num_tutors <= 0 || num_chairs <= 0 || max_help <= 0) {
        fprintf(stderr, "Error: Args must be +ve integers.\n");
        return 1;
    }

    // Seeding random number generator
    srand((unsigned int)time(NULL)^(unsigned int)getpid());

    // Init global vars
    num_empty_chairs = num_chairs;
    total_help_requests = 0;
    total_sessions_tutored = 0;
    current_tutoring_sessions = 0;
    students_finished_count = 0;
    p_queue_total_count = 0;

    //Dynamic allocation
    student_data = malloc(num_students * sizeof(StudentInfo));
    tutor_data = malloc(num_tutors * sizeof(TutorInfo));
    pthread_t* student_threads = malloc(num_students * sizeof(pthread_t));
    pthread_t* tutor_threads = malloc(num_tutors * sizeof(pthread_t));
    pthread_t coord_thread;

    // (Arrival FIFO queue is bounded by num_chairs)
    arrival_fifo.head = arrival_fifo.tail = NULL;
    arrival_fifo.count = 0;

    // (Priority queue has max_help levels)
    p_queue_init(max_help);

    //Init sync primitives
    pthread_mutex_init(&mutex, NULL);
    sem_init(&sem_student_arrived, 0, 0);
    sem_init(&sem_student_waiting_in_pqueue, 0, 0);
    sem_init(&sem_tutors_available, 0, num_tutors);

    //Tutor threads
    for (int i = 0; i < num_tutors; i++) {
        tutor_data[i].id = i;
        if (pthread_create(&tutor_threads[i], NULL, tutor_thread, &tutor_data[i]) != 0) {
            printf("Failed to create tutor thread");
            return 1;
        }
    }

    // Coordinator thread
    if (pthread_create(&coord_thread, NULL, coord_thread, NULL) != 0) {
        printf("Failed to create coordinator thread");
        return 1;
    }

    // Student threads
    for (int i = 0; i < num_students; i++) {
        student_data[i].id = i;
        student_data[i].visit_count = 0;
        sem_init(&student_data[i].sem_start_tutoring, 0, 0);
        sem_init(&student_data[i].sem_finish_tutoring, 0, 0);
        if (pthread_create(&student_threads[i], NULL, student_thread, &student_data[i]) != 0) {
            printf("Failed to create student thread");
            return 1;
        }
    }

    //Join (wait for) all student threads
    for (int i = 0; i < num_students; i++) {
        pthread_join(student_threads[i], NULL);
    }

    


}

void* tutor_thread(void* arg) {}
void* coord_thread(void* arg) {}
void* student_thread(void* arg) {}
