#include <stdio.h>

// ---Global Shared Resources---

//Inputs
int num_students;
int num_tutors;
int num_chairs;
int num_help;

//FIFO Queue for Waiting Students
int* waiting_queue;
int waiting_count = 0;
int queue_front = 0;
int queue_rear = 0;


int main(int argc, char* argv[]) {
    // Validating and parsing inputs
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <num_students> <num_tutors> <num_chairs> <num_help>\n", argv[0]);
        return 1;
    }

    num_students = atoi(argv[1]);
    num_tutors = atoi(argv[2]);
    num_chairs = atoi(argv[3]);
    num_help = atoi(argv[4]);

    if (num_students <= 0 || num_chairs <= 0 || num_help <= 0) {
        fprintf(stderr, "Error: Number of students, chairs and helps must be positive.\n");
        return 1;
    }

    // Allocating dynamic memory
    waiting_queue = malloc(num_chairs * sizeof(int));


}
