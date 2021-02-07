#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                               } while (0)
#define clear() printf("\033[H\033[J")
#define gotoxy(x,y) printf("\033[%d;%dH", (y), (x))

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define RESET "\x1B[0m"

#define MESSAGESIZE 45

float intervalDec;  // w decysekundach
int intervalCount, coordX, coordY, optimalCoordX, optimalCoordY;
timer_t initialTimer, childTimer;
int intialTimerCounter = 0;
struct winsize ws;
int sigContCount = 0;
siginfo_t signalInfoFeedback;
pid_t childPID;
struct timespec contTime;
int newSizeFlag = 0; // 1 - new size was set
const char *DATE_STR = "2020-01-12T12:12:12.120";

void parseArgs(int argc, char* argv[]);
void createInitialTimer();
void waitForTimerValues();
void getAndSetupTerminalSize();
void createChild();
void waitForChildSignals();
void childMain();
void setupChildTimer();
void setupContSignal();
void handleNewTerminalSize();

static void initialTimerHandler(int sig, siginfo_t *si, void *uc);
static void sigchldHandler(int sig, siginfo_t *si, void *uc);
static void sigwinchHandler(int sig);
static void contHandler(int sig, siginfo_t *si, void *uc);

void float2timespec(float f, struct timespec *ts);
static inline void timespec_diff(struct timespec *a, struct timespec *b, struct timespec *result);


int main(int argc, char* argv[]) {
    parseArgs(argc, argv);

    createInitialTimer();
    waitForTimerValues();
    getAndSetupTerminalSize();
    createChild();
    waitForChildSignals();

    return 0;
}

void parseArgs(int argc, char* argv[]) {
    if(argc == 2) {
        char* endPtr = NULL;
        intervalDec = (float)strtod(argv[1], &endPtr);
        if(*endPtr == ':' && intervalDec != 0) {
            char* endPtr2 = NULL;
            intervalCount = (int)strtol((endPtr+1), &endPtr2, 10);
            if(*endPtr2 == ':') {
                endPtr = NULL;
                coordX = (int)strtol((endPtr2+1), &endPtr, 10);
                optimalCoordX = coordX;
                if(*endPtr == ',') {
                    endPtr2 = NULL;
                    coordY = (int)strtol((endPtr+1), &endPtr2, 10);
                    optimalCoordY = coordY;
                    return;
                }
            }
        }
    }
    fprintf(stderr, "Usage: %s <float>:<int>:<int>,<int>\n<float> must be greater than 0.1!", argv[0]);
    exit(EXIT_FAILURE);
}

void createInitialTimer() {
    // register initialTimer handler
    struct sigaction sa = {.sa_flags = SA_SIGINFO | SA_NOCLDWAIT, .sa_sigaction = initialTimerHandler};
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN+1, &sa, NULL) == -1)
        errExit("sigaction");

    // create initialTimer
    struct sigevent sev = {.sigev_notify=SIGEV_SIGNAL, .sigev_signo=SIGRTMIN+1};
    if (timer_create(CLOCK_REALTIME, &sev, &initialTimer) == -1)
        errExit("timer_create");
//
    // set initialTimer (start)
    struct itimerspec ts={0};
    float2timespec(intervalDec*10, &(ts.it_interval));

    ts.it_value.tv_nsec=1;   // jak obie wartosci beda 0  to timer zostanie rozbrojony!!

    if (timer_settime(initialTimer, 0, &ts, NULL) == -1)
        errExit("timer_settime");
}

void waitForTimerValues() {
    for (int i = intervalCount; i >= 0; --i) {
        pause();
        if(i != 0 )
            printf("odliczanie #%d\n", i);
        else {
            struct itimerspec ts={0};
            if (timer_settime(initialTimer, 0, &ts, NULL) == -1)    // disarm timer
                errExit("timer_settime");
        }
    }
}

void getAndSetupTerminalSize() {
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
        errExit("ioctl");

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigwinchHandler;
    if (sigaction(SIGWINCH, &sa, NULL) == -1)
        errExit("sigaction");

    handleNewTerminalSize();
}

void createChild() {
    // register child handler
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigchldHandler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        errExit("sigaction");

    // block SIGCHLD to prevent its devlivery if a child terminates before parent commences the loop below
    sigset_t blockMask;
    sigemptyset(&blockMask);
    sigaddset(&blockMask, SIGCHLD);
    if (sigprocmask(SIG_SETMASK, &blockMask, NULL) == -1)
        errExit("sigprocmask");
    childPID = fork();
    switch (childPID) {
        case -1:
            errExit("fork");
        case 0: /* Child - sleeps and then exits */
            //sigaddset(&blockMask, SIGWINCH);
            setupContSignal();
            setupChildTimer();
            childMain();
            exit(EXIT_SUCCESS);
        default:
            break;
    }
}

