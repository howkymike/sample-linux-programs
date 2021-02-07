#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <wait.h>
#include <fcntl.h>


#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                               } while (0)
#define ADDRESS_SIZE 256
#define MSG_SIZE 100
#define EPOLL_CHILD_SIZE 5  // uzywane sa tylko 2
#define EPOLL_PARENT_SIZE 2

char SERVER_ADDRESS[ADDRESS_SIZE] = "127.0.0.1";
int SERVER_PORT = 5567;
int MAX_PARALLEL_CONNECTIONS = 5;   // backlog
float MESSAGE_INTERVAL = -1;
int serverSocket;
int timerFd, childEpollFd, parentEpollFd;
int parentEpollDisabled = 0;

int *childSockets;  // circular buffer
int childIndex = 0;  // circular tail, next free child
int mainChildIndex = 0; // buffer head
int activeClients = 0;

void parseArgs(int argc, char* argv[]);
int parseInt(char *arg);
float parseFloat(char *arg);
void getPrettyCurrDate(char *timeBuff, size_t timeBuffSize, struct timespec *ts);

void setupServer();
int createChild(int clientSocket);
void handleClient(int clientSocket, int parentSocket);

void parentMain();
void acceptIncomingClient();
void handleOldestChild(int childFd);
void addParent2Epoll();
void add2ParentEpoll(int childFd);

void createMsgTimer();
void setupChildEpoll(int clientSocket);
void getTimeAndSend(int parentSocket, int clientSocket, struct timespec *ts);

void setupGrimReaper();
static void grimReaper(int sig);
void setupSIGTERM();
static void sigterm_handler (int sig);


int main(int argc, char* argv[]) {
    parseArgs(argc, argv);
    setupServer();
    setupSIGTERM();
    parentMain();

    close(serverSocket);
    free(childSockets);
    return 0;
}


void parentMain() {    // accept incoming clients AND listen for child requests

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)    // QUESTION : czy trzeba tak to zrobic?
        errExit("signal");
    setupGrimReaper();                          // QUESTION : czy trzeba handle SIGCHLD zeby zebrac dzieci ???

    parentEpollFd = epoll_create1(0);
    if (parentEpollFd < 0)
        errExit("parent epoll_create1");
    addParent2Epoll();
    struct epoll_event* events = calloc(EPOLL_PARENT_SIZE, sizeof(struct epoll_event));
    if(events == NULL)
        errExit("parent calloc");
    while(1) {
        int nReady = epoll_wait(parentEpollFd, events, EPOLL_PARENT_SIZE, -1);
        for (int i = 0; i < nReady; i++) {
            if (events[i].events & EPOLLERR)
                errExit("parent epoll_wait");
            if(events[i].data.fd == serverSocket) { // accept incoming client
                acceptIncomingClient();

            } else {    // the oldest child
                handleOldestChild(events[i].data.fd);
            }
        }
    }
}

int createChild(int clientSocket) {
    // create socket pair
    int socketPair[2];
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, socketPair) != 0)
        errExit("socketpair");

    // create child
    switch (fork()) {
        case -1:
            errExit("fork");
        case 0:  /* Child */
            close(socketPair[0]);
            close(serverSocket);
            handleClient(clientSocket, socketPair[1]);
            _exit(EXIT_SUCCESS);
        default: /* Parent */
            close(socketPair[1]);
            close(clientSocket);
            break;
    }
    return socketPair[0];
}

void acceptIncomingClient() {
    if(activeClients >= MAX_PARALLEL_CONNECTIONS) {
        printf("Connection queue is full, temporary disabling accepting new connections...\n");
        if(epoll_ctl(parentEpollFd, EPOLL_CTL_DEL, serverSocket, NULL) == -1)
            errExit("epoll_ctl DEL");
        parentEpollDisabled = 1;
        return;
    }
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    int newSocketFd = accept4(serverSocket, (struct sockaddr*)&peer_addr, &peer_addr_len, O_NONBLOCK);
    if (newSocketFd == -1)
        errExit("parent accept");
    fprintf(stderr, "client %s  connected(port %d)\n",
            inet_ntoa(peer_addr.sin_addr), peer_addr.sin_port);
    int childFd = createChild(newSocketFd);

    childSockets[childIndex] = childFd;
    childIndex = (childIndex + 1) % MAX_PARALLEL_CONNECTIONS;   //circular buffer
    if(activeClients == 0) // here, add only first client
        add2ParentEpoll(childFd);
    activeClients++;
}

