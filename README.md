# ProgettoRetiInformatiche
Project for the Computer Networks course taught in the Bachelor's Degree in Computer Engineering at the University of Pisa, during the Academic Year 2021/2022.


# What you should know before running the program

Registered users:
Username ----- Password
user1 | psw1
user2 | psw2
User to register: user3

SIGNUP:

It is possible to register other users, but they will not be able to communicate with anyone else as the "address book" has been implemented.
As stated in the documentation, the first parameter taken from stdin for the signup operation is the server's listening port.
Possible bugs:

It is possible (in rare cases) that the device, in response to the LOGIN or SIGNUP operations, starts printing the startup menu infinitely. In this case, it is necessary to terminate the device and restart it.

Most data structures are arrays: due to time constraints, index checks have not been implemented, assuming that the chosen sizes are more than sufficient to hold the data. However, if this is not the case, this choice could lead to errors.

The file "FileToShare" is the file used to test file sharing.
The directory "DownloadedFiles" is the directory where files received from a peer are stored.
