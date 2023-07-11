# Instant Messaging Application in C
Project for the Computer Networks course taught in the Bachelor's Degree in Computer Engineering at the University of Pisa, during the Academic Year 2021/2022. <br>

This project aimed to implement an instant messaging application in C-89. The application includes the following features:

- A server responsible for user registration and handling pending messages.
- File exchange functionality.
- Group chat capability.
- Status indication to show if contacts in the address book are online or offline.
- Message delivery status, indicating if the message has been viewed or only delivered.

## Running the Application

To run the application, execute the `exec2022.sh` file. This script will start the necessary components of the application and set up the environment for communication.

## Documentation

All implementation choices and details are explained in Italian within the `Documentazione.pdf` file. Please refer to this document for a comprehensive explanation of the project, including design decisions, data structures, and algorithms used.

Please note that the documentation is provided in <b>Italian</b> language!

# What you should know before running the program

Registered users:<br>
Username | Password<br>
user1 | psw1<br>
user2 | psw2<br>
User to register: user3<br>

SIGNUP:<br>

It is possible to register other users, but they will not be able to communicate with anyone else as the "address book" has been implemented.
As stated in the documentation, the first parameter taken from stdin for the signup operation is the server's listening port.

### Possible bugs:
It is possible (in rare cases) that the device, in response to the LOGIN or SIGNUP operations, starts printing the startup menu infinitely. In this case, it is necessary to terminate the device and restart it.<br>

Most data structures are arrays: due to time constraints, index checks have not been implemented, assuming that the chosen sizes are more than sufficient to hold the data. However, if this is not the case, this choice could lead to errors.<br>

The file "FileToShare" is the file used to test file sharing.<br>
The directory "DownloadedFiles" is the directory where files received from a peer are stored.<br>


Enjoy using it!