void handleOldestChild(int childFd) {
    int32_t readBuff;
    if(read(childFd, &readBuff, sizeof(readBuff)) == -1)    //< sizeof(readBuff)) // jak nie przeczytamy to ciagle epoll bedzie aktywny
        errExit("read from parent handleOldestChild");

    struct timespec ts = {0};
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
        errExit("clock_gettime");
    ssize_t wRet = write(childFd, &ts, sizeof(struct timespec));  // send time to child
    if( wRet != sizeof(struct timespec)) {
        if(errno == EPIPE || errno == ECONNRESET) {
            printf("Child has exited, removing from epoll..\n");
            if(epoll_ctl(parentEpollFd, EPOLL_CTL_DEL, childFd, NULL) == -1)
                errExit("epoll_ctl DEL");
            close(childFd);
            activeClients--;
            if(parentEpollDisabled == 1) {  // resume accepting connections
                addParent2Epoll();
                parentEpollDisabled = 0;
                printf("Accepting connections again :D\n");
            }

            // add new child
            mainChildIndex = (mainChildIndex + 1) % MAX_PARALLEL_CONNECTIONS;
            if(activeClients)    // if there are awaiting children, add the oldest
                add2ParentEpoll(childSockets[mainChildIndex]);
        } else {
            errExit("write");
        }
    }
}

void addParent2Epoll() {
    struct epoll_event acceptEvent;
    acceptEvent.events = EPOLLIN;
    acceptEvent.data.fd = serverSocket;
    if (epoll_ctl(parentEpollFd, EPOLL_CTL_ADD, serverSocket, &acceptEvent) < 0)
        errExit("create parent epoll_ctl");
}
void add2ParentEpoll(int childFd) {
    struct epoll_event acceptEvent;
    acceptEvent.events = EPOLLIN;
    acceptEvent.data.fd = childFd;
    if (epoll_ctl(parentEpollFd, EPOLL_CTL_ADD, childFd, &acceptEvent) < 0 && errno != EEXIST)
        errExit("add parent epoll_ctl");
}

void handleClient(int clientSocket, int parentSocket) {
    createMsgTimer();
    setupChildEpoll(clientSocket);
    struct epoll_event* events = calloc(EPOLL_CHILD_SIZE, sizeof(struct epoll_event));
    if(events == NULL)
        errExit("calloc");

    struct timespec ts = {0};
    uint64_t childRead;
    uint64_t timerRead; // QUESTION: czmeu trzba czytac timer?

    while (1) {
        int nReady = epoll_wait(childEpollFd, events, EPOLL_CHILD_SIZE, -1);
        for (int i = 0; i < nReady; i++) {
            if (events[i].events & EPOLLERR) {
                if(errno == EAGAIN) {
                    printf("Client seems to be unavaiable. Disconnecting.\n");
                    close(clientSocket);
                    close(parentSocket);
                    exit(EXIT_SUCCESS);
                }
                errExit("epoll_wait");
            }
            if(events[i].data.fd == clientSocket) { // client send something
                if(events[i].events == EPOLLRDHUP) {
                    fprintf(stderr, "Client disconnected (EPOLLRDHUP)\n");
                    close(clientSocket);
                    close(parentSocket);
                    free(events);
                    exit(EXIT_SUCCESS);
                }   // else EPOLLIN
                int bytesRead = read(clientSocket, &childRead, sizeof(childRead));
                if(bytesRead == -1)
                    errExit("read");
                if(bytesRead == 0) {
                    fprintf(stderr, "Client disconnected (EPOLLIN)\n");
                    close(clientSocket);
                    close(parentSocket);
                    exit(EXIT_SUCCESS);
                }
                for(;;) {   // zczytujemy wszystko
                    bytesRead = read(clientSocket, &childRead, sizeof(childRead));
                    if(bytesRead <= 0)
                        break;
                }
                getTimeAndSend(parentSocket, clientSocket, &ts);
            } else {    // childTimer
                int bytesRead = read(timerFd, &timerRead, 8);
                if(bytesRead == -1)
                    errExit("read");

                getTimeAndSend(parentSocket, clientSocket, &ts);
            }
        }
    }
}

void getTimeAndSend(int parentSocket, int clientSocket, struct timespec *ts) {
    // ask parent for time
    int32_t writeBuff;
    if(write(parentSocket, &writeBuff, sizeof(writeBuff)) != sizeof(writeBuff))
        errExit("write to parent");
    if(read(parentSocket, ts, sizeof(struct timespec)) == -1)   // < sizeof(struct timespec)), moze byc 0 jak klient nagle sie rozlaczy
        errExit("getTimeAndSend read");

    // make pretty
    char msgBuff[MSG_SIZE] = {0};
    getPrettyCurrDate(msgBuff, MSG_SIZE, ts);

    // send
    if(write(clientSocket, msgBuff, MSG_SIZE) != MSG_SIZE)
        errExit("write");
}

