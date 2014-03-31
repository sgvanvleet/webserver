
const char * usage =
"                                                               \n"
"myhttpd server:                                                \n"
"                                                               \n"
"Simple server program that shows how to use socket calls       \n"
"in the server side.                                            \n"
"                                                               \n"
"To use it in one window type:                                  \n"
"                                                               \n"
"   myhttpd <port>   	                                        \n"
"                                                               \n"
"Where 1024 < port < 65536.             			\n"
"                                                               \n"
"In another window type:                                        \n"
"                                                               \n"
"   telnet <host> <port>                                        \n"
"                                                               \n"
"where <host> is the name of the machine where myhttpd 	        \n"
"is running. <port> is the port number you used when you run    \n"
"myhttpd. 	                                                \n"
"                                                               \n"
"Then type your name and return. You will get a greeting and    \n"
"the time of the day.                                           \n"
"                                                               \n";

#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int QueueLength = 5;
const int MAX_MESSAGE = 10240;
const int BYTES = 1024;
char * ROOT;
const char * dir = "/http-root-dir/htdocs";

// Processes time request
void processTimeRequest( int socket );
void respondWithPage(int socket);

int
main( int argc, char ** argv )
{
  // Print usage if not enough arguments
  if ( argc < 2 ) {
    fprintf( stderr, "%s", usage );
    exit( -1 );
  }

  // Get the port from the arguments
  int port = atoi( argv[1] );

  ROOT = getenv("PWD");
  strncat( ROOT, dir, strlen(dir) );
  

  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);

  // Allocate a socket
  int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
  if ( masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the same port number
  int optval = 1; 
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
      (char *) &optval, sizeof( int ) );

  // Bind the socket to the IP address and port
  int error = bind( masterSocket,
      (struct sockaddr *)&serverIPAddress,
      sizeof(serverIPAddress) );
  if ( error ) {
    perror("bind");
    exit( -1 );
  }

  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections
  error = listen( masterSocket, QueueLength);
  if ( error ) {
    perror("listen");
    exit( -1 );
  }

  // loop forever
  while ( 1 ) {

    // Accept incoming connections
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
    int slaveSocket = accept( masterSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);

    if ( slaveSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }

    // Process request.
    //processTimeRequest( slaveSocket );
    respondWithPage( slaveSocket );

    // Close socket
    close( slaveSocket );
  }

}


void respondWithPage(int socket) {
  char message[MAX_MESSAGE];
  char * request[3];
  char data_to_send[BYTES];
  char path[MAX_MESSAGE];

  int bytes_received; 
  int fd;
  int bytes_read;

  memset( (void*)message, (int)'\0', MAX_MESSAGE );
  bytes_received = recv(socket, message, MAX_MESSAGE, 0);

  if (bytes_received< 0) { // error
    fprintf(stderr, "recv error\n");
  } else if (bytes_received == 0) { // socket closed
    fprintf(stderr, "client unexpectedly closed socket\n");

  } else { // message received!
    
    printf("%s", message);
    request[0] = strtok (message, " \t\n");

    if ( strncmp(request[0], "GET\0", 4) == 0 ) {
      request[1] = strtok (NULL, " \t");
      request[2] = strtok (NULL, " \t\n");

      // check for bad requests (error 400)
      if ( strncmp( request[2], "HTTP/1.0", 8) != 0 && 
	   strncmp( request[2], "HTTP/1.1", 8) != 0 ) {
	write(socket, "HTTP/1.0 400 Bad Request\n", 25);

      // reply with the file
      } else {

	// if we get a request for '/' send index.html by default
	if ( strncmp(request[1], "/\0", 2) == 0 ) {
	  request[1] = "/index.html";        
	}

	strcpy(path, ROOT);
	strcpy(&path[strlen(ROOT)], request[1]);

	printf("file requested: %s\n", path);

	// read file and send it over the socket
	if ( (fd = open(path, O_RDONLY)) != -1 ) {
	  send(socket, "HTTP/1.0 200 OK\n\n", 17, 0);

	  while ( (bytes_read = read(fd, data_to_send, BYTES)) > 0 ) {
	    write (socket, data_to_send, bytes_read);
	  }

	// file not found
	} else { // ERROR 404!!!
	  write(socket, "HTTP/1.0 404 Not Found\n", 23); 
	}
      } 
    }
  } // end message received
}

