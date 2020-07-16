README

This is a scalable fault tolerant verison of the other Social Network Project. This implementation contains a routing server that is assumed to always be available. A client will contact the router and the router will send the client an IP address of an available server using an election algorithm. Each master server has a slave server that will be used to restart the server if it goes down.

Instructions to run program

First start the router server
1. Launch a machine
2. run router script with ./router_script (may need to update permissions -> chmod +x router_script)
3. Enter the ip address of the machine (i.e. 10.0.2.4)
4. Enter the port number you wish to use (i.e. 9876)

Second start master servers
1. Launch a machine
2. run server script with ./server_script (may need to update permissions -> chmod +x server_script)
3. Enter the ip address of the machine (i.e. 10.0.2.5)
4. Enter the port number you wish to use (i.e. 7890)
5. Enter the routing ip and port together in one string (i.e. 10.0.2.4:9876)
6. Repeat for up to 3 master server machines

Lastly start client machine
1. Launch a machine
2. run the client script with ./client_script (may need to update permsissions -> chmod +x client_script)
3. Enter the ip address of the router script

Notes and To fixes:
1. Sometimes the when reconnecting the client will fail to display the command prompt put commands can still go through
2. On client launch, the command prompt will display "Invalid Command" on the first line without a command being entered
