#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <map>
#include <pthread.h>
#include <mutex>
#include <iostream>
#include <ctime>

using namespace std;

#define SERVERPORT "8888" // the port users will be connecting to

#define BACKLOG 10 // how many pending connections queue will hold

#define MAXTHREADS 3

#define MAXDATASIZE 1024

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_RESET "\x1b[0m"

pthread_t threads[MAXTHREADS]; //thread[0] for ping, thread[1] for peer send, thread[2] for peer receive
mutex mtx;
bool pingAlive, sendAlive, rcvAlive; //to check running status of the 3 threads.
string global_UserName; // keep user name here.


void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void sigchld_handler(int s)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

void *sendPing(void *fd)
{
    int rv;
    int sockfd = (int)fd;

    char buf[5];
    while (1)
    {
        pingAlive = true;
        if (send(sockfd, "PING", 5, 0) < 0)
        {
            fprintf(stderr, "Error in sending PING to server\n");
        }
        memset(buf, '\0', 5);
        if ((rv = recv(sockfd, buf, 5, 0)) > 0)
        {
            if (strcmp(buf, "ACK"))
                cout << "Ping ACKed\n";
        }
        else
        {
            if (sendAlive || rcvAlive)
            {
                cout << "Server Down! Application will exit after current chat is over...\n";
                close(sockfd);
                pingAlive = false;
                pthread_exit(NULL);
            }
            else
            {
                cout << "Server Down! Application will exit" << endl;
                exit(1);
            }
        }
        usleep(10000000); //ping every 10 sec
    }
}

bool getOnlineClients(int sockfd)
{
    int rv;
    if (send(sockfd, "LIST", 5, 0) < 0)
    {
        fprintf(stderr, "Error in sending LIST to server\n");
    }

    char buf[MAXDATASIZE];
    memset(buf, '\0', MAXDATASIZE);
    if ((rv = recv(sockfd, buf, MAXDATASIZE - 1, 0)) > 0)
    {
        cout << "--------------------------------\n";
        cout << "Online Users :\n";
        cout << "UserName\tIP\tPort\n";
        cout << string(buf);
        cout << "--------------------------------\n";
    }
    else
    {
        cout << "Server Down! Application will exit after current chat is over...\n";
        close(sockfd);
        return false;
    }
    return true;
}

void *SendMessage(void *fd)
{
    int socket = (int)fd;

    int msgNo = 1;
    while (1)
    {
        if (!rcvAlive) //if rcv thread is dead
        {
            cout << "Connection to peer closed." << endl;
            close(socket); //consider this. this should not be done.

            sendAlive = false;
            pthread_exit(NULL);
        }

        sendAlive = true;
        string msg;
        string buf;

        // cout<<"ME#"<<msgNo++<<":";
        // msgNo++;
        // fgets(msg, MAXDATASIZE-3, stdin);
        getline(cin, msg);
        // cout<<endl<<msg<<endl;
        if (msg == "/exit") //client wishes to close connection
        {
            cout << "Received exit msg from user. Application will exit.\n";
            send(socket, (char *)(msg.c_str()), size_t(msg.size()), 0);
            exit(1);
        }

        // if (msg == "/end")
        // {
        //     cout << "Terminate this chat. Select a new peer to chat.\n";
        //     send(socket, (char *)(msg.c_str()), size_t(msg.size()), 0);
        //     sendAlive = false;

        //     pthread_exit(NULL);
        //     return;
        // }




        buf = global_UserName + " : ";
        buf += msg;

        if (send(socket, (char *)(buf.c_str()), size_t(buf.size()), 0) < 0)
        {
            fprintf(stderr, "Error in sending msg to peer\n");
            close(socket); //consider this. this should not be done.

            sendAlive = false;
            pthread_exit(NULL);
        }
    }
}

void *ReceiveMessage(void *fd)
{
    int socket = (int)fd, rv;
    while (1)
    {
        if (!sendAlive) //if send thread is dead
        {
            cout << "Connection to peer closed." << endl;
            close(socket); //consider this. this should not be done.

            rcvAlive = false;
            pthread_exit(NULL);
        }

        rcvAlive = true;
        char msg[MAXDATASIZE];
        string buf;
        memset(msg, '\0', MAXDATASIZE);
        if ((rv = recv(socket, msg, MAXDATASIZE, 0)) > 0)
        {
            string sMsg = string(msg);
            if (sMsg == "/exit")
            {
                cout << "Connection closed by peer. Application will exit" << endl;
                //we should go back to server loop instead of exit(1)
                exit(1);
            }
            if (sMsg.substr(0, 3) == "ACK") //ACK for msg received
            {
                //receive ACK ,
                // we dont need to act 

            }

            else
            {
                string ack = "ACK"; 

                buf = sMsg + "\n";

                //sending ACK for the above message.
                if (send(socket, (char *)(ack.c_str()), size_t(ack.size()), 0) < 0)
                {
                    fprintf(stderr, "Error in sending ACK to peer\n");
                    cout << "Connection to peer closed." << endl;
                    close(socket); //consider this. this should not be done.

                    rcvAlive = false;
                    pthread_exit(NULL);
                }
            }
        }
        else if (rv < 0)
        {
            perror("rcvsdfsdf");
            exit(1);
        }
        else //rv==0
        {
            cout << "Connection closed by peer. Chat will exit.\n";
            exit(1);
            // close(socket);		//think about this. you can't close socket.

        }
        cout << buf << endl;
    }
}

int main(int argc, char const *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    int sockfd, numbytes, yes = 1, rc;
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];
    string peerIP;
    struct sigaction sa;
    socklen_t sin_size;

    cout << "Please provide server ip: ";
    string sServerAddress = "";
    cin >> sServerAddress;

    // string sUserName = "";
    string aPortNumber = "";

    cout << "Enter your name: ";
    cin >> global_UserName;

    cout << "Define the port others client can talk to you: ";
    cin >> aPortNumber;

    cout << "\nConnecting to : " << sServerAddress << " with port: " << SERVERPORT << endl;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0)
    if ((rv = getaddrinfo(sServerAddress.c_str(), SERVERPORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("client: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
              s, sizeof s);
    printf("client: connecting to server at %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    char serverMsg[5];
    memset(serverMsg, '\0', 5);

    //recv CONN or DISC code from server
    if ((rv = recv(sockfd, serverMsg, 5, 0)) <= 0)
    {
        cout << "Code Receive from server failed. Application will exit..." << endl;
        exit(1);
    }

    if (strcmp(serverMsg, "DISC") == 0) //DISC code received. thread overflow at server
    {
        cout << "Thread overflow at server. Application will exit..." << endl;
        exit(1);
    }

    if (strcmp(serverMsg, "CONN") == 0)
    {
        cout << "Successfully connected to server" << endl;

        string user_string;
        user_string += global_UserName;
        user_string += " >=< ";
        user_string += aPortNumber;
        uint16_t send_value = send(sockfd, user_string.c_str(), user_string.size(), 0);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;                // make it safe to return from a handler
    if (sigaction(SIGCHLD, &sa, NULL) == -1) // clean up
    {
        perror("sigaction");
        exit(1);
    }

    if ((rc = pthread_create(&threads[0], NULL, sendPing, (void *)sockfd)) != 0)
    {
        fprintf(stderr, "Error:unable to create thread, %d\n", rc);
        return 1;
    }
    /*------- creating socket for chat and binding it to port. Will be used both for connect and listen-----*/

    int clientSocket, new_fd;
    struct addrinfo *clientInfo;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, aPortNumber.c_str(), &hints, &clientInfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for (p = clientInfo; p != NULL; p = p->ai_next)
    {
        if ((clientSocket = socket(p->ai_family, p->ai_socktype,
                                   p->ai_protocol)) == -1)
        {
            perror("client: socket");
            continue;
        }

        if (setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if (bind(clientSocket, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(clientSocket);
            perror("server: bind");
            continue;
        }

        break;
    }
    if (p == NULL)
    {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    freeaddrinfo(clientInfo); // all done with this structure

    if (listen(clientSocket, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;                // make it safe to return from a handler
    if (sigaction(SIGCHLD, &sa, NULL) == -1) // clean up
    {
        perror("sigaction");
        exit(1);
    }

    //creating sets of file descriptors to be used for select command to poll them for activity
    fd_set readfds;
    FD_ZERO(&readfds); //clears the new set

    while (1)
    {

        FD_SET(clientSocket, &readfds); //used for adding given fd to a set
        FD_SET(0, &readfds);

        int choice;
        cout << "\n-------------------------------------------------" << endl;
        cout << "Select your action , follow by (return) :" << endl;
        cout << "[ 1 ] Get Online Clients List." << endl;
        cout << "[ 2 ] Connect to a peer (require peer's IP address)." << endl;
        cout << "[ 3 ] Exit ." << endl;
        cout << "-------------------------------------------------" << endl;

        select(clientSocket + 1, &readfds, NULL, NULL, NULL);

        // Someone contact you
        if (FD_ISSET(clientSocket, &readfds))
        {
            struct sockaddr_storage their_addr; // connector's address information
            socklen_t socket_size = sizeof(their_addr);
            new_fd = accept(clientSocket, (struct sockaddr *)&their_addr, &socket_size);
            if (new_fd == -1)
            {
                perror("accept");
                continue;
            }

            inet_ntop(their_addr.ss_family,
                      get_in_addr((struct sockaddr *)&their_addr),
                      s, sizeof s);
            printf("got connection from %s\n", s);
            string peerIP;
            peerIP = s;
            string ans;
            cout << "Do you want to chat with " << peerIP << "?[Y/n]:";
            cin >> ans;

            if (ans == "y" || ans == "" || ans == "Y")
            {
                ans = "y";
                send(new_fd, (char *)(ans.c_str()), 1, 0);
            }
            else
            {
                send(new_fd, (char *)(ans.c_str()), 1, 0);
                close(new_fd);
                continue;
            }
            cout << "Connected successfully to peer. You may now start chatting\n\n\n";

            //create threads for chat send and chat rcv.
            getchar();
            rcvAlive = true, sendAlive = true;
            if (pthread_create(&threads[1], NULL, SendMessage, (void *)new_fd) != 0) //for send
            {
                cout << "Failed to create new thread to send message. Connection to peer will be closed ";
                close(new_fd);
                rcvAlive = false, sendAlive = false;
                continue;
            }
            if (pthread_create(&threads[2], NULL, ReceiveMessage, (void *)new_fd) != 0) // for receive message
            {
                cout << "Failed to create new thread to receive message. Connection to peer will be closed ";
                rcvAlive = false, sendAlive = false;
                close(new_fd);
                continue;
            }

            //wait till the above threads die.
            while (sendAlive && rcvAlive)
                ;
        }

        //in case user press (return) key
        else if (FD_ISSET(0, &readfds))
        {
            cin >> choice;

            int peerSocket;
            // char buf[MAXDATASIZE];
            struct addrinfo *peerinfo;
            // struct sockaddr_storage their_addr; // connector's address information
            char s[INET6_ADDRSTRLEN];
            string peerIP;
            string peerPORT;

            struct sigaction sa;

            switch (choice)
            {
            case 1: //get online client list from server
                if (getOnlineClients(sockfd))
                    continue;
                else
                    exit(1);
                break;

            case 2: //connect to peer
                cout << "Enter the IP address you want to connect to: ";
                cin >> peerIP;

                cout << "Port: ";
                cin >> peerPORT;

                memset(&hints, 0, sizeof hints);
                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;

                if ((rv = getaddrinfo((char *)(peerIP.c_str()), peerPORT.c_str(), &hints, &peerinfo)) != 0)
                {
                    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
                    return 1;
                }

                // loop through all the results and connect to the first we can
                for (p = peerinfo; p != NULL; p = p->ai_next)
                {
                    if ((peerSocket = socket(p->ai_family, p->ai_socktype,
                                             p->ai_protocol)) == -1)
                    {
                        perror("client: socket");
                        continue;
                    }

                    if (setsockopt(peerSocket, SOL_SOCKET, SO_REUSEADDR, &yes,
                                   sizeof(int)) == -1)
                    {
                        perror("setsockopt");
                        exit(1);
                    }

                    if (connect(peerSocket, p->ai_addr, p->ai_addrlen) == -1)
                    {
                        close(peerSocket);
                        perror("client: connect");
                        continue;
                    }

                    break;
                }
                if (p == NULL)
                {
                    fprintf(stderr, "client: failed to connect\n");
                    return 2;
                }

                inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
                          s, sizeof s);
                printf("connecting to peer at %s\n", s);

                freeaddrinfo(peerinfo); // all done with this structure

                //checking for positive response from peer
                memset(buf, '\0', sizeof(buf));
                if ((rv = recv(peerSocket, buf, 1, 0)) > 0)
                {
                    // cout<<buf<<endl;
                    if (buf[0] == 'y')
                        cout << "Connected successfully to peer. You may now start chatting\n\n\n";
                    else
                    {
                        fprintf(stderr, "Peer denied connection request\n");
                        close(peerSocket);
                        continue;
                    }
                }
                else
                {
                    fprintf(stderr, "Peer failed to connect\n");
                    close(peerSocket);
                    continue;
                }

                //create threads for chat send and chat rcv.
                rcvAlive = true, sendAlive = true;
                getchar();
                if (pthread_create(&threads[1], NULL, SendMessage, (void *)peerSocket) != 0) //for send
                {
                    cout << "Failed to create new thread for chat. Connection to peer will be closed ";
                    close(peerSocket);
                    rcvAlive = false, sendAlive = false;
                    continue;
                }
                if (pthread_create(&threads[2], NULL, ReceiveMessage, (void *)peerSocket) != 0)
                {
                    cout << "Failed to create new thread for chat. Connection to peer will be closed ";
                    close(peerSocket);
                    rcvAlive = false, sendAlive = false;
                    continue;
                }

                //wait for both threads to complete
                while (sendAlive && rcvAlive)
                    ;

                break; //break of switch case statement
            case 3:    //exit
                return 0;
            default:
                cout << "Enter a valid choice..." << endl;
            }
        }
    }

    return 0;
}
