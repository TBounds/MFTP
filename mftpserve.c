#include "mftp.h"

char *E = "E";
char *A = "A";

// Prototypes for mftpserve
void errorPrint(char * message);
int serverDataConnect(int connectfd, char *hostName);
void errorResponse(int listenfd, char *errorMessage);

int main(void){
    char clientMessage[BUFFER_SIZE], errorMessage[BUFFER_SIZE];
    int listenfd, controlfd, datafd;
    struct sockaddr_in servAddr, clientAddr;
    struct hostent* hostEntry;
    char *hostName;
    socklen_t length;
    pid_t pid;
   
   
    // Make a socket
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Socket");
        exit(errno);
    }
  
    // Bind the socket to a port  
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(MY_PORT_NUMBER);
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind (listenfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
        perror("Bind");
        exit(errno);
    }
  
    // Listen and accept connections
    if(listen(listenfd, 4) != 0){
        perror("Listen");
        exit(errno);
    }
  
    
    length = sizeof(struct sockaddr_in);
    
    // Wait for connection loop.
    while(1){ 
    
        printf("Waiting for a connection.\n");
	
        // Accept a connection.
        if((controlfd = accept(listenfd, (struct sockaddr *) &clientAddr, &length)) < 0){
            printf("%s\n", strerror(errno));
            exit(errno);
        }
        
        // Get the client's name
        if((hostEntry = gethostbyaddr(&(clientAddr.sin_addr), sizeof(struct in_addr), AF_INET)) != NULL){
            hostName = hostEntry->h_name;
            printf("%s has connected.\n", hostName);
        }
        else
            printf("Unnamed client has connected.\n");
    
        // Create child process to handle the client. 
        if((pid = fork()) == 0){     
            while(1){
                // Read from the client.
                readMessage(controlfd, clientMessage);
                
                // Check if the clientMessage = "D"
                /*------------------ DATA ------------------*/
                if(strcmp(clientMessage, "D") == 0){
                    printf("Creating data connection with %s\n", hostName);
                    datafd = serverDataConnect(controlfd, hostName);
                }
                /*------------------- RCD -------------------*/
                else if(clientMessage[0] == 'C'){     // RCD
                    char cwd[BUFFER_SIZE]; /* DEBUG */
                    char *pathName = clientMessage;
                    
                    // Check if it is a valid pathname.
                    if(chdir(++pathName) < 0){
                        strcpy(errorMessage, "0");
                        errorResponse(controlfd, errorMessage);    // Tell client an error has occured. 
                    }
                    else{
                        getcwd(cwd, BUFFER_SIZE);
                        printf("%s's cwd >> %s\n", hostName, cwd); /* DEBUG */
                        strcpy(errorMessage, "0");
                        writeMessage(controlfd, errorMessage, A);
                    }
                }
                /*------------------ RLS ------------------*/
                else if(strcmp(clientMessage, "L") == 0){
                    
                    // Send acknowledgement to client.
                    strcpy(errorMessage, "0");
                    writeMessage(controlfd, errorMessage, A);
                    
                    if(fork() == 0){    // Child
                        // Child's output -> datafd
                        dup2(datafd, 1);
                        // Execute ls
                        execlp("ls", "ls", "-l", (char *)NULL);
                    }
                    else{                    // Parent
                        wait(NULL);
                        close(datafd);
                    }
                }
                /*------------------ GET/SHOW ------------------*/
                else if(clientMessage[0] == 'G'){    
                    int filefd;
                    ssize_t bytesRead;
                    char *fileName = clientMessage;
                    struct stat sb;
                    
                    if(stat(++fileName, &sb) == -1){
                        // Error response with errno
                        strcpy(errorMessage, "0");
                        errorResponse(controlfd, errorMessage);
                    }
                    else if(S_ISREG(sb.st_mode)){
                        // If the file doesn't exist, create it and open it for writing.
                        if((filefd = open(fileName, O_RDONLY)) == -1){
                            // Error response with errno
                            strcpy(errorMessage, "0");
                            errorResponse(controlfd, errorMessage);
                        }
                        else{
                            // Send acknowledgement to client.
                            strcpy(errorMessage, "0");
                            writeMessage(controlfd, errorMessage, A);
                            
                            do{
                                // Write what the server sends into the file.
                                bytesRead = read(filefd, clientMessage, BUFFER_SIZE);
                                write(datafd, clientMessage, bytesRead);
                            }while(bytesRead == BUFFER_SIZE);
                            
                            close(filefd);
                            close(datafd);
                        }
                    }
                    else{
                        // Print error, not reg file or is dir.
                        strcpy(errorMessage, "File is not regular.");
                        errorResponse(controlfd, errorMessage);
                        close(datafd);
                    }
                }
                /*------------------ PUT ------------------*/
                else if(clientMessage[0] == 'P'){
                    int filefd;
                    ssize_t bytesRead;
                    char *fileName = clientMessage;
                
                    // If the file doesn't exist, create it and open it for writing.
                    if((filefd = open(++fileName, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0666)) == -1){
                        // Error response with errno
                        strcpy(errorMessage, "0");
                        errorResponse(controlfd, errorMessage);
                    }
                    else{
                        // Send acknowledgement to client.
                        strcpy(errorMessage, "0");
                        writeMessage(controlfd, errorMessage, A);
                        
                        do{
                            // Write what the server sends into the file.
                            bytesRead = read(datafd, clientMessage, BUFFER_SIZE);
                            write(filefd, clientMessage, bytesRead);
                        }while(bytesRead == BUFFER_SIZE);
                        
                        close(filefd);
                        close(datafd);
                    }
                }
                /*------------------ EXIT ------------------*/
                else if(strcmp(clientMessage, "Q") == 0){
                    printf("Exit command recieved from %s.\n", hostName);
                    
                    // Send acknowledgement.
                    strcpy(errorMessage, "0");
                    writeMessage(controlfd, errorMessage, A);
                    
                    close(listenfd);
                    exit(1);
                }
            }
        }
        else if (pid < 0){
            strcpy(errorMessage, "0");
            errorResponse(controlfd, errorMessage);
        }
        else{    // Parent
            close(controlfd);
            signal(SIGCHLD, SIG_IGN); // Kill zombie processes
        }
    }
}