void waitForChildSignals() {
    int i = 0;
    sigset_t emptyMask;
    sigemptyset(&emptyMask);     // wait for SIGCHLD
    for(;;) {
        if(newSizeFlag)
            handleNewTerminalSize();
        int sigsuspendRet = sigsuspend(&emptyMask);
        if (sigsuspendRet == -1 && errno != EINTR) {
            errExit("sigsuspend");
        }
        printf(RED);    // change color to red
        gotoxy(optimalCoordX, optimalCoordY);
        if(signalInfoFeedback.si_status == SIGSTOP) {  // if stopped - sleep 1sek and send SIGCONT
            printf("Child is stopped\n");
            sleep(1);
            kill(childPID, SIGCONT);
        } else if(signalInfoFeedback.si_status == SIGCONT) { // if resumed - clock_getitme OR signalInfoFeedback.si_stime ??
            sigContCount++;//debug
            if (clock_gettime(CLOCK_REALTIME, &contTime) == -1)
                errExit("clock_gettime");
            printf("Child is resumed (#%d)\n", i++ + 1);
        } else if(signalInfoFeedback.si_code == CLD_EXITED || sigsuspendRet == -1) { // if finished - clock_gettime and print diff
            struct timespec exitTime, diffTime;
            if (clock_gettime(CLOCK_REALTIME, &exitTime) == -1)
                errExit("clock_gettime");
            timespec_diff(&exitTime, &contTime, &diffTime);
            clear();
            printf("Child is dead\nTime diff: %ldsec %ldnsec\n", diffTime.tv_sec, diffTime.tv_nsec);
            exit(EXIT_SUCCESS);
        }
    }
}

void childMain() {
    setbuf(stdout, NULL);
    struct timespec ts, sleepTs, sleepRemain;
    sleepTs.tv_sec = 0;
    sleepTs.tv_nsec = 300000000;
    int s;
    for(;;) {
        if(newSizeFlag)
            handleNewTerminalSize();
        if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
            errExit("clock_gettime");
        gotoxy(optimalCoordX+4, optimalCoordY+1);
        printf(GRN);
        char timeBuff[256];
        strcpy(timeBuff, ctime(&ts.tv_sec));
        timeBuff[strlen(timeBuff)-1]='\0';
        printf("Current time: %s, csec:%d\n", timeBuff,  ((int)(ts.tv_nsec/10000000)) );
        s = nanosleep(&sleepTs, &sleepRemain);
        if (s == -1 && errno != EINTR)
            errExit("nanosleep");
        if(s == -1 && errno == EINTR)
            nanosleep(&sleepRemain, NULL);  // czy tak wystarczy to zrobic?
    }
}

void setupChildTimer() {
    // create childTimer
    struct sigevent sev = {.sigev_notify=SIGEV_SIGNAL, .sigev_signo=SIGSTOP};
    if (timer_create(CLOCK_REALTIME, &sev, &childTimer) == -1)
        errExit("timer_create");

    // set childTimer (start)
    struct itimerspec ts={0};
    float2timespec(((float)intervalDec)*10, &(ts.it_interval));
    ts.it_value.tv_nsec=1;   // jak obie wartosci beda 0  to timer zostanie rozbrojony!!

    if (timer_settime(childTimer, 0, &ts, NULL) == -1)
        errExit("timer_settime");
}

void setupContSignal() {
    // register cont handler
    struct sigaction sa = {.sa_flags = SA_SIGINFO, .sa_sigaction = contHandler};
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCONT, &sa, NULL) == -1)
        errExit("sigaction");
}

void handleNewTerminalSize() {
    clear();
    if(ws.ws_col - MESSAGESIZE < optimalCoordX) {
        if(ws.ws_col - MESSAGESIZE > 0)
            optimalCoordX = ws.ws_col - MESSAGESIZE;
        else
            optimalCoordX = 0;
    } else
        optimalCoordX = coordX;
    if(ws.ws_row < optimalCoordY)
        optimalCoordY = ws.ws_row;
    else
        optimalCoordY = coordY;
    newSizeFlag = 0;
}

static void initialTimerHandler(int sig, siginfo_t *si, void *uc) {
    ++intialTimerCounter;
}

static void sigchldHandler(int sig, siginfo_t *si, void *uc)
{
    signalInfoFeedback = *si;
}

static void contHandler(int sig, siginfo_t *si, void *uc) {
    ++sigContCount;
    if(sigContCount >= intervalCount)
        exit(EXIT_SUCCESS);
}

static void sigwinchHandler(int sig) {
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
        errExit("ioctl");
    newSizeFlag = 1;
}

void float2timespec(float f, struct timespec *ts) {
    ts->tv_sec = (long)f;
    ts->tv_nsec = (f - (float)ts->tv_sec) * 1000000000.0;
}

static inline void timespec_diff(struct timespec *a, struct timespec *b, struct timespec *result) {
    result->tv_sec  = a->tv_sec  - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (result->tv_nsec < 0) {
        --result->tv_sec;
        result->tv_nsec += 1000000000L;
    }
}
