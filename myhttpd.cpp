#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

#define MAX_MESSAGE 2000
#define BYTES 1024
#define DEFAULT_PORT 14566

unsigned int USE_THREADS = 0;
unsigned int USE_FORKS = 0;
unsigned int USE_POOL = 0;

int QueueLength = 5;
char * ROOT;
char OPTION = '\0'; // cli flag option, if any
const char * dir = "/http-root-dir";

pthread_mutex_t mutex;

void * responseHandler(void* socketDescriptor);
void * respond( int socketDescriptor);
void * poolResponseHandler(void * );

int main( int argc, char ** argv ) {
  int port;

  // handle cli arguments
  if ( argc == 2 ) {

    if ( argv[1][0] == '-' ) { // we have a flag and no port
      if ( (char)argv[1][1] == '-' || argv[1][1] == 'h') { 
	fprintf(stderr, "%s", usage);
	exit (-1);
      }
      OPTION = (char)argv[1][1];
      port = DEFAULT_PORT;
    } else { // port and no flag
      port = atoi( argv[1] );
    }

  } else if ( argc == 3 ) { // port and flag
    OPTION = (char)argv[1][1];
    port = atoi( argv[2] );

  } else { // ya dun goofed
    fprintf( stderr, "%s", usage );
    exit( -1 );
  }

  printf("cli option = %c\n", (char)OPTION);

  ROOT = getenv("PWD");
  strncat( ROOT, dir, strlen(dir) );
  
  // Set the IP address and port for this server
  struct sockaddr_in serverIP; 
  memset( &serverIP, 0, sizeof(serverIP) );
  serverIP.sin_family = AF_INET;
  serverIP.sin_addr.s_addr = INADDR_ANY;
  serverIP.sin_port = htons((u_short) port);

  // Allocate a socket
  int masterSocket =  socket(AF_INET, SOCK_STREAM, 0);
  if ( masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the same port number
  int optval = 1; 
  if (setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, (char *) &optval,
	sizeof( int ) ) ) {
    perror( "setsockopt" );
    exit( -1 );
  }

  // Bind the socket to the IP address and port
  if (bind(masterSocket, (struct sockaddr *)&serverIP, sizeof(serverIP))) {
    perror("bind");
    exit( -1 );
  }
  printf("bind done\n");

  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections
  if ( listen( masterSocket, QueueLength) ) {
    perror("listen");
    exit( -1 );
  }

  struct sockaddr_in clientIPAddress;
  int addressLength = sizeof( clientIPAddress );
  int clientSocket;
  int * clientSock;

  // loop forever

  if (OPTION == 'p') {
    // spawn 5 threads with poolResponseHandler running
    printf("creating pool of threads\n");
    pthread_t pool[5];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

    pthread_mutex_init(&mutex, NULL);

    int * masterSock;
    masterSock = (int*)malloc( 1 );
    *masterSock = masterSocket;

    int i;
    for (i = 0; i < 5; i++) {
      pthread_create( &(pool[i]), &attr, 
	  (void * (*)(void*))poolResponseHandler, 
	  (void *) masterSock);
    }

    for (i = 0; i < 5; i++) {
      pthread_join(pool[i], NULL);
    }
    
  } else {
    printf("waiting for incoming connections\n");
    while ( (clientSocket = accept( masterSocket,
				(struct sockaddr *)&clientIPAddress,
				(socklen_t*)&addressLength)) ) {

      printf("connection accepted\n");

      if ( OPTION == 't' ) { 
	// create a new thread for each requested
	clientSock = (int*)malloc( 1 );
	*clientSock = clientSocket;

	pthread_t cThread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	printf("spawning thread to handle response\n");
	if (pthread_create(&cThread, &attr, 
	      (void * (*)(void *))responseHandler, (void *)clientSock) < 0) {
	  perror("failed to create thread");
	  return 1;
	}

      } else if ( OPTION == 'f' ) {
	// fork for each request
	if (fork() == 0) { // child
	  printf("responding in forked child process\n");
	  respond( clientSocket );
	  exit(0);
	} else { // parent
	}
      } else if ( OPTION == 'p' ) {
      } else { 
	// single threaded behavior
	printf("responding single-threaded\n");
	respond( clientSocket );
      }
    } // end of while loop

    if ( clientSocket < 0 ) {
      perror( "accept failed" );
      exit( -1 );
    }
  }
}

void * poolResponseHandler(void * masterSocketDescriptor) {
  int masterSocket = *(int*)masterSocketDescriptor;

  while (1) {
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress);

    // don't want multiple threads calling accept() at the same time
    pthread_mutex_lock(&mutex);

    int clientSocket = accept(masterSocket, 
	(struct sockaddr*)&clientIPAddress,
	(socklen_t*)&alen);

    pthread_mutex_unlock(&mutex);

    int * clientSock;
    clientSock = (int*)malloc( 1 );
    *clientSock = clientSocket;

    if (clientSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }

    //process request
    responseHandler(clientSock);
  }
}

