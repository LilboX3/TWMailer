Server Setup
Before you can use TwMailer, you need to set up the server and client components.

Server Start
To start the server, use the following command, providing a port (matching with the clients port) number and a mail spool directory name:
    ./twmailer-server <port> <mailspooldirectory>

Client Setup
Now, you can begin using TwMailer within the client application.

Client Start
To start the client, use the following command, specifying the server's IP address (127.0.0.1 for localhost) and port (matching with the servers port):
    ./twmailer-client <server-ip> <port>


Sending Messages

You can send messages to other users via the client. To do this, use the SEND command. Write your message, and when you're ready to send it to the server, add a period (.) on a new line.

Managing Messages

LIST: Use the LIST command to view all messages for a specific user. Simply input the user's name.

READ: If you want to read a particular message, use the READ command. Insert the user's name and the message number.

DELETE: To remove a message, use the DEL command. Specify the user's name and the message number to delete.

Quitting TwMailer
When you're finished using TwMailer, you can exit the client using the QUIT command.
