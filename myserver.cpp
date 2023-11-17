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
#include <chrono>
#include <thread>
#define LDAP_DEPRECATED 1
#include <ldap.h>

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

const char *LDAP_SERVER = "ldap://ldap.technikum-wien.at:389";
const char *LDAP_SEARCH_BASE = "ou=people,dc=technikum-wien,dc=at";
const int MAX_LOGIN_ATTEMPTS = 3;
const int BLACKLIST_DURATION = 60;
int loginAttempts = 0;
int connectLdap(int client_socket);
int isLoggedIn = 0;
string currentUser = "";

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data);
void signalHandler(int sig);
int processSend(int client_socket);
int createMailSpool(string dirName);
int writeUserFile(string username, string sender, string subject, string message);
int processList(int client_socket);
int processRead(int client_socket);
int processDel(int client_socket);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
   socklen_t addrlen;
   struct sockaddr_in address, cliaddress;
   int reuseValue = 1;

   if(argc != 3){
      argv = argv;
      cerr << "Usage: ./twmailer-server <port> <mail-spool-directoryname>";
      return EXIT_FAILURE;
   }

   //get port from console argument: convert to int
   std::istringstream iss(argv[1]);
   int port;
   if(!(iss >> port)){
      cerr << "Invalid port - not a number";
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
   strcpy(buffer, "Welcome to myserver!\r\n Login first, with LOGIN!\n");
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

      cout << "Log in attempts: "<<to_string(loginAttempts)<<endl;
      //Not logged in: can't access
      if(!isLoggedIn){
         //Client must login first
            if(strcmp(buffer, "LOGIN")==0){
               if(connectLdap(*current_socket)==-1){
                     if(send(*current_socket, "<< ERR", 7, 0) == -1)
                     {
                        perror("send answer failed");
                        return NULL;
                     }
               } else {
                  isLoggedIn = 1;
                  // SEND login message
                  string welcome = "You are now logged in as "+currentUser+"!\r\n Please enter a command SEND, LIST, READ, DEL, QUIT\n";
                  if (send(*current_socket, welcome.c_str(), strlen(welcome.c_str()), 0) == -1)
                     {
                        perror("send failed");
                        return NULL;
                     }
                  if (send(*current_socket, "<< OK", 6, 0) == -1)
                        {
                           perror("send answer failed");
                           return NULL;
                        }
               }
               
            } else {
               //Must login first message
               if (send(*current_socket, "<< LOGIN FIRST", 15, 0) == -1)
                     {
                        perror("send answer failed");
                        return NULL;
                     }
            }
      //Logged in: access to commands
      } else {
            if(strcmp(buffer, "SEND")==0){
               if(processSend(*current_socket)!=-1){
                  if (send(*current_socket, "<< OK", 6, 0) == -1)
                     {
                        perror("send answer failed");
                        return NULL;
                     }
            } else {
               //send error if it didnt work
               if (send(*current_socket, "<< ERR", 7, 0) == -1)
                  {
                     perror("send answer failed");
                     return NULL;
                  }
            }
         }
         else if (strcmp(buffer, "LIST") == 0) {
            if(processList(*current_socket)!=-1){
               if (send(*current_socket, "<< OK", 6, 0) == -1)
                  {
                     perror("send answer failed");
                     return NULL;
                  }
            }  else {
               //send error if it didnt work
               if (send(*current_socket, "<< ERR", 7, 0) == -1)
                  {
                     perror("send answer failed");
                     return NULL;
                  }
            }
         }
         else if(strcmp(buffer, "READ")==0){
            if(processRead(*current_socket)!=-1){
               if (send(*current_socket, "<< OK", 6, 0) == -1)
                  {
                     perror("send answer failed");
                     return NULL;
                  }
            } else {
               //send error if it didnt work
               if (send(*current_socket, "<< ERR", 7, 0) == -1)
                  {
                     perror("send answer failed");
                     return NULL;
                  }
            }
         }
         else if (strcmp(buffer, "DEL") == 0) {
            if (processDel(*current_socket) != -1) {
               if (send(*current_socket, "<< OK", 6, 0) == -1) {
                     perror("send answer failed");
                     return NULL;
               }
            } else {
               // Send an error if the delete didn't work
               if (send(*current_socket, "<< ERR", 7, 0) == -1) {
                     perror("send answer failed");
                     return NULL;
               }
            }
         }
         else if(strcmp(buffer, "QUIT")==0){
            cout << "Client is quitting" <<endl;
            abortRequested = 1;//handled in signalHandler
            currentUser = "";
            isLoggedIn = 0;
            loginAttempts = 0;
            
         }
         else if(strcmp(buffer, "LOGIN")==0){ //already logged In!
            if (send(*current_socket, "<< ERR", 7, 0) == -1) {
                     perror("send answer failed");
                     return NULL;
               }
         }
         
      }
      

   } while (!abortRequested);

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
   string receiver, subject, message;
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

      message += buffer;
      message +="\n";
      memset(buffer, 0, BUF);
   }

   if(writeUserFile(receiver, currentUser, subject, message)==-1){
      return -1;
   }

   cout << "Message from: "+ currentUser + " to: "+ receiver + "\nSubject: "+subject+"\n Message: "+message<< endl;
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
   cout << "FILENAME IS-->"<<filepath<<endl;
   ofstream file;
   file.open(filepath, ios::app);
   if(!file){
      return -1;
   }

   file << "\n////////////\n";
   file << sender+"\n";
   file << subject+"\n";
   file << message+"\n";
   file.close();
   return 0;
}