char * findContentType( char * filename) {
  int i;
  char extension[10];

  // loop backwards until we find a .
  for (i = strlen(filename); i >= 0; i--) {
    if ( filename[i] == '.') {
      break;
    }
  }
  i++;

  // i is now the location of '.' preceding the file extension
  int m = 0;
  while ( filename[i] != '\0' ) {
    extension[m] = filename[i]; // grab the extension characters
    m++;
    i++;
  }
  extension[m] = '\0'; // null terminate the extension string

  printf("extension: %s\n", extension);

  if ( !strcmp(extension, "html") ) {
    return "text/html";
  } else if (  !strcmp(extension, "gif")) {
    return "image/webp";
  } else {
    return "text/plain";
  }
}

void * respond( int socket ) {
  char message[MAX_MESSAGE];
  char * request[3];
  char data_to_send[BYTES];
  char path[MAX_MESSAGE];

  //int socket = *(int*)socketVoid;
  int bytes_received; 
  int bytes_read;
  int fd;

  memset( (void*)message, (int)'\0', MAX_MESSAGE );
  // receive message on socket
  bytes_received = recv(socket, message, MAX_MESSAGE, 0);
  
  if (bytes_received == -1) { // error
    fprintf(stderr, "recv error\n");
    printf("mesg: %s\n", message);
  } else if (bytes_received == 0) { // socket closed
    fprintf(stderr, "client disconnected\n");
    fflush(stdout);
  } else { 
    // message received!
    printf("\n%s", message);
    
    // tokenize message
    request[0] = strtok (message, " \t\n");

    if ( strncmp(request[0], "GET\0", 4) == 0 ) {
      // if it's a GET, tokenize some more
      request[1] = strtok (NULL, " \t");
      request[2] = strtok (NULL, " \t\n");

      // check for bad requests (error 400)
      if ( strncmp( request[2], "HTTP/1.0", 8) != 0 && 
	   strncmp( request[2], "HTTP/1.1", 8) != 0 ) {
	write( socket, "HTTP/1.0 400 Bad Request\n", 25 );

      } else if ( !strncmp(request[1], "/cgi-bin/", strlen("/cgi-bin/")) ) {
	// CGI response
	pid_t pid;
	if ( (pid = fork()) == 0) {
	  // in the child process
	  
	  // tokenize the request to separate variable=value part
	  char * execvars[2];
      	  execvars[0] = strtok(request[1], "?");
	  execvars[1] = strtok(NULL, "\0");
	  if (execvars[1] != NULL) { // only if we have variables
	    setenv("REQUEST_METHOD", "GET", 1);
	    setenv("QUERY_STRING", execvars[1], 1);
	  }
	  
	  printf("QUERY_STRING: %s\n", getenv("QUERY_STRING"));

	  // expand the file location
	  if ( !strcmp( request[1] + strlen("/cgi-bin/"), "finger") ) {
	    execvars[1] = NULL;
	  } 
	  strcpy( path, ROOT );
	  strcat( path, execvars[0]);
	  execvars[0] = path;
	  printf("executing: %s\nargs: %s\n", execvars[0], execvars[1]);

	  // redirect output to socket
	  dup2(socket, STDOUT_FILENO);
	  dup2(socket, STDERR_FILENO);
	  close(socket);

	  printf("HTTP/1.1 200 Document follows\nServer: CS 252 lab5\n");
	  execvp(execvars[0], execvars);
	  printf("\n");
	} else {
	  // in the parent
	  pid_t endID = waitpid( pid, NULL, 0 ); // wait for process
	  printf("killed child: endID = %d\n", (int)endID);
	}

      } else {
	// reply with the file
	printf("request: %s\n", request[1]);

	strcpy( path, ROOT );
	strcpy( &path[strlen(ROOT)], "/htdocs" );

	// if we get a request for '/' send index.html by default
	if ( strncmp(request[1], "/\0", 2) == 0 ) {
	  request[1] = "/index.html";        
	} else if ( !strcmp( request[1], "/favicon.ico" ) ) {
	  // ignore requests for the favicon
	  return 0;
	}

	strcat(path, request[1]);

	printf("sending requested file: %s\n", path);
	char * contentType = findContentType(request[1]);
	char write_buf[1024];

	// read file and send it over the socket
	if ( (fd = open(path, O_RDONLY)) != -1 ) {

	  printf("writing doc, type: %s\n", contentType);

	  // write http header
	  sprintf(write_buf, 
	      "HTTP/1.1 200 Document follows\nServer: CS 252 lab5\nContent-type: %s\n\n", contentType);
	  write(socket, write_buf, strlen(write_buf));

	  // write document
	  while ( (bytes_read = read(fd, data_to_send, BYTES)) > 0 ) {
	    write (socket, data_to_send, bytes_read);
	  }
	  write(socket, "\n", 1);

	  printf("finished writing document\n");
	// file not found
	} else { // ERROR 404!!!
	  printf("404 file not found!\n");
	  sprintf(write_buf, 
	      "HTTP/1.1 404 File Not Found\nServer: CS 252 lab5\nContent-type: %s\n\n<html><h1>404 File Not Found</h1></html>\n",
	      contentType);
	  write(socket, write_buf, strlen(write_buf));
	} // end 404
      } // end reply with file
    } // end GET response
  } // end while

  printf("closing socket\n");
  shutdown( socket, 2);
  close( socket ); // Close socket
  return 0;
}


// responseHandler() is called by pthread_create()
void * responseHandler(void * socketDescriptor) {
  int socket = *(int*)socketDescriptor;

  respond(socket);
  return 0;
}