/*-----------------------------------------------------------------------------------------*/
/*----------------------------- serverDataConnect --------------------------------*/
/*-----------------------------------------------------------------------------------------*/
int serverDataConnect(int controlfd, char *hostName){
    
    int newListenfd, newPortNum, datafd;
    struct sockaddr_in servAddr, dataAddr, clientAddr;
    char newPortString[BUFFER_SIZE];
    char *errorMessage = "0";
    socklen_t  dataAddrLen, length;
    
    // create new socket with socket()
    if((newListenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Socket");
        errorResponse(controlfd, errorMessage);    // Tell client an error has occured. 
        exit(errno);
    }
    
    // give the socket a name with bind()
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = 0;
    
    if (bind (newListenfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
        perror("Bind");
        errorResponse(controlfd, errorMessage);    // Tell client an error has occured. 
        exit(errno);
    }
    
    // Listen and accept connections
    if(listen(newListenfd, 1) != 0){
        perror("Listen");
        errorResponse(controlfd, errorMessage);    // Tell client an error has occured. 
        exit(errno);
    }
    
    // getsockname()
    dataAddrLen = sizeof(dataAddr);
    if((getsockname(newListenfd, (struct sockaddr*) &dataAddr, &dataAddrLen)) == -1)
        errorResponse(controlfd, errorMessage);    // Tell client an error has occured. 
    
    // ntohs()
    newPortNum = ntohs(dataAddr.sin_port);
    
    // Send port number to client with acknowledgement.
    sprintf(newPortString, "A%d\n", newPortNum); 
    if((write(controlfd, newPortString, strlen(newPortString))) == -1)
        errorResponse(controlfd, errorMessage);    // Tell client an error has occured. 
    
    // Accept a connection.
    length = sizeof(struct sockaddr_in);
    if((datafd = accept(newListenfd, (struct sockaddr *) &clientAddr, &length)) < 0){
        printf("%s\n", strerror(errno));
        errorResponse(controlfd, errorMessage);    // Tell client an error has occured. 
        exit(errno);
    }
    
    printf("Data connection with %s successful.\n", hostName);   /* DEBUG */
    
    return datafd;
    return datafd;
}

/*-----------------------------------------------------------------------------------------*/
/*---------------------------------- readMessage ----------------------------------*/
/*-----------------------------------------------------------------------------------------*/
void readMessage(int listenfd, char * clientMessage){
    int i = 0;
    
    // Read char by char until a \n
    while(1){
        if((read(listenfd, &clientMessage[i], 1)) == -1)
            writeMessage(listenfd, strerror(errno), E);    // Tell client an error has occured.
        if(clientMessage[i] == '\n')
            break;
        i++;
    }
    // Replace \n with \0
    clientMessage[i] = '\0';  
}

/*-----------------------------------------------------------------------------------------*/
/*---------------------------------- writeMessage ----------------------------------*/
/*-----------------------------------------------------------------------------------------*/
void writeMessage(int listenfd, char * errorMessage, char *symbol){
    char message[BUFFER_SIZE];
    
    // Create Symbol<message>
    strcpy(message, symbol);
    
    if(errorMessage[0] != '0')                  // E<message>
        strcat(message, errorMessage);
    else                                                    // Acknowledgement doesn't require a message.
        strcat(message, "\n");
    
    // Write Command<pathname> to server
    int len = strlen(message);
    if((write(listenfd, message, len)) == -1){
        printf("Error with writing to client.\n");
        exit(-1);
    }
}

/*-----------------------------------------------------------------------------------------*/
/*---------------------------------- errorResponse --------------------------------*/
/*-----------------------------------------------------------------------------------------*/
void errorResponse(int listenfd, char *errorMessage){
    
    char response[BUFFER_SIZE];
    strcpy(response, "E");
    
    if (errorMessage[0] == '0')   // No custom error.
        strcat(response, strerror(errno));
    else                                        // Custom error message.
        strcat(response, errorMessage); 
    
    strcat(response, "\n");
    
    int len = strlen(response);
    write(listenfd, response, len);
}
