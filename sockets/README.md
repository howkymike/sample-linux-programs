#  TCP server implementation

### Short descryption
Server sends current time to client.

### Descryption
- server
	- creates new child process for each client
	- main process communicate with chlidren using AF_UNIX domain sockets
	- child processes send time to client



#### Credits
© 2021 howkymike,
based on "The Linux Programming Interface" and Linux lectures at AGH

