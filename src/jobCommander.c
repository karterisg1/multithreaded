#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

/* establish connection to server */
int connect_to_server(const char *hostname, int port) 
{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
	{
        perror("ERROR opening socket");
		exit(1);
    }

    server = gethostbyname(hostname);
    if (server == NULL)
	{
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }

    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
	{
        perror("ERROR connecting");
		exit(1);
    }
    return sockfd;
}

int main(int argc, char **argv)
{
    if (argc < 4)
	{
        fprintf(stderr, "Usage: %s [serverName] [portNum] [jobCommanderInputCommand]\n", argv[0]);
        exit(1);
    }

    char *server_name = argv[1];	/* Domain where the server lives */
    int port_num = atoi(argv[2]);	/* Port number where server is listening */
    char command[1024] = {0};		/* join command line arguments in a single string */
    
	for (int i = 3; i < argc; ++i) 	/* starting from 4th argumment */
	{
        strcat(command, argv[i]);	/* joining */
        if (i < argc - 1) 			//
		{							//
            strcat(command, " ");	// separate with blank
        }							//
    }

	/* Connects to server */
    int server_socket = connect_to_server(server_name, port_num);
    if (server_socket < 0) 
	{
        fprintf(stderr, "Failed to connect to server\n");
        exit(1);
    }

	/* Sending command */
    if (send(server_socket, command, strlen(command), 0) < 0) 
	{
        perror("Failed to send command");
        close(server_socket);
        exit(1);
    }

	char buffer[1024];
      
	/* Handling commands */	  
	int nb = recv(server_socket, buffer, sizeof(buffer) - 1, 0);
	if (nb < 0) /* if none byte received */
	{
		perror("Failed to receive message");
		close(server_socket);
        exit(1);
	} 
    buffer[nb] = '\0';
    printf("%s\n", buffer);
	
	/* special case for issueJob: Prints file data containg results */
	if (strncmp(command, "issueJob", 8) == 0)
	{
		// Receive and print the job output
		while ((nb = recv(server_socket, buffer, sizeof(buffer) - 1, 0)) > 0) 
		{
			buffer[nb] = '\0';
			printf("%s", buffer);
		}
		if (nb < 0) 
		{
			perror("Failed to receive job output");
			close(server_socket);
			exit(1);
		}
	}
	close(server_socket);
    return 0;
}