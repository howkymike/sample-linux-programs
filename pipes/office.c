#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <zconf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <asm/errno.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <dirent.h>

#define NAZWA_SIZE 50

int parseInt(char* arg);
double parseDouble(char* arg);

void createTmpFifo(char* buffer, char* path);
void generateRandomTmpPath();

void raceMain();
void createChildren();
void verifyFiles(struct timespec raceEndTime);

// race parameters  //
double raceTime = -1;    // okresla raceTime, przez jaki mozna zdobywac zasoby (w milisekundach)
int iloscPotomkow = 16;  // okresla iloscPotomkow potomkow (OPCJONALNY, domyślnie 16)
char nazwaZasobu[NAZWA_SIZE];   // nazwa zasobu - czesc nazwy generowanych plikow
char nazwaPotomkow[NAZWA_SIZE];    // poczatek nazwy potomkow
char fifoPath[FILENAME_MAX];  // wskazuje na plik FIFO (OPCJONALNY, domyslnie losowa nazwa)

int fdes[2]; // fdes[0] - read, fdes[1] - write

int main(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, ":t:n:z:p:")) != -1) {
        switch (opt) {
            case 't':
                raceTime = parseDouble(optarg);
                break;
            case 'n':
                iloscPotomkow = parseInt(optarg);
                break;
            case 'z':
                if(strlen(optarg) > NAZWA_SIZE) {
                    fprintf(stderr, "Too long resource name!\n"); exit(EXIT_FAILURE);
                }
                strcpy(nazwaZasobu, optarg);
                break;
            case 'p':
                if(strlen(optarg) > NAZWA_SIZE) {
                    fprintf(stderr, "Too long process prefix name!\n"); exit(EXIT_FAILURE);
                }
                strcpy(nazwaPotomkow, optarg);
                break;
            default:
                printf("Usage: %s -t <raceTime> -n <iloscPotomkow> -z <nazwa> -p <nazwa> <sciezka>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if(optind < argc)
        createTmpFifo(fifoPath, argv[optind]);  //strcpy(fifoPath, argv[optind]);
    else
        createTmpFifo(fifoPath, NULL);   //generateRandomTmpPath(fifoPath);

    if(optind + 1 < argc ||
       raceTime == -1 || strlen(nazwaZasobu) == 0 || strlen(nazwaPotomkow) == 0 ||
       iloscPotomkow < 5) {
        fprintf(stderr, "Usage: %s -t <raceTime> -n <iloscPotomkow> -z <nazwa> -p <nazwa> <sciezka>\n", argv[0]); exit(EXIT_FAILURE);
    }


    raceMain();

    return 0;
}



void raceMain() {
    // create pipe
    if (pipe(fdes) == -1) {
        perror("pipe"); exit(EXIT_FAILURE);
    }

    // create children
    createChildren();

    // open fifo
    int fifoDes = open(fifoPath, O_RDONLY | O_NONBLOCK); // aby nie czekal na otwarcie drugiego konca pipe
    if(fifoDes == -1) {
        if(errno == ENXIO){
            printf("(should always happen) ENXIO errno, one end of the pipe is not open");
        } else {
            perror("fifo open"); exit(EXIT_FAILURE);
        }
    }
    if (close(fdes[0]) == -1) { // unused
        perror("pipe close"); exit(EXIT_FAILURE);
    }

    // start race
    if (close(fdes[1]) == -1) {
        perror("pipe close"); exit(EXIT_FAILURE);
    }

    // wait raceTime
    struct timespec ts;
    ts.tv_sec = (time_t)(raceTime / 1000);
    ts.tv_nsec = raceTime - (double)(ts.tv_sec * 1000);
    if(nanosleep(&ts, NULL) == -1) {
        perror("Signal killed sleep!"); exit(EXIT_FAILURE);
    }

    // end race (close FIFO)
    close(fifoDes);

    // get close time
    struct timespec endRacetime;
    clock_gettime(CLOCK_REALTIME, &endRacetime);

    // wait for all children
    int child_exitStatus = 0;
    while(wait(&child_exitStatus) > 0) {
        if(child_exitStatus !=0) {
            fprintf(stderr, "childExitStatus: %d", child_exitStatus); exit(EXIT_FAILURE);
        }
    }

    // verify files
    verifyFiles(endRacetime);

    // remove fifo
    unlink(fifoPath);
}

void createChildren() {
    for(int i = 0; i < iloscPotomkow; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork"); exit(EXIT_FAILURE);
        } else if(pid == 0) {   // child
            if (close(fdes[1]) == -1) { // close unused write pipe end
                perror("pipe close"); exit(EXIT_FAILURE);
            }
            if(dup2(fdes[0], 4) == -1) {    // duplicate pipe desc to desc 4
                perror("dup2"); exit(EXIT_FAILURE);
            }
            if(close(fdes[0]) == -1) {  // close pipe desc (already copied to desc 4)
                perror("close pipe"); exit(EXIT_FAILURE);
            }


            char chldID[10];
            sprintf(chldID, "%d", i);   // getpid()
            strcat(nazwaPotomkow, chldID);  // every child make it in its own memory

            char * argv_list[] = {nazwaPotomkow,"-z", nazwaZasobu,"-s", fifoPath,NULL};
            execv("poszukiwacz",argv_list);
            //exit(0); // czy to jest potrzebne?? raczej nie (ale nie obsluguje bledow)
        }
    }
}

void verifyFiles(struct timespec raceEndTime) {
    // delete forgotten files property*
    // and delete files after race finish
    char currentPathBuffer[PATH_MAX];
    if(getcwd(currentPathBuffer, PATH_MAX) == NULL) {
        perror("getcwd"); exit(EXIT_FAILURE);
    }

    DIR *dirp = opendir(".");
    if (dirp == NULL) {
        perror("opendir"); exit(EXIT_FAILURE);
    }

    struct stat fileinfo;
    struct dirent *dp;
    for(;;) {
        errno = 0;
        dp = readdir(dirp);
        if(dp == NULL)
            break;
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;   /* Skip . and .. */

        char filenameBuffer[FILENAME_MAX+PATH_MAX];
        sprintf(filenameBuffer, "%s/%s", currentPathBuffer, dp->d_name);
        if(strncmp(dp->d_name, "property_", 9) == 0) { // delete forgotten files
            unlink(filenameBuffer); // trzeba pewnie podac pelna sciezke
        }

        stat(filenameBuffer, &fileinfo);
        if(fileinfo.st_ctim.tv_sec > raceEndTime.tv_sec ||
            (fileinfo.st_ctim.tv_sec == raceEndTime.tv_sec &&  fileinfo.st_ctim.tv_nsec > raceEndTime.tv_nsec )) {  // check if created after race
            unlink(filenameBuffer);
        }
    }

    if (errno != 0) {
        perror("readdir"); exit(EXIT_FAILURE);
    }
    if (closedir(dirp) == -1) {
        perror("closedir"); exit(EXIT_FAILURE);
    }

}

void createTmpFifo(char* buffer, char* path) {
    if(path == NULL) {
        generateRandomTmpPath(buffer);
    } else {
        if(strncmp(path, "/tmp",4) != 0) {
            fprintf(stderr, "Sciezka must be in the tmp dir!"); exit(EXIT_FAILURE);
        }
        strncpy(buffer, path, strlen(path));
    }
    umask(0);
    if(mkfifo(buffer, S_IRUSR | S_IWUSR | S_IWGRP) == -1 && errno != EEXIST) {
        perror("mkfifo"); exit(EXIT_FAILURE);
    }
}

// template: /tmp/urzad_regulacji_FIFO_%d_%d , rand(), getpid()
void generateRandomTmpPath(char* buffer) {
    sprintf(buffer, "%s_%ld_%d", "/tmp/urzad_regulacji_FIFO", time(NULL) % 1000, getpid());
}

int parseInt(char* arg) {
    char* endPtr = NULL;
    int intArg = (int) strtol(arg, &endPtr, 10);
    if (*arg == '\0' || *endPtr != '\0') {   // correct conversion to int
        fprintf(stderr, "Incorrect rozmiar!\n"); exit(EXIT_FAILURE);
    }
    return intArg;
}

double parseDouble(char *arg) {
    char* endPtr = NULL;
    double doubleArg = strtod(arg, &endPtr);
    if (*arg == '\0' || *endPtr != '\0') {
        fprintf(stderr, "Incorrect argument: %s\n", arg); exit(EXIT_FAILURE);
    }
    return doubleArg;
}

