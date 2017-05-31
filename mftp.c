#include "mftp.h"

// Prototypes for mftp
int makeDataConnect(int socketfd, char *argv1);
int getDataPortNum(char *servMessage);
void findCommand(char *buffer, int socketfd, char *argv1);

int main(int argc, char **argv){
  
  struct in_addr **pptr;
  struct sockaddr_in servAddr;
  struct hostent* hostEntry;
  int socketfd;

  // Making a connection from a client
  if((socketfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
	  printf("%s\n", strerror(errno));
	  exit(errno);
  }
  
  memset(&servAddr, 0, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port = htons(MY_PORT_NUMBER);
  
  // Check number of arguments.
  if(argc != 2){
    printf("Invalid number of arguments.\n");
    exit(-1);
  }
  
  // Getting a text host name  
  if((hostEntry = gethostbyname(argv[1])) == NULL){ /* test hostEntry */
  	herror("gethostbyname");
	exit(errno);
  }
  
  // Set up the address of the server
  pptr = (struct in_addr **) hostEntry->h_addr_list;
  memcpy(&servAddr.sin_addr, *pptr, sizeof(struct in_addr));
   
  // Connect to the server
  if((connect(socketfd, (struct sockaddr *) &servAddr, sizeof(servAddr))) < 0){
    printf("Connect error. %s\n", strerror(errno));
    exit(errno);
  }
  
  /*------------------------- Read user input. -------------------------*/
  char buffer[BUFFER_SIZE];
  char cwd[BUFFER_SIZE];
  while(1){
    
    // Get current directory.
    getcwd(cwd, BUFFER_SIZE);
    printf("%s >> ", cwd);   
    
	// Read user input.
	fgets(buffer, BUFFER_SIZE, stdin);
    
    // Find what command the user typed (if any)
    findCommand(buffer, socketfd, argv[1]);
	
  }
  
  return 0;
}

/*-----------------------------------------------------------------------------------------*/
/*------------------------------------- errorPrint --------------------------------------*/
/*-----------------------------------------------------------------------------------------*/
void errorPrint(char * message){
    printf("%s: %s\n", message, strerror(errno));
}

/*-----------------------------------------------------------------------------------------*/
/*---------------------------------- findCommand -----------------------------------*/
/*-----------------------------------------------------------------------------------------*/
void findCommand(char *buffer, int socketfd, char * argv1){
    char *input;		    // User input to be parsed. Used with strtok.
    char *pathName;  // String to point to the pathName pointed to by strtok.
    char servMessage[BUFFER_SIZE];  
    char errMessage[BUFFER_SIZE];
    
    // Parse for commands.
	input = strtok(buffer, " ");
	
	/*------------------ EXIT ------------------*/
	if(strcmp(input, "exit\n") == 0){
        char *Q = "Q";
        pathName = NULL;
        
        // Tell the server to end the process handling this client.
        writeMessage(socketfd, pathName, Q); 
        
        // Read the server's response.
        readMessage(socketfd, servMessage);
        
        // Print the error if the server returns an error.
        if(servMessage[0] == 'E'){
            sscanf(servMessage, "E%[^\t\n]", errMessage);
            printf("%s\n", errMessage);
        }
        else
            exit(1);
	}
    /*------------------- CD -------------------*/
	else if(strcmp(input, "cd") == 0){
        pathName = strtok(NULL, " ");
        
        // Replace the \n at the end of the pathName with a \0
        pathName[strlen(pathName) - 1] = '\0';
       
        // Check if it is a valid pathname.
        if(chdir(pathName) < 0)
             errorPrint("Invalid path");
         
	}
    /*------------------ RCD ------------------*/
	else if(strcmp(input, "rcd") == 0){
        char *C = "C";
        pathName = strtok(NULL, " ");                   // Grab the pathName
        writeMessage(socketfd, pathName, C); 

        // Read the server's response.
        readMessage(socketfd, servMessage);
        
        // Print the error if the server returns an error.
        if(servMessage[0] == 'E'){
            sscanf(servMessage, "E%[^\t\n]", errMessage);
            printf("%s\n", errMessage);
        }
	}
    /*------------------- LS --------------------*/
	else if(strcmp(input, "ls\n") == 0){
        
        if(fork() == 0){        // Child
            // Create pipe
            int fd[2];
            pipe(fd);
                
            if(fork() == 0){    // Grandchild
                // Grandchild's output -> write end of pipe
                close(fd[0]);
                dup2(fd[1], 1);
                // Execute ls
                execlp("ls", "ls", "-l", (char *)NULL);
            }
            else{                   // Child
                // Wait for child to die (ls to execute)
                wait(NULL);
            }
                // Child's input -> read end of pipe
                close(fd[1]);
                dup2(fd[0], 0);
                // Execute more
                execlp("more", "more", "-20", (char* ) NULL); 
        }
        else    // Parent
            wait(NULL);
	}
    /*------------------ RLS ------------------*/
	else if(strcmp(input, "rls\n") == 0){
        int connectfd;
        char *L = "L";
        
        // Make a data connection.
        connectfd = makeDataConnect(socketfd, argv1);   // connectfd == -1 on error
        
        // Write an L to the server.
        pathName = NULL;
        writeMessage(socketfd, pathName,  L);
       
        // Read the server's response.
        readMessage(socketfd, servMessage);
        
        // Print the error if the server returns an error.
        if(servMessage[0] == 'E'){
            sscanf(servMessage, "E%[^\t\n]", errMessage);
            printf("%s\n", errMessage);
        }
       
       if(connectfd > 0){
            if(fork() == 0){
                dup2(connectfd, 0);     
                close(connectfd);         // Pre-caution
                // Execute more
                execlp("more", "more", "-20", (char* ) NULL); /* NEEDS TO BE 20 */
            }
            else
                wait(NULL);
       }
       close(connectfd);
	}
    /*------------------ GET ------------------*/
	else if(strcmp(input, "get") == 0){
        int connectfd, filefd;
        ssize_t bytesRead;
        char *G = "G";
        
        // Make a data connection.
        connectfd = makeDataConnect(socketfd, argv1);   // connectfd == -1 on error
        
        // Send G
        pathName = strtok(NULL, " ");                   // Grab the fileName
        writeMessage(socketfd, pathName, G); 
        
        // Read the server's response.
       readMessage(socketfd, servMessage);
        
        // If server responds with an error.
        if(servMessage[0] == 'E'){
            sscanf(servMessage, "E%[^\t\n]", errMessage);
            printf("Error: %s\n", errMessage);
            close(connectfd);
        }
        else{
           // Replace the \n with a \0
           pathName[strlen(pathName) - 1] = '\0';

           // If the file doesn't exist, create it and open it for writing.
           if((filefd = open(pathName, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0666)) == -1)
               errorPrint("Problem in creating the file");
           else{
               do{
                   // Write what the server sends into the file.
                   bytesRead = read(connectfd, servMessage, BUFFER_SIZE);
                   write(filefd, servMessage, bytesRead);
               }while(bytesRead == BUFFER_SIZE);
               
               close(filefd);
               close(connectfd);
           }
        }
	}
    /*------------------ SHOW ------------------*/
	else if(strcmp(input, "show") == 0){
        int connectfd;
        char *G = "G";
        
        // Make a data connection.
        connectfd = makeDataConnect(socketfd, argv1);   // connectfd == -1 on error
        
        // Write a G to the server.
        pathName = strtok(NULL, " ");                   // Grab the file name
        writeMessage(socketfd, pathName,  G);
       
        // Read the server's response.
        readMessage(socketfd, servMessage);
        
        // Print the error if the server returns an error.
        if(servMessage[0] == 'E'){
            sscanf(servMessage, "E%[^\t\n]", errMessage);
            printf("%s\n", errMessage);
        }
       
       if(connectfd > 0){
            if(fork() == 0){
                dup2(connectfd, 0);     
                close(connectfd);         // Pre-caution
                // Execute more
                execlp("more", "more", "-20", (char* ) NULL); /* NEEDS TO BE 20 */
            }
            else
                wait(NULL);
       }
       
       close(connectfd);
	}
    /*------------------ PUT ------------------*/
	else if(strcmp(input, "put") == 0){
        int connectfd, filefd;
        ssize_t bytesRead;
        char *P = "P";
        struct stat sb;
        
        // Before making connection, test the validity of the file.
        pathName = strtok(NULL, " ");                   // Grab the fileName
        
        // Replace the \n with a \0
        pathName[strlen(pathName) - 1] = '\0';
        
        if(stat(pathName, &sb) == -1)
            errorPrint("Problem with stat");
        else if(S_ISREG(sb.st_mode)){
        
            // Make a data connection.
            connectfd = makeDataConnect(socketfd, argv1);   // connectfd == -1 on error
            
            // Send P
            pathName[strlen(pathName)] = '\n';
            writeMessage(socketfd, pathName, P); 
            
            // Read the server's response.
            readMessage(socketfd, servMessage);
            
            // If server responds with an error.
            if(servMessage[0] == 'E'){
                sscanf(servMessage, "E%[^\t\n]", errMessage);
                printf("%s\n", errMessage);
            }
            else{
                pathName[strlen(pathName) - 1] = '\0';
                // If the file doesn't exist, create it and open it for writing.
                if((filefd = open(pathName, O_RDONLY)) == -1)
                    errorPrint("Problem in creating the file");
                else{
                    do{
                        // Write what the server sends into the file.
                        bytesRead = read(filefd, servMessage, BUFFER_SIZE);
                        write(connectfd, servMessage, bytesRead);
                    }while(bytesRead == BUFFER_SIZE);
                    
                    close(filefd);
                    close(connectfd);
                }
            }
        }
        else
            printf("File is not regular or does not exist.\n");
	}
	else{
		printf("Invalid command. \nValid commands are: exit, cd <pathname>, rcd <pathname>, ls, rls, get <pathname>, show <pathname>, put <pathname>\n");
	}  
}

/*-----------------------------------------------------------------------------------------*/
/*------------------------------- makeDataConnect --------------------------------*/
/*-----------------------------------------------------------------------------------------*/
int makeDataConnect(int socketfd, char *argv1){
    int dataPortNum, connectfd;
    char *D= "D\n"; 
    char servMessage[BUFFER_SIZE];
    char errMessage[BUFFER_SIZE];
    struct sockaddr_in servAddr;
    struct in_addr **pptr;
    struct hostent* hostEntry;
    
    /* You can do this to pull of the port number*/
    /* sscanf(str, "A%i", &p) str = pulling stuff from, &p - address of int var to save portnumber */
    
    // Tell server to create a data connection.
    write(socketfd, D, 2);
    
    // Read what the server responds with.
    readMessage(socketfd, servMessage);
    
    // Print the error if the server returns an error.
    if(servMessage[0] == 'E'){
        sscanf(servMessage, "E%[^\t\n]", errMessage);
        printf("%s\n", errMessage);
        return -1;
    }
    
    // Check if the server responded with an error or an acknowledge
    dataPortNum = getDataPortNum(servMessage);
    
    // If checkMessage returns > 0, that is the port number for the data connection.
    if(dataPortNum > 0){
       
        // Making a connection from a client
        if((connectfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
            printf("%s\n", strerror(errno));
            exit(errno);
        }
        
        // Change the port number.
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons(dataPortNum);
        
        // Getting a text host name  
        if((hostEntry = gethostbyname(argv1)) == NULL){ /* test hostEntry */
            herror("gethostbyname");
            exit(errno);
        }
        
        // Set up the address of the server
        pptr = (struct in_addr **) hostEntry->h_addr_list;
        memcpy(&servAddr.sin_addr, *pptr, sizeof(struct in_addr));
        
        // Make a data connection to the server.
        if((connect(connectfd, (struct sockaddr *) &servAddr, sizeof(servAddr))) < 0){
            printf("Connect error. %s\n", strerror(errno));
            exit(errno);
        }
    }

    return connectfd;
}

/*---------------------------------------------------------------------------------------------*/
/*--------------------------------- getDataPortNum -----------------------------------*/
/*---------------------------------------------------------------------------------------------*/
int getDataPortNum(char *servMessage){
    char errMessage[BUFFER_SIZE];
    int dataPortNum;
    
    if(servMessage[0] == 'A'){
        sscanf(servMessage, "A%i", &dataPortNum);
        return dataPortNum;
    }
    else{
        sscanf(servMessage, "E%[^\t\n]", errMessage);
        printf("%s\n", errMessage);
    }
    
    return -1;
}

/*-----------------------------------------------------------------------------------------*/
/*---------------------------------- readMessage ------------------------------------*/
/*-----------------------------------------------------------------------------------------*/
void readMessage(int socketfd, char * servMessage){
    int i = 0;
    
    // Read char by char until a \n
    while(1){
        read(socketfd, &servMessage[i], 1);
        if(servMessage[i] == '\n')
            break;
        i++;
    }
    // Replace \n with \0
    servMessage[i] = '\0';  
}

/*-----------------------------------------------------------------------------------------*/
/*---------------------------------- writeMessage ----------------------------------*/
/*-----------------------------------------------------------------------------------------*/
void writeMessage(int socketfd, char * pathName, char * cmd){
    char servCmd[BUFFER_SIZE];
    
    // Create Command<pathName>
    strcpy(servCmd, cmd);
    
    if(pathName != NULL)                // Some commands don't require a pathname (rls)
        strcat(servCmd, pathName);
    else
        strcat(servCmd, "\n");
    
    // Write Command<pathname> to server
    int len = strlen(servCmd);
    write(socketfd, servCmd, len);
}