// ./twmailer-server 1234 Users
// ./twmailer-client 127.0.0.1 1234 port kann alles sein muss einfach nur matchen

int processList(int client_socket) {
   printf("Listing messages for user: %s\n", currentUser.c_str());

   // Open the user's file and read messages
   string userFilename = "./" + mailSpool + "/" + currentUser;
   ifstream userFile(userFilename.c_str());

   if (userFile.is_open()) {
      string line;
      int messageNumber = 0; // Track the message number
      while (getline(userFile, line)) {

         if (line == "////////////") {
               string sender, subject, message;
               getline(userFile, sender);
               getline(userFile, subject);

               string messageLine;
               while (getline(userFile, messageLine)) {
                  if (messageLine.empty()) {
                     break;
                  }
                  message += messageLine + "\n";
               }

               // Send message number and subject to the client
               messageNumber++; // Increment the message number
               send(client_socket, to_string(messageNumber).c_str(), to_string(messageNumber).size(), 0);
               send(client_socket, ". Subject: ", 11, 0); // Add a period to distinguish the subject
               send(client_socket, subject.c_str(), subject.size(), 0);
               send(client_socket, "\n", 1, 0); // Add a newline
         }
      }
      userFile.close();
   } else {
      printf("User file not found for user: %s\n", currentUser.c_str());
      return -1;
   }
   return 0;
}

int processRead(int client_socket){
   char buffer[BUF];
   string messageNr;

   //Get number of message
   recv(client_socket, buffer, sizeof(buffer), 0);
   messageNr = buffer;
   memset(buffer, 0, BUF);

   //Check if number of message is in fact an int
   char* p;
   long converted = strtol(messageNr.c_str(), &p, 10);
   if (*p) {
      return -1;
   }
   int messageToFind = converted;
   //Open file of user
   string userFilename = "./" + mailSpool + "/" + currentUser;
   cout << "Trying to find: " << userFilename << endl;
   ifstream userFile(userFilename.c_str());
   if (userFile.is_open()) {
      //find specific message
      int messageNumber = 1;
      string line;
      while(getline(userFile, line)){
         //Message found
         if(line=="////////////"&&messageNumber==messageToFind){
            cout << "FOUND MESSAGE NUMBER "<<messageToFind << endl;
            string sender, subject, message;
               getline(userFile, sender);
               getline(userFile, subject);

               string messageLine;
               while (getline(userFile, messageLine)) {
                  if (messageLine.empty()) {
                     break;
                  }
                  message += messageLine + "\n";
               }
               //Send the specific message to client
               send(client_socket, sender.c_str(), sender.size(), 0);
               send(client_socket, "\n", 1, 0);
               send(client_socket, subject.c_str(), subject.size(), 0);
               send(client_socket, "\n", 1, 0);
               send(client_socket, message.c_str(), message.size(), 0);
               return 0;

         } else if(line=="////////////") {
            messageNumber++;
         }
      }
      //if message was not found
      string errMsg = "Message nr. "+to_string(messageToFind)+" doesn't exist!\n";
      send(client_socket, errMsg.c_str(), errMsg.size(), 0);
      return -1;
      userFile.close();
   } else {
      printf("User file not found for user: %s\n", currentUser.c_str());
      return -1;  
   }

   return 0;
}

