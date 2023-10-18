#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <fstream>
using namespace std;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
string mailSpool;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data);
void signalHandler(int sig);
int processSend(int client_socket);
int createMailSpool(string dirName);
int writeUserFile(string username, string sender, string subject, string message);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
   socklen_t addrlen;
   struct sockaddr_in address, cliaddress;
   int reuseValue = 1;

   if(argc != 3){
      argv = argv;
      perror("Usage: ./twmailer-server <port> <mail-spool-directoryname>");
      return EXIT_FAILURE;
   }

   //get port from console argument: convert to int
   std::istringstream iss(argv[1]);
   int port;
   if(!(iss >> port)){
      perror("Invalid port - not a number");
      return EXIT_FAILURE;
   }

   //mail directory name
   string directory = argv[2];
   //set global varaible for mail spool directory
   mailSpool = directory;
   if(createMailSpool(directory)==-1){
      perror("Cannot create mail spool directory");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // SIGNAL HANDLER
   // SIGINT (Interrup: ctrl+c)
   // https://man7.org/linux/man-pages/man2/signal.2.html
   if (signal(SIGINT, signalHandler) == SIG_ERR)
   {
      perror("signal can not be registered");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as client)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error"); // errno set by socket()
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // SET SOCKET OPTIONS
   // https://man7.org/linux/man-pages/man2/setsockopt.2.html
   // https://man7.org/linux/man-pages/man7/socket.7.html
   // socket, level, optname, optvalue, optlen
   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEADDR,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reuseAddr");
      return EXIT_FAILURE;
   }

   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEPORT,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reusePort");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(port);

   ////////////////////////////////////////////////////////////////////////////
   // ASSIGN AN ADDRESS WITH PORT TO SOCKET
   if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
   {
      perror("bind error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // ALLOW CONNECTION ESTABLISHING
   // Socket, Backlog (= count of waiting connections allowed)
   if (listen(create_socket, 5) == -1)
   {
      perror("listen error");
      return EXIT_FAILURE;
   }

   while (!abortRequested)
   {
      /////////////////////////////////////////////////////////////////////////
      // ignore errors here... because only information message
      // https://linux.die.net/man/3/printf
      printf("Waiting for connections...\n");

      /////////////////////////////////////////////////////////////////////////
      // ACCEPTS CONNECTION SETUP
      // blocking, might have an accept-error on ctrl+c
      addrlen = sizeof(struct sockaddr_in);
      if ((new_socket = accept(create_socket,
                               (struct sockaddr *)&cliaddress,
                               &addrlen)) == -1)
      {
         if (abortRequested)
         {
            perror("accept error after aborted");
         }
         else
         {
            perror("accept error");
         }
         break;
      }

      /////////////////////////////////////////////////////////////////////////
      // START CLIENT
      // ignore printf error handling
      printf("Client connected from %s:%d...\n",
             inet_ntoa(cliaddress.sin_addr),
             ntohs(cliaddress.sin_port));
      clientCommunication(&new_socket); // returnValue can be ignored
      new_socket = -1;
   }

   // frees the descriptor
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown create_socket");
      }
      if (close(create_socket) == -1)
      {
         perror("close create_socket");
      }
      create_socket = -1;
   }

   return EXIT_SUCCESS;
}

void *clientCommunication(void *data)
{
   char buffer[BUF];
   int size;
   int *current_socket = (int *)data;

   ////////////////////////////////////////////////////////////////////////////
   // SEND welcome message
   strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands: \n SEND, LIST, READ, DEL, QUIT...\r\n");
   if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
   {
      perror("send failed");
      return NULL;
   }

   do
   {
      /////////////////////////////////////////////////////////////////////////
      // RECEIVE
      size = recv(*current_socket, buffer, BUF - 1, 0);
      if (size == -1)
      {
         if (abortRequested)
         {
            perror("recv error after aborted");
         }
         else
         {
            perror("recv error");
         }
         break;
      }

      if (size == 0)
      {
         printf("Client closed remote socket\n"); // ignore error
         break;
      }
      
      // remove ugly debug message, because of the sent newline of client
      if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
      {
         size -= 2;
      }
      else if (buffer[size - 1] == '\n')
      {
         --size;
      }

      buffer[size] = '\0';

      printf("Message received: %s\n", buffer); // ignore error

      if(strcmp(buffer, "SEND")==0){
         if(processSend(*current_socket)!=-1){
            if (send(*current_socket, "OK", 3, 0) == -1)
               {
                  perror("send answer failed");
                  return NULL;
               }
         }
      }
      else if(strcmp(buffer, "LIST")==0){
          if (send(*current_socket, "OK", 3, 0) == -1)
               {
                  perror("send answer failed");
                  return NULL;
               }
      }
      else if(strcmp(buffer, "READ")==0){
          if (send(*current_socket, "OK", 3, 0) == -1)
               {
                  perror("send answer failed");
                  return NULL;
               }
      }
      else if(strcmp(buffer, "DEL")==0){
          if (send(*current_socket, "OK", 3, 0) == -1)
               {
                  perror("send answer failed");
                  return NULL;
               }
      }
      else {
          if (send(*current_socket, "ERR", 3, 0) == -1)
               {
                  perror("send answer failed");
                  return NULL;
               }
      }

      

   } while (strcmp(buffer, "QUIT") != 0 && !abortRequested);

   // closes/frees the descriptor if not already
   if (*current_socket != -1)
   {
      if (shutdown(*current_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown new_socket");
      }
      if (close(*current_socket) == -1)
      {
         perror("close new_socket");
      }
      *current_socket = -1;
   }

   return NULL;
}

void signalHandler(int sig)
{
   if (sig == SIGINT)
   {
      printf("abort Requested... "); // ignore error
      abortRequested = 1;
      /////////////////////////////////////////////////////////////////////////
      // With shutdown() one can initiate normal TCP close sequence ignoring
      // the reference count.
      // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
      // https://linux.die.net/man/3/shutdown
      if (new_socket != -1)
      {
         if (shutdown(new_socket, SHUT_RDWR) == -1)
         {
            perror("shutdown new_socket");
         }
         if (close(new_socket) == -1)
         {
            perror("close new_socket");
         }
         new_socket = -1;
      }

      if (create_socket != -1)
      {
         if (shutdown(create_socket, SHUT_RDWR) == -1)
         {
            perror("shutdown create_socket");
         }
         if (close(create_socket) == -1)
         {
            perror("close create_socket");
         }
         create_socket = -1;
      }
   }
   else
   {
      exit(sig);
   }
}

int processSend(int client_socket){
   ifstream file;
   
   char buffer[BUF];
   string sender, receiver, subject, message;

   //Get sender of message
   recv(client_socket, buffer, sizeof(buffer), 0);
   sender = buffer;
   memset(buffer, 0, BUF);

   //Get receiver of message
   recv(client_socket, buffer, sizeof(buffer), 0);
   receiver = buffer;
   memset(buffer, 0, BUF);

   //Get subject of message
   recv(client_socket, buffer, sizeof(buffer), 0);
   subject = buffer;
   memset(buffer, 0, BUF);

   //Get message
   while(true){
      recv(client_socket, buffer, sizeof(buffer), 0);
      if(buffer[0]=='.'){
         break;
      }

      message += buffer+'\n';
      memset(buffer, 0, BUF);
   }

   if(writeUserFile(receiver, sender, subject, message)==-1){
      return -1;
   }

   cout << "Message from: "+ sender + " to: "+ receiver + "\nSubject: "+subject+"\n Message: "+message<< endl;
   return 1;    
}

int createMailSpool(string dirName){
    // Path to the directory
    string dir = "./"+dirName;
    // Structure which would store the metadata
    struct stat sb;

   //only create mail spool directory with name if it doesnt exist yet
   if(stat(dir.c_str(), &sb) != 0){
      if (mkdir(dirName.c_str(), 0777) != 0) { 
         return -1; 
      }
   }
   
  return 0;
}

int writeUserFile(string username, string sender, string subject, string message){
   string filename = username;
   string filepath = "./"+mailSpool+"/"+username;
   ofstream file;
   file.open(filepath, ios::app);
   if(!file){
      return -1;
   }

   file << sender+"\n";
   file << subject+"\n";
   file << message+"\n";
   file << "END\n";
   file.close();
   return 0;
}

/* TODO:
--> Folder die schon existiert nicht createn
--> File erstellen (nach user benannt) und darin content von SEND schreiben
--> File in Folder speichern
*/
