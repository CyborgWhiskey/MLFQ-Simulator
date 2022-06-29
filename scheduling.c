#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_LINE 100

int S; //Stores time before priority reset
pthread_t *cores; //Array of CPU core threads
int finishedTasks = 0; //Keeps track of number of finished tasks
int endProg = 0;

//QUEUES
struct Queue* high;
struct Queue* mid;
struct Queue* low;
struct Queue* done;

//LOCKS
//MLFQ levels
pthread_mutex_t highLock;
pthread_mutex_t midLock;
pthread_mutex_t lowLock;

//Used to lock the finished queue
pthread_mutex_t doneQ;

//CONDITION VARIABLES
pthread_cond_t isWork = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cpuLock;

struct timespec diff(struct timespec start, struct timespec end);

typedef struct task_t{
	//Task Data: Hold the data for each task and keep track of haow much time is left
	char* name;
	int id;
	int length;
	int oddsIO;
	
	//What priority level in the queue and how much until priority is lowered
	int level;
	int timeSpent;
	
	//Time Variables: Used in summary at end of program
	struct timespec startTime;
	struct timespec endTime;
	struct timespec response;

	//next pointer
	struct task_t* next;
}task_t;

//------------------------------------------------------------
//QUEUE IMPLEMENTATION
//------------------------------------------------------------
typedef struct Queue {
	task_t* head;
	task_t* tail;
}Queue;

//Creates a brand new queue and allocates heap memory for it
//Sets the head and tail pointers to null
//Returns a pointer to the new queue
Queue* createQueue(){
	Queue* q;
	q = malloc(sizeof(Queue));

	q->head = q->tail = NULL;
	return q;
}

//Takes in a queue and a task
//Takes the task and puts it at the end of the queue
void enterQueue(Queue* q, task_t* task){
	task->next = NULL;

	//If it is the first item in queue, set head and tail to it	
	if(q->tail == NULL){
		q->head = q->tail = task;
	}
	//Otherwise it is added where tail-> next points to
	else{
		q->tail->next = task;
	}

	q->tail = task;
}

//Returns 1 if the passed queue is empty, or 0 if not
int queueEmpty(Queue* q){
	return (q->head == NULL);
}

//Takes the top value of the queue and removes it and returns it
task_t* exitQueue(Queue* q){
	task_t* task = NULL;

	//Checks if queue is empty
	//If not item at front is removed
	//Otherwise nothing happens
	if(q->head != NULL){
		task = q->head;
		q->head = q->head->next;
	}

	if(q->head == NULL){q->tail = NULL;}
	return task;
}

//sleeps a thread by a given number of microseconds
void msleep(unsigned int usecs){
	
	long seconds = usecs/1000000;
	long nanos = (usecs%1000000)*1000;

	struct timespec t = {.tv_sec = seconds, .tv_nsec = nanos};
	int ret;

	do{
		ret = nanosleep(&t, &t);
	} while(ret == -1 && (t.tv_sec || t.tv_nsec));
}

//------------------
//SCHEDULER METHODS
//------------------
//Takes in a line of task input, and makes a new task object out of it
void getTask(char* line, task_t* task){	
	char *token = strchr(line, '\n');
	if(token != NULL){
		*token = ' ';
	}
		
	//Grabs task data and puts it into struct
	//Allocates space for name array and copies name
	token = strtok(line, " ");
	task->name = malloc(sizeof(token));
	strcpy(task->name, token);

	//Gets rest of standard parameters(no malloc required)
	task->id = atoi(strtok(NULL, " "));
	task->length = atoi(strtok(NULL, " "));
	task->oddsIO = atoi(strtok(NULL, " "));

	//Auto sets priority to highest level and time spent on task to 0
	task->level = 0;
	task->timeSpent = 0;
}

//Recieves a task and puts it into a struct, then adds it to the end of the queue
void enterMLFQ(task_t* task){
	//Checks task priority
	//High Priority
	if(task->level == 0){
		pthread_mutex_lock(&highLock);
		enterQueue(high, task);
		pthread_mutex_unlock(&highLock);
	}
	
	//Mid priority
	else if(task->level == 1){
		pthread_mutex_lock(&midLock);
		enterQueue(mid, task);
		pthread_mutex_unlock(&midLock);
	}

	//Low priority
	else if(task->level == 2){
		pthread_mutex_lock(&lowLock);
		enterQueue(low, task);
		pthread_mutex_unlock(&lowLock);
	}
}

//Gets the head of the queue and returns it
task_t* exitMLFQ(){
	task_t* task = NULL;

	//Checks if each queue is empty bevore checking the next, goes in order of priority
	//High priority	
	if(queueEmpty(high) == 0){
		pthread_mutex_lock(&highLock);
		task = exitQueue(high);
		pthread_mutex_unlock(&highLock);
	}

	//Mid priority
	else if(queueEmpty(mid) == 0){
		pthread_mutex_lock(&midLock);
		task = exitQueue(mid);
		pthread_mutex_unlock(&midLock);
	}

	//Low priority
	else{
		pthread_mutex_lock(&lowLock);
		task = exitQueue(low);
		pthread_mutex_unlock(&lowLock);
	}

	return task;
}

//Resets all tasks in mid and low queues to high queue
void resetTasks(){
	task_t* task;

	//Locks low and mid queues
	pthread_mutex_lock(&midLock);
	pthread_mutex_lock(&lowLock);

	//Resets priorities for all tasks in mid to 0, and resets the time it spent on the mid queue
	while(queueEmpty(mid) == 0){
		//Gets task and resets parameters
		task = exitQueue(mid);
		task->level = 0;
		task->timeSpent = 0;

		//Gets lock for high queue and adds task
		pthread_mutex_lock(&highLock);
		enterQueue(high, task);
		pthread_mutex_unlock(&highLock);
	}

	//Resets priorities for all tasks in low to 0, and resets the time it spent on the low queue
	while(queueEmpty(low) == 0) {
		//Gets task and resets parameters
		task = exitQueue(low);
		task->level = 0;
		task->timeSpent = 0;

		//Gets lock for high queue and adds task
		pthread_mutex_lock(&highLock);
		enterQueue(high, task);
		pthread_mutex_unlock(&highLock);
	}
	
	//Unlocks mid and low queues
	pthread_mutex_unlock(&midLock);
        pthread_mutex_unlock(&lowLock);
}

//--------------------
//FINISHED TASK QUEUE
//-------------------
void enterDone(task_t* task){
	pthread_mutex_lock(&doneQ);
		enterQueue(done, task);
		finishedTasks++;
	pthread_mutex_unlock(&doneQ);
}

//-----------------
//CPU METHODS
//-----------------
void* cpuWork(){
	task_t* task = NULL;
	int random;

	while(endProg == 0){
		//Waits for task to be avaliable
		//When signalled it gets the next task and attempts to run it
		pthread_cond_wait(&isWork, &cpuLock);
		task = exitMLFQ();
		random = rand()%100;

		//If task returned is not null, run the task
		if(task != NULL){
			//If this is the first time the task has been run, gets time of first run
			if(task->response.tv_nsec == 0){clock_gettime(CLOCK_REALTIME, &task->response);}
			
			//If the chance of IO is less than the random number, IO call is NOT made
			if(random > task->oddsIO){					
				//If the task has less then the time quantum left to complete, task is run until end and put into done queue
				if(task->length <= 50){
					msleep(task->length);
					task->length = 0;
					
					//Gets tge finish time and puts task in done queue
					clock_gettime(CLOCK_REALTIME, &task->endTime);
					enterDone(task);
				}
				else{
					msleep(50);
					task->length -= 50;
					task->timeSpent += 50;

					//If the time for the task goes beyond 200ms its priority is reduced
					if(task->level < 2 && task->timeSpent > 200){
						task->level++;
						task->timeSpent = 0;
					}
		
					enterMLFQ(task);
				}
			}
			//If chane of IO is greater than or equal to random, IO will be done
			else{
				//Gets new random to be the time until the IO call is made
				if(task->length <= 50) {random = rand()%task->length + 1;}
				else{random = rand()%50 + 1;}

				//Sleeps until IO cale is "made"
				msleep(random);
				
				//Updates task data
				task->length -= random;
				task->timeSpent += random;
				
				//If the task has exceeded its time quantum, its level gets reduced
				if(task->level < 2 && task->timeSpent > 200){
                                	task->level++;
                                        task->timeSpent = 0;
                               	}

				//If the IO call is made at the very end of the task, then the task is put in the done queue
				if(task->length != 0){enterMLFQ(task);}
				else{
					clock_gettime(CLOCK_REALTIME, &task->endTime);
					enterDone(task);
				}
			}
		}

		//Else there was an error and the task is not run
		
	}
        pthread_exit(NULL);
}

//-------------------
//MAIN/READER METHODS
//-------------------
//Checks it the line is an actual task or a delay command
//Returns 0 if it is a regular task or 1 if it is a delay
int isTask(char* line){
	char delay[] = {line[0], line[1], line[2], line[3], line[4], '\0'};
	int task = 0;
	if(strcmp(delay, "DELAY") == 0){task = 1;}
	return task;
}

//After each time period S, this thread resets the MLFQ by putting every task in high priority
void* timeReset(){
	while(endProg == 0){
		msleep(S);
		resetTasks();
	}
	pthread_exit(NULL);
}

//FInds the difference between two timespec structs
//Code is sourced from Guy Rutenberg
struct timespec diff(struct timespec start, struct timespec end){
	struct timespec temp;