int processDel(int client_socket) {
   char buffer[BUF];
   string messageNr;

   // Get number of message
   recv(client_socket, buffer, sizeof(buffer), 0);
   messageNr = buffer;
   memset(buffer, 0, BUF);

   // Checks if number of message is in fact an int
   char *p;
   long converted = strtol(messageNr.c_str(), &p, 10);
   if (*p) {
      return -1;
   }
   int messageToDelete = converted;
   // Opens file of user
   string userFilename = "./" + mailSpool + "/" + currentUser;
   cout << "Trying to find: " << userFilename << endl;
   ifstream userFile(userFilename.c_str());
   if (userFile.is_open()) {
      // Creates a temporary file to rewrite the user's file
      string tempFilename = "./" + mailSpool + "/temp_" + currentUser;
      ofstream tempFile(tempFilename.c_str());

      if (tempFile.is_open()) {
         // Copys lines from the original file to the temporary file
         int messageNumber = 1;
         string line;
         bool inMessage = false;

         while (getline(userFile, line)) {
               if (line == "////////////") {
                  inMessage = true;
                  if (messageNumber == messageToDelete) {
                     // Skips this message if it matches the one to be deleted
                     while (getline(userFile, line) && !line.empty()) {
                           // Skips the entire message
                     }
                     messageNumber++;
                     continue;
                  }
               }

               if (inMessage) {
                  if (line.empty()) {
                     inMessage = false;
                     messageNumber++;
                  }
               }

               tempFile << line << "\n";
         }

         // Closes both files
         userFile.close();
         tempFile.close();

         // Removes the original file and rename the temporary file
         if (remove(userFilename.c_str()) == 0) {
               if (rename(tempFilename.c_str(), userFilename.c_str()) == 0) {
                  string successMsg = "Message " + messageNr + " deleted successfully.\n";
                  send(client_socket, successMsg.c_str(), successMsg.size(), 0);
                  send(client_socket, "<< OK", 6, 0);
                  return 0;
               }
         }
      }
   } else {
      printf("User file not found for user: %s\n", currentUser.c_str());
      return -1;
   }

   return -1;
}

int connectLdap(int client_socket){
   LDAP *ld;
   int ldapVersion = LDAP_VERSION3;
   int rc = 0;

   char buffer[BUF];
   string username, password;
   
   memset(buffer, 0, BUF);
   // Gets username
   recv(client_socket, buffer, sizeof(buffer), 0);
   username = buffer;
   memset(buffer, 0, BUF);
   // Get number of message
   recv(client_socket, buffer, sizeof(buffer), 0);
   password = buffer;

   //initialize connection to LDAP server
      rc = ldap_initialize(&ld, LDAP_SERVER);
      if (rc != LDAP_SUCCESS)
      {
         fprintf(stderr, "ldap_init failed\n");
         return EXIT_FAILURE;
      }
      printf("connected to LDAP server %s\n", LDAP_SERVER);

      //  Set the version to 3.0 (default is 2.0).
   int returnCode = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &ldapVersion);
   if(returnCode == LDAP_SUCCESS)
      printf("ldap_set_option succeeded - version set to 3\n");
   else
   {
      printf("SetOption Error:%0X\n", returnCode);
   }

   //start a secure connection
   rc = ldap_start_tls_s(ld,NULL,NULL);
   if (rc != LDAP_SUCCESS)
   {
      fprintf(stderr, "ldap_start_tls_s(): %s\n", ldap_err2string(rc));
      ldap_unbind_ext_s(ld, NULL, NULL);
      return -1;
   }

   //username mit search base verbinden, spezifischer distinguished name!
   char dn[256];
   sprintf(dn, "uid=%s,%s", username.c_str(), LDAP_SEARCH_BASE);
   cout << dn << endl;

   if(ldap_simple_bind_s(ld, dn, password.c_str())!=LDAP_SUCCESS){
      loginAttempts+=1;
      cerr << "Authentication failed" << endl;
      if(loginAttempts==MAX_LOGIN_ATTEMPTS){
         cout << "3rd login attempt!"<< endl;
            strcpy(buffer, "Too many login attempts! You are blacklisted for 1 Minute\n");
            if (send(client_socket, buffer, strlen(buffer), 0) == -1)
            {
               perror("send failed");
               return -1;
            }
            std::this_thread::sleep_for(std::chrono::minutes(1));
            loginAttempts = 0;
         return -1;
      }
      ldap_unbind_ext_s(ld, NULL, NULL);
      return -1;
   }

   cout << "LDAP bind as "<< username << " was successful!"<<endl;
   currentUser = username;
   ldap_unbind_ext_s(ld, NULL, NULL);
   return 0;
}
