#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <termios.h> // To use 'termios' and 'tcsetattr'
#include <ldap.h>
using namespace std;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////
int sendCommand(int socket);
int listCommand(int socket);
int readCommand(int socket);
int delCommand(int socket);
int specificMessage(int socket);
int loginCommand(int socket);

int main(int argc, char **argv)
{
   int create_socket;
   char buffer[BUF];
   struct sockaddr_in address;
   int size;
   int isQuit = 0;

   if(argc != 3){
      cerr << "Usage: ./twmailer-client <ip> <port>";
      return EXIT_FAILURE;
   }

   std::istringstream iss(argv[2]);
   int port;
   if(!(iss >> port)){
      cerr << "Invalid port - not a number";
      return EXIT_FAILURE;
   }
   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as server)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address)); // init storage with 0
   address.sin_family = AF_INET;         // IPv4
   // https://man7.org/linux/man-pages/man3/htons.3.html
   address.sin_port = htons(port);
   inet_aton(argv[1], &address.sin_addr); //127.0.0.1 for now
   // https://man7.org/linux/man-pages/man3/inet_aton.3.html

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A CONNECTION
   // https://man7.org/linux/man-pages/man2/connect.2.html
   if (connect(create_socket,
               (struct sockaddr *)&address,
               sizeof(address)) == -1)
   {
      // https://man7.org/linux/man-pages/man3/perror.3.html
      perror("Connect error - no server available");
      return EXIT_FAILURE;
   }

   // ignore return value of printf
   printf("Connection with server (%s) established\n",
          inet_ntoa(address.sin_addr));

   ////////////////////////////////////////////////////////////////////////////
   // RECEIVE DATA
   // https://man7.org/linux/man-pages/man2/recv.2.html
   size = recv(create_socket, buffer, BUF - 1, 0);
   if (size == -1)
   {
      perror("recv error");
   }
   else if (size == 0)
   {
      printf("Server closed remote socket\n"); // ignore error
   }
   else
   {
      buffer[size] = '\0';
      printf("%s", buffer); // ignore error
   }
   memset(buffer, 0, BUF);

   do
   {
      printf(">> ");
      string command;
      getline(cin, command);
      if(command=="SEND"){
        if(sendCommand(create_socket) == -1){
            continue;
        }
      }
      else if(command=="LIST"){
         if(listCommand(create_socket) == -1){
            continue;
        }
      }
      else if(command=="READ"){
         if(readCommand(create_socket) == -1){
            continue;
        }
      }
      else if(command=="DEL"){
         if(delCommand(create_socket) == -1){
            continue;
        }
      }
      else if(command=="LOGIN"){
         if(loginCommand(create_socket) ==-1){
            continue;
         }
      }
      else if(command=="QUIT"){
         isQuit = 1;
         if ((send(create_socket, "QUIT", 4, 0)) == -1) 
            {
               perror("send error");
               return -1;
            }
      } else {
         cout << "No valid command!" << endl;
         continue;
      }
         if(!isQuit){

            while(true){
               size = recv(create_socket, buffer, BUF - 1, 0);

            if (size == -1)
            {
               perror("recv error");
               break;
            }
            else if (size == 0)
            {
               printf("Server closed remote socket\n"); // ignore error
               break;
            }
            else
            {
               buffer[size] = '\0';
               printf("%s\n", buffer);

               //Buffer doesnt receive line by line: check if OK or ERR is contained anywhere in there.
               char *output = NULL;
               output = strstr (buffer,"<< OK");
               if(output) {
                  memset(buffer, 0, BUF);
                  break;
               }
               output = strstr (buffer,"<< ERR");
               if(output) {
                  memset(buffer, 0, BUF);
                  break;
               }
               output = strstr (buffer, "<< LOGIN FIRST");
               if(output) {
                  memset(buffer, 0, BUF);
                  break;
               }
            }
            }
         }
   } while (!isQuit);

   ////////////////////////////////////////////////////////////////////////////
   // CLOSES THE DESCRIPTOR
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         // invalid in case the server is gone already
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

// -----------------------Functions for user input and sending-------------------------------
int sendCommand(int socket){
   if ((send(socket, "SEND", 4, 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   
   string receiver;
   cout << "Receiver: ";
   getline(cin, receiver);
   if ((send(socket, receiver.c_str(), receiver.size(), 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   
   string subject;
   cout << "Subject(max. 80 chars): ";
   getline(cin, subject);
   if(subject.size()>80){
      perror("Maximum size of 80 characters for subject!\n");
      return -1;
   }
   if ((send(socket, subject.c_str(), subject.size(), 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   
   string message;
    std::cout << "Message (end with '.' on new line.):\n";
    while (true) {
        getline(cin, message);
        if ((send(socket, message.c_str(), message.size(), 0)) == -1) 
         {
            perror("send error");
            return -1;
         }
         if (message == ".")
            break;
    }
   
   return 1;
}

int listCommand(int socket){
   if ((send(socket, "LIST", 4, 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   
   return 1;
}

int readCommand(int socket){
   if ((send(socket, "READ", 4, 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   
   if(specificMessage(socket)==-1){
      return -1;
   }
   
   return 1;
}

int delCommand(int socket){
   if ((send(socket, "DEL", 4, 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   
   if(specificMessage(socket)==-1){
      return -1;
   }
   
   return 1;
}

int specificMessage(int socket){
   string msgNumber;
   cout << "Number of message: ";
   getline(cin, msgNumber);
   if ((send(socket, msgNumber.c_str(), msgNumber.size(), 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   
   return 1;
}

int loginCommand(int socket){
   if ((send(socket, "LOGIN", 6, 0)) == -1) 
      {
         perror("send error");
         return -1;
      }

   string username;
   string password;

   cout << "User ID: ";
   getline(cin, username);
   if ((send(socket, username.c_str(), username.size(), 0)) == -1) 
      {
         perror("send error");
         return -1;
      }

   /*Hide user input*/
   termios oldt;
   tcgetattr(STDIN_FILENO, &oldt);
   termios newt = oldt;
   newt.c_lflag &= ~ECHO;
   tcsetattr(STDIN_FILENO, TCSANOW, &newt); // Hides
   cout << "User password (hidden): "<<endl;
   getline(cin, password);
   if ((send(socket, password.c_str(), password.size(), 0)) == -1) 
      {
         perror("send error");
         return -1;
      }
   tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // return to display
   
   return 1;
}

// ./twmailer-client 127.0.0.1 1