	if((end.tv_nsec - start.tv_nsec) < 0) {
		temp.tv_sec = (end.tv_sec-start.tv_sec-1);
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	}
	else{
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}

int main(int args, char* argv[]){
	//File to read tasks from and a char to store each in for processing
	//Task pointer is also declared to iterate through each line of file
	FILE* tasks;
	char line[MAX_LINE];
	task_t* task;

	//Timer thread used to know when to priority boost
	pthread_t timer = -1;
	int i; //Used in loops

	//From input, number of CPUs to simulate, and number of total tasks read in from the file
	int numCPU; 
	int tasksAdded = 0;
	
	//Execution summary variables
	//Array to store average turnaround and response times for each tasky type
	//Timespec to store calculated time differences to get elapsed time from system time
	long turnaround[4] = {0, 0, 0, 0};
	long response[4] = {0, 0, 0, 0};
	int numEach[4]= {0, 0, 0, 0};;	
	struct timespec elapsed;
	struct timespec firstRun;

	//The correct number of arguments should be 3
	if(args == 4){
		//Gets the # on CPUs and S value from command line and converts them to integer values
		numCPU = atoi(argv[1]);
		S = atoi(argv[2]);

		printf("Using MLFQ with %d CPU's and %d ms before priority reset\n\n", numCPU, S);

		S*=1000; //Converts S from miliseconds to microseconds

		//initializes queues
		high = createQueue();
		mid = createQueue();
		low = createQueue();	
		done = createQueue();
	
		//Allocates memory for pthread ids, and starts all CPUs
		cores = malloc(sizeof(pthread_t)*numCPU);
		for(i = 0; i < numCPU; i++){
			pthread_create(&cores[i], NULL, cpuWork, NULL);
		}

		//Attenmpts to open file, if successful program begins processing 
		tasks = fopen(argv[3], "r");
		if(tasks != NULL){

			//Begins reading in lines from file until EOF is reached
			while(fgets(line, MAX_LINE, tasks) != NULL){
				//Checks if the current line is a task or a delay command
				//If task, add task to queue
				if(isTask(line) == 0){
					//Declares memory for new task and gets its perameters
					task = malloc(sizeof(task_t));
					getTask(line, task);		
					
					//Starts task timer and enters it into the MLFQ
					clock_gettime(CLOCK_REALTIME, &task->startTime);
					enterMLFQ(task);
					
					//Increases count of tasks added to system and notifies CPUs that there is work
					//Only notifies CPU after the first DELAY is read
					tasksAdded++;
					if(timer != -1){pthread_cond_signal(&isWork);}
				}

				//If delay, sleeps reading thread for amount of microseconds given
				else{
					//If this is the first delay call, start the processing	
					if(timer == -1){
						pthread_create(&timer, NULL, timeReset, NULL);
						pthread_cond_signal(&isWork);
					}
						
					//Sleeps for the amount of time needed
					msleep(atoi(&line[6]));
				}
			}

			//Closes file ad halts excecution
			fclose(tasks);
		}		
		//If file cannot be opened, error is printed  and program exits
		else{printf("File %s could not be found\n", argv[3]);}

		//Dispatcher
		//Repeadiately signals CPUs there is work until all tasks are in the done queue
		while(finishedTasks < tasksAdded) {
                        pthread_cond_signal(&isWork);
                }	

		//Gets all tasks from the finished queue and adds up all of their running times by tipe
		//Total turnaround time is stored in the turnaround array, with each index corrisponding to the type of task
		//Total response time is stored in the response array, with each index corrisponding to the type of task
		//Amount of each task type is also counted and stored in array numEach
		while(queueEmpty(done) == 0){
			//Gets next task in done queue
			pthread_mutex_lock(&doneQ);
			task = exitQueue(done);
			pthread_mutex_unlock(&doneQ);
	
			//If task is not null
			//Get time difference between start, end, and response times
			//Add them to their respective sum arrays
			if(task != NULL){
				//Get times for current task
				elapsed = diff(task->startTime, task->endTime);
				firstRun = diff(task->startTime, task->response);

				//Store task data in arrays
				numEach[task->id]++;
				turnaround[task->id] += elapsed.tv_nsec;
				response[task->id] += firstRun.tv_nsec;
			}
			else{printf("ERROR: Task was null\n");}

			//Frees the task memory
			free(task->name);
			free(task);
		}

		//Prints out average turnaround time for each id	
		printf("Average turnaround time per type:\n");
		for(i = 0; i < 4; i++) {
			turnaround[i] /= numEach[i];
			printf("Type %d: %lu usec\n", i, turnaround[i]/1000);
		}

		//Prints out average response time for each id
		printf("\nAverage response time per type:\n");
		for(i = 0; i < 4; i++) {
			response[i] /= numEach[i];
			printf("Type %d: %lu usec\n", i, response[i]/1000);
		}

		//Frees memory allocated in array and in queues
		free(cores);
		free(high);
		free(mid);
		free(low);
		free(done);
	}
	//If not enough arguments are given, program prints error message and exits
	else{printf("Not enough arguments given, expected 3 got %d\n", args-1);}

	printf("\nHalting Execution\n");
	endProg = 1;
	return 0;
}
