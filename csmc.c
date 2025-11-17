#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h> // For assertions
#include <time.h>   // For srand seeding
#include <unistd.h> // For usleep

// ---Global Shared Resources---

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
volatile int shutdown_flag = 0;

//Semaphores
sem_t sem_student_arrived;              // Student signals Coord they are in arrival_fifo
sem_t sem_student_waiting_in_pqueue;    // Coord signals Tutor a student is in p_queue
sem_t sem_tutors_available;             // Semaphore for tutor "slots"

// ---Simulation Times---
#define PROGRAM_SLEEP_US 2000 // 0-2ms
#define TUTOR_SLEEP_US 200   // 0.2ms

// ---Data Structure and Other Helpers---
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

//Globally Shared Queues
Queue arrival_fifo; // Students add themselves here (max size num_chairs)
Queue* p_queues;    // Priority queues (array of Queues, size max_help)

//Queue Helpers:
void queue_init(Queue* q) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
}

void enqueue(Queue* q, int student_id) {
    Node* newNode = malloc(sizeof(Node));
    newNode->student_id = student_id;
    newNode->next = NULL;
    if (q->tail == NULL) {
        q->head = newNode;
    } else {
        q->tail->next = newNode;
    }
    q->tail = newNode;
    q->count++;
}

int dequeue(Queue* q) {
    if (q->head == NULL) {
        return -1; // Should not happen if count is managed
    }
    Node* temp = q->head;
    int student_id = temp->student_id;
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    free(temp);
    q->count--;
    return student_id;
}

void p_queue_init(int max_priority) {
    p_queues = malloc(max_priority * sizeof(Queue));
    for (int i = 0; i < max_priority; i++) {
        queue_init(&p_queues[i]);
    }
}

void p_queue_enqueue(int student_id, int priority) {
    // priority is 0-based visit_count
    assert(priority < max_help);
    enqueue(&p_queues[priority], student_id);
    p_queue_total_count++;
}

int p_queue_dequeue() {
    // Find highest-priority (lowest index) student
    for (int i = 0; i < max_help; i++) {
        if (p_queues[i].count > 0) {
            p_queue_total_count--;
            return dequeue(&p_queues[i]);
        }
    }
    return -1; // Queue was empty
}

//Sleep Helper
void sleep_random(long max_us) {
    if (max_us <= 0) return;
    long random_us = (long)rand() % (max_us + 1);
    usleep(random_us);
}

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

//Thread Data (Global)
StudentInfo* student_data;
TutorInfo* tutor_data;

// --TUTOR THREAD--
void* tutor_thread(void* arg) {
    TutorInfo* me = (TutorInfo*)arg;
    int my_id = me->id;

    while(1){
        //wait for student in p-queue
        sem_wait(&sem_student_waiting_in_pqueue);
        pthread_mutex_lock(&mutex);
        //check shutdown flag
        if(shutdown_flag){
            if (p_queue_total_count == 0 && students_finished_count == num_students) {
                // Propagate shutdown signal to other tutors
                sem_post(&sem_student_waiting_in_pqueue);
                pthread_mutex_unlock(&mutex);
                break;
            }
            // there are still students, no one else is coming, wake up another tutor.
            sem_post(&sem_student_waiting_in_pqueue);
        }
        // i was woken but there are no students
        if (p_queue_total_count == 0) {
            pthread_mutex_unlock(&mutex);
            continue; // Go back to waiting
        }

        //Get tutor slot
        pthread_mutex_unlock(&mutex); // Unlock to wait on semaphore
        sem_wait(&sem_tutors_available);
        pthread_mutex_lock(&mutex); // Re-lock

        //Re-check p-queue (tutor might have grabbed them between my sem_wait and lock) and get highest priority student
        if (p_queue_total_count == 0) {
            sem_post(&sem_tutors_available); // Release the slot
            pthread_mutex_unlock(&mutex);
            continue; // Go back to waiting
        }
        //found student
        int student_id = p_queue_dequeue();
        assert(student_id != -1);
        StudentInfo* student_to_help = &student_data[student_id];
        //state management
        student_to_help->current_tutor_id = my_id;
        current_tutoring_sessions++;
        total_sessions_tutored++;

        //sanity checks
        assert(total_sessions_tutored <= total_help_requests);
        assert(current_tutoring_sessions <= num_tutors);

        //message
        printf("T: Student %d tutored by Tutor %d. Total sessions being tutored = %d. Total sessions tutored by all = %d.\n",
                student_id, my_id, current_tutoring_sessions, total_sessions_tutored);
        
        //student has been claimed, unlock:
        pthread_mutex_unlock(&mutex);

        //signal *specific* student to start
        sem_post(&student_to_help->sem_start_tutoring);
        
        //Simulate tutoring
        usleep(TUTOR_SLEEP_US);

        //Wait for student to signal they are done
        sem_wait(&student_to_help->sem_finish_tutoring);

        //Update state after tutoring
        pthread_mutex_lock(&mutex);
        current_tutoring_sessions--;
        pthread_mutex_unlock(&mutex);

        // 12. Release the "tutor slot"
        sem_post(&sem_tutors_available);
    
    }
    return NULL;
}

// --COORD THREAD--
void* coord_thread(void* arg) {
    (void)arg; // Unused

    while(1){
        //wait for student
        sem_wait(&sem_student_arrived);
        pthread_mutex_lock(&mutex);

        //Check for shutdown signal
        if (shutdown_flag) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        //Grab student from arrival_fifo
        assert(arrival_fifo.count > 0);
        int student_id = dequeue(&arrival_fifo);
        int priority = student_data[student_id].visit_count; // 0-based

        //Add student to the correct priority queue
        p_queue_enqueue(student_id, priority);

        total_help_requests++;
        assert(p_queue_total_count <= num_chairs); //just making sure 

        //Print coordinator message (with 1-based priority)
        printf("C: Student %d with priority %d in queue. Waiting students = %d. Total help requested so far = %d.\n",
               student_id, priority + 1, p_queue_total_count, total_help_requests);
        
        pthread_mutex_unlock(&mutex);

        //Signal to idle tutors that a student is ready
        sem_post(&sem_student_waiting_in_pqueue);
    }
    
    //Propagate shutdown signal to any waiting tutors
    sem_post(&sem_student_waiting_in_pqueue);
    return NULL;
}

// --STUDENT THREAD--
void* student_thread(void* arg) {
    StudentInfo* me = (StudentInfo*)arg;
    int my_id = me->id;

    while(me->visit_count < max_help){
        // do some programing
        sleep_random(PROGRAM_SLEEP_US);

        //try to get a chair
        int chair_taken = 0;
        pthread_mutex_lock(&mutex);
        if (num_empty_chairs > 0) {
            num_empty_chairs--;
            printf("S: Student %d takes a seat. Empty chairs remaining = %d.\n",
                   my_id, num_empty_chairs);
            // Add myself to simple arrival FIFO
            enqueue(&arrival_fifo, my_id);
            chair_taken = 1;
        } else {
            printf("S: Student %d found no empty chair. Will come again later.\n", my_id);
        }
        pthread_mutex_unlock(&mutex);
        
        // once a chair is exclusively claimed...
        if (chair_taken) {
            //Signal Coordinator
            sem_post(&sem_student_arrived);

            //Wait for a tutor to signal me
            sem_wait(&me->sem_start_tutoring);

            // Woken up by tutor!
            
            // 5. Free the chair exclusively
            pthread_mutex_lock(&mutex);
            num_empty_chairs++;
            // The tutor incremented current_tutoring_sessions
            assert(current_tutoring_sessions <= num_tutors);
            pthread_mutex_unlock(&mutex);

            // 6. Receive help
            printf("S: Student %d receives help from Tutor %d.\n", 
                   my_id, me->current_tutor_id);
            
            // Simulate tutoring
            usleep(TUTOR_SLEEP_US);

            // 7. Signal tutor I am done
            sem_post(&me->sem_finish_tutoring);

            // 8. Update my visit count
            me->visit_count++;
        
        } // else, loop again to "program" and "try later"

    }

    // Reached max_help, no one can help you now...
    pthread_mutex_lock(&mutex);
    students_finished_count++;
    pthread_mutex_unlock(&mutex);
    return NULL;
    
}

// --MAIN--
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
    pthread_t coordinator_thread;

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
    if (pthread_create(&coordinator_thread, NULL, coord_thread, NULL) != 0) {
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

    //Signal shutdown
    pthread_mutex_lock(&mutex);
    shutdown_flag = 1;
    pthread_mutex_unlock(&mutex);

    // Wake up coordinator and tutors so they can exit
    sem_post(&sem_student_arrived); // Wake coordinator
    for (int i = 0; i < num_tutors; i++) {
         // Wake all tutors
        sem_post(&sem_student_waiting_in_pqueue);
    }

    // Join coordinator and tutors
    pthread_join(coordinator_thread, NULL);
    for (int i = 0; i < num_tutors; i++) {
        pthread_join(tutor_threads[i], NULL);
    }

    //Cleanup
    pthread_mutex_destroy(&mutex);
    sem_destroy(&sem_student_arrived);
    sem_destroy(&sem_student_waiting_in_pqueue);
    sem_destroy(&sem_tutors_available);

    for (int i = 0; i < num_students; i++) {
        sem_destroy(&student_data[i].sem_start_tutoring);
        sem_destroy(&student_data[i].sem_finish_tutoring);
    }
    
    // Free P-Queues (linked list nodes were freed in dequeue)
    for (int i = 0; i < max_help; i++) {
        // Just in case any are left (shouldn't be)
        while(p_queues[i].count > 0) dequeue(&p_queues[i]);
    }
    free(p_queues);

    // Free main data arrays
    free(student_data);
    free(tutor_data);
    free(student_threads);
    free(tutor_threads);

    return 0;
}