void setupServer() {
    // 1. create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSocket == -1)
        errExit("socket");

    // 2. register address, bind
    struct sockaddr_in  A;
    A.sin_family = AF_INET;
    A.sin_port = htons(SERVER_PORT);
    int R = inet_aton(SERVER_ADDRESS, &A.sin_addr);
    if(!R)
        errExit("Invalid address");
    int optval = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))== -1)
        errExit("setsockopt");
    if(bind(serverSocket, (struct sockaddr*)&A, sizeof(A)))
        errExit("bind");

    // 3. listen (change to passive mode)
    if(listen(serverSocket, MAX_PARALLEL_CONNECTIONS))
        errExit("listen");
}


void createMsgTimer() {
    timerFd = timerfd_create(CLOCK_REALTIME, 0);
    if(timerFd == -1)
        errExit("timerfd_create");

    struct itimerspec ts={0};
    ts.it_interval.tv_sec = (int)MESSAGE_INTERVAL;
    ts.it_interval.tv_nsec = (MESSAGE_INTERVAL - (float)ts.it_interval.tv_sec) * 1000000000;
    ts.it_value.tv_sec=ts.it_interval.tv_sec;   // jak obie wartosci beda 0  to timer zostanie rozbrojony!!
    ts.it_value.tv_nsec=ts.it_interval.tv_nsec;
    if(timerfd_settime(timerFd, 0, &ts, NULL) == -1)
        errExit("timerfd_settime");
}

void setupChildEpoll(int clientSocket) {
    childEpollFd = epoll_create1(0);
    if(childEpollFd == -1)
        errExit("epoll_create");
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.fd = timerFd;
    if(epoll_ctl(childEpollFd, EPOLL_CTL_ADD, timerFd, &ev) == -1)
        errExit("epoll");
    ev.data.fd = clientSocket;
    if(epoll_ctl(childEpollFd, EPOLL_CTL_ADD, clientSocket, &ev) == -1)
        errExit("epoll");
}



void getPrettyCurrDate(char *timeBuff, size_t timeBuffSize, struct timespec *ts) {
    struct tm *timeTM = localtime(&(ts->tv_sec));
    if(timeTM == 0)
        errExit("localtime");
    if(strftime(timeBuff, timeBuffSize, "%F %T", timeTM) == 0)
        errExit("strftime");
    sprintf(timeBuff+strlen(timeBuff), ":%ld\n", (ts->tv_nsec / 1000000));
}

void parseArgs(int argc, char* argv[]) {
    if(argc > 2) {
        childSockets = (int*) calloc(5, sizeof(int));
        if(childSockets == NULL)
            perror("calloc childSockets");

        int opt;
        while ((opt = getopt(argc, argv, ":a:p:c:d:")) != -1) {
            switch (opt) {
                case 'a': {
                    strncpy(SERVER_ADDRESS, optarg, sizeof SERVER_ADDRESS);
                    break;
                }
                case 'p': {
                    SERVER_PORT = parseInt(optarg);
                    break;
                }
                case 'c': {
                    MAX_PARALLEL_CONNECTIONS = parseInt(optarg);
                    childSockets = (int*) calloc(MAX_PARALLEL_CONNECTIONS, sizeof(int));
                    if(childSockets == NULL)
                        perror("calloc childSockets");
                    break;
                }
                case 'd': {
                    MESSAGE_INTERVAL = parseFloat(optarg);
                    break;
                }
                default:    // handles two types of error (opt is equal to '?')
                    printf("Bad parameter: %c\n", optopt);
            }
        }
    }
    if(MESSAGE_INTERVAL == -1) {
        fprintf(stderr, "Usage: %s -a <adres*> -p <port*> -c <int*> -d <float>", argv[0]);
        exit(EXIT_FAILURE);
    }
}
int parseInt(char *arg) {
    char* endPtr = NULL;
    int intArg = (int)strtol(arg, &endPtr, 10);
    if (*arg == '\0' || *endPtr != '\0') {  // incorrect coversion
        printf("Incorrect argument! (%s)\n", arg);
    }
    return intArg;
}
float parseFloat(char *arg) {
    char* endPtr = NULL;
    double doubleArg = strtod(arg, &endPtr);
    if (*arg == '\0' || *endPtr != '\0') {
        // incorrect coversion
        printf("Incorrect argument: (%s)\n", arg);
    }
    return (float)doubleArg;
}


void setupGrimReaper() {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = grimReaper;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        errExit("sigaction");
}
static void grimReaper(int sig)
{
    int savedErrno;
    savedErrno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        continue;
    errno = savedErrno;
}

void setupSIGTERM() {
    struct sigaction action;
    memset (&action, 0, sizeof(action));
    action.sa_handler = sigterm_handler;
    if (sigaction(SIGTERM, &action, 0))
        errExit("sigterm sigaction");
}

static void sigterm_handler (int sig)   // nie trzeba nadpisywac atexit bo zawsze wychodzimy sygnalami
{
    close(serverSocket);    // QUESTION : czy trzeba tutaj zabijac wszystkie potomki i zamykac wszystkie polaczenia?
    _exit(EXIT_SUCCESS);
}