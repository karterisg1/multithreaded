#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define SIZE 1024

typedef struct 
{
    char jobID[16];
    char job_command[256];
    int client_socket;
} task;

pthread_mutex_t Qmutex = 	PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t 	QnotEmpty = PTHREAD_COND_INITIALIZER;
pthread_cond_t 	QnotFull = 	PTHREAD_COND_INITIALIZER;

task *Q;					/* Buffer with pending jobs waiting to be eexcuted */
int Qsize;					/* Queue maximum number of jobs */
int front = 0;				/* index to refer to older job */
int rear = 0;				/* index to refer to newer job */
int Qcount = 0;				/* Current number of pending jobs */
int concurrencyLevel = 1;	/* Degree of parallelly executed jobs (default is 1) */

/* Prints error message and terminates */
void error(const char *msg) 
{
    perror(msg);
    exit(1);
}

/* function to set concurrency level */
void setConcurrency(int client_socket, char *command)
{
	/* changing concurrencyLevel */
	concurrencyLevel = atoi(command + 15);

	/* inform client */
	char packet[256];
	snprintf(packet, sizeof(packet), "CONCURRENCY SET AT %d", concurrencyLevel);
	send(client_socket, packet, strlen(packet), 0);
}

/* function to stop a specific job */
void stop(int client_socket, char *command)
{
	char *jobID = command + 5;

	pthread_mutex_lock(&Qmutex);

	/* search job */
	int found = 0;
	for (int i = 0; i < Qcount; i++) 
	{
		int index = (front + i) % Qsize;
		if (strcmp(Q[index].jobID, jobID) == 0) 
		{
			found = 1;
			Q[index] = Q[(front + Qcount - 1) % Qsize];
			rear = (rear + Qsize - 1) % Qsize;
			Qcount--;
			break;
		}
	}
	/* inform client */
	char packet[256];
	
	if (found)
	{
		snprintf(packet, sizeof(packet), "<%s> REMOVED", jobID);
	} 
	else 
	{
		snprintf(packet, sizeof(packet), "<%s> NOT FOUND", jobID);
	}
	send(client_socket, packet, strlen(packet), 0);

	pthread_cond_signal(&QnotFull);
	pthread_mutex_unlock(&Qmutex);
}

/* function to print pending jobs */
void poll(int client_socket)
{
	pthread_mutex_lock(&Qmutex);
		
	/* inform client */
	char packet[SIZE] = "POLLED JOBS:\n";
	for (int i = 0; i < Qcount; i++) 
	{
		int index = (front + i) % Qsize;
		char job_info[SIZE];
		
		snprintf(job_info, sizeof(job_info), "<%s, %s>\n", Q[index].jobID,Q[index].job_command);
		strcat(packet, job_info);
	}
	send(client_socket, packet, strlen(packet), 0);
	pthread_mutex_unlock(&Qmutex);
}

/* function to terminate life of server */
void terminate(int client_socket)
{
	// Terminate the server and inform client
	char packet[256] = "SERVER TERMINATED BEFORE EXECUTION";
	for (int i = 0; i < Qcount; i++) 
	{
		send(Q[i].client_socket, packet, strlen(packet), 0);
		close(Q[i].client_socket);
	}
	Qcount = 0;
	strcpy(packet, "SERVER TERMINATED");
	send(client_socket, packet, strlen(packet), 0);
	pthread_mutex_unlock(&Qmutex);
}

/* function to issueJob command */
void issueJob(int client_socket, char *command)
{
	pthread_mutex_lock(&Qmutex);
	while (Qcount == Qsize) 
	{
		pthread_cond_wait(&QnotFull, &Qmutex);
	}
	static int job_count = 0;
	char jobID[16];
	
	/* assign unique id */
	snprintf(jobID, sizeof(jobID), "job_%d", job_count++);
	
	/* Add job to queue */
	strcpy(Q[rear].jobID, jobID);
	snprintf(Q[rear].job_command, sizeof(Q[rear].job_command), "%s", command + 9);
	Q[rear].client_socket = client_socket;
	rear = (rear + 1) % Qsize;
	Qcount++;

	/* inform client */
	char packet[2*SIZE];
	
	snprintf(packet, sizeof(packet), "<%s, %s> SUBMITTED", jobID, command + 9);
	send(client_socket, packet, strlen(packet), 0);

	pthread_cond_signal(&QnotEmpty);
	pthread_mutex_unlock(&Qmutex);
}
/* Handle cotroller thread */
void* controller_thread_function(void* arg) 
{
    int client_socket = *(int*)arg;
    free(arg);
    
    char command[SIZE] = {'\0'};		/* storing the command */
	
    if (recv(client_socket, command, sizeof(command), 0) <= 0) 
	{
        close(client_socket);
        return NULL;
    }

	/* examines kind of command */
    if (strncmp(command, "issueJob", 8) == 0) 
	{
        issueJob(client_socket, command);
    } 
	else if (strncmp(command, "setConcurrency", 14) == 0)
	{
		setConcurrency(client_socket, command);
        close(client_socket);
    }
	else if (strncmp(command, "stop", 4) == 0)
	{
        stop(client_socket, command);
        close(client_socket);
    } 
	else if (strncmp(command, "poll", 4) == 0) 
	{
        poll(client_socket);
        close(client_socket);
    } 
	else if (strncmp(command, "exit", 4) == 0) 
	{
        terminate(client_socket);
        exit(0);
    }
    return NULL;
}

/* Handle worker thread */
void* worker_thread_function(void* arg) 
{
    while (1) 
	{
        pthread_mutex_lock(&Qmutex);
        while (Qcount == 0) 
		{
            pthread_cond_wait(&QnotEmpty, &Qmutex);
        }

		/* Extracts job */
        task job = Q[front];
        front = (front + 1) % Qsize;
        Qcount--;

        pthread_cond_signal(&QnotFull);
        pthread_mutex_unlock(&Qmutex);

        int pid = fork();
        if (pid == 0) 
		{ // Child process
	
			/* creates temp file */
            char output_filename[50];
            sprintf(output_filename, "%d.output", getpid());
            int output_fd = open(output_filename, O_CREAT | O_WRONLY, 0644);
			
			/* reshape stdout */
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);

			/* actual command execution */
            execlp("/bin/sh", "sh", "-c", job.job_command, NULL);
            perror("execlp failed");
            exit(1);
        } 
		else 
		{ // Parent process
            int status;
			/* waiting child to end */
            waitpid(pid, &status, 0);

			/* reads from tem file */
            char output_filename[50];
            sprintf(output_filename, "%d.output", pid);
            int output_fd = open(output_filename, O_RDONLY);
            if (output_fd < 0) 
			{
                perror("Failed to open output file");
                continue;
            }
            char packet[SIZE];
            int bytes_read;
			
			/* inform client START MESSAGE */
			snprintf(packet, SIZE, "----- %s output start------\n", job.jobID);
			send(job.client_socket, packet, strlen(packet), 0);
			/* inform client with execution results */
            while ((bytes_read = read(output_fd, packet, sizeof(packet))) > 0) 
			{
                send(job.client_socket, packet, bytes_read, 0);
            }
            close(output_fd);
            remove(output_filename);	/* get rid of tem file */
			/* inform client END MESSAGE */
			snprintf(packet, SIZE, "----- %s output end------\n", job.jobID);
			send(job.client_socket, packet, strlen(packet), 0);
            close(job.client_socket);
        }
    }
    return NULL;
}

int main(int argc, char **argv) 
{
    if (argc != 4) 
	{
        fprintf(stderr, "Usage: %s [portnum] [bufferSize] [threadPoolSize]\n", argv[0]);
        exit(1);
    }

    int portnum = atoi(argv[1]);			/* Port number where server is listening */
    Qsize = atoi(argv[2]);					/* Maximum number of pending jobs */
    int thread_pool_size = atoi(argv[3]);	/* Total number of worker threads in the thread pool */

	/* Queue of pending jobs */
    Q = malloc(Qsize * sizeof(task));

	/* Establish server socket */
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) 
	{
        error("ERROR opening socket");
    }

    struct sockaddr_in server_addr;
    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(portnum);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
	{
        error("ERROR on binding");
    }

    listen(server_socket, 5);

	/* Create army of threads */
    pthread_t thread_pool[thread_pool_size];
    for (int i = 0; i < thread_pool_size; i++)
	{
        pthread_create(&thread_pool[i], NULL, worker_thread_function, NULL);
    }

	/* listen and execute commands */
    while (1) 
	{
        struct sockaddr_in client_addr;
		
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) 
		{
            error("ERROR on accept");
        }

        int *arg = malloc(sizeof(*arg));
        *arg = client_socket;
        pthread_t controller_thread;
        pthread_create(&controller_thread, NULL, controller_thread_function, arg);
        pthread_detach(controller_thread);
    }
	/* clean up */
    close(server_socket);
    free(Q);
    return 0;
}