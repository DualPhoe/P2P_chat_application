# P2P_chat_application

HOW TO COMPILE:

	client:  g++ -std=c++11 -fpermissive -w client.cpp -lpthread -o client
	server: g++ -std=c++11 server.cpp -lpthread -o server



TO RUN: (please let the server run first)

	server: ./server
	client: ./client


REQUIREMENTS:

	Server should run first.
	Clients must not in the same machine(Local IP address), however there can be one client run on the same machine with server
	Client/user will provide server's IP at startup, with user name and a (available) port on client machine (the client app will use this port to listen to peer connections).



Features: 

	Client can get a list of Users
	Client can connect with a User, by providing their IP and port.
