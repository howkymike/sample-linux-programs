#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <zconf.h>
#include <stdint.h>

#define NAZWA_SIZE 50
#define PIPE_DESCRIPTOR_NR 4

void run();
void createFile();
void changeFilename(int status, void *arg);

char filenameBuff[2*NAZWA_SIZE+10];
char resourceName[NAZWA_SIZE];   // nazwa zasobu - czesc nazwy generowanych plikow
char fifoPath[NAZWA_SIZE];    // poczatek nazwy potomkow
char processName[NAZWA_SIZE];
int createdFilesCount = 0;

uintptr_t fileNumIterator = 0;
int filesCount = 0;

// Usage: poszukiwacz -z <nazwa> -s <sciezka>
int main(int argc, char* argv[]) {
    if(argc < 3) {
        fprintf(stderr, "Bad parameters!\n"); exit(EXIT_FAILURE);
    }

    int dummyBuffer;
    if(read(4, &dummyBuffer, 1) != 0) { // waiting for a race start
        fprintf(stderr, "child didnt get EOF"); exit(EXIT_FAILURE);
    }


    strcpy(processName, argv[0]);  // jako argument przekazywana jest tylko nazwa

    int opt;
    while ((opt = getopt(argc, argv, ":z:s:")) != -1) {
        switch (opt) {
            case 'z':
                strcpy(resourceName, optarg);
                break;
            case 's':
                strcpy(fifoPath, optarg);
                break;
            default:
                printf("Usage: %s -z <nazwa> -s <sciezka>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    errno = 0;
    if(fcntl(PIPE_DESCRIPTOR_NR, F_GETFD) == -1) {  // check if descriptor is ok
        perror("fcntl"); exit(EXIT_FAILURE);
    }


    run();
    return 0;
}



void run() {
    while(fileNumIterator < 100) {  // we want to create max 100 files
        // check if can run
        int fifoFd = open(fifoPath, O_WRONLY | O_NONBLOCK);   // O_NONBLOCK for write calls
        if(fifoFd == -1) {
            if(errno == ENXIO)  // other end of FIFO closed
                break;
            perror("fifo open"); exit(EXIT_FAILURE);
        }

        createFile();
        close(fifoFd);
    }
}

// filename: property_<N>.<nazwa_zasobu>mine
void createFile() {
    int fileD;
    while(fileNumIterator < 100) {
        sprintf(filenameBuff, "property_%02lu.%smine", fileNumIterator, resourceName);   // create a filename, moze byc relatywna sciezka
        errno = 0;
        fileD = open(filenameBuff, O_CREAT | O_WRONLY | O_EXCL, S_IRUSR | S_IWUSR); // try to create file
        if(fileD == -1) {
            if(errno == EEXIST) {
                fileNumIterator++;
                continue;
            }
            perror("open"); exit(EXIT_FAILURE);
        }
        if(ftruncate(fileD, ++createdFilesCount * 384) == -1) { // truncate file (change size)
            perror("ftruncated"); exit(EXIT_FAILURE);
        }
        //fprintf(stderr, "Created file: %s\n", filenameBuff); // DEBUG
        // filenameBuff jest jeden dla wielu nazw, ale w argumencie przekazuje tylko numer, a potem dynamicznie odtwarzam te nazwy (zaoszczedzam na pamieci kosztem dodatkowych obliczen)
        if(on_exit(changeFilename, (void *) fileNumIterator) != 0) { // register
            perror("on_exit"); exit(EXIT_FAILURE);
        }
        filesCount++;
        break;
    }
}

// new filename template: <nazwa_potomka>_<nazwa_zasobu>mine.#<nr>
// *arg to N
void changeFilename(int status, void *arg) {
    char oldFilenameBuff[2*NAZWA_SIZE+10];
    sprintf(oldFilenameBuff, "property_%02ld.%smine", ((uintptr_t)arg), resourceName);

    char newFilenameBuff[2*NAZWA_SIZE+10];
    sprintf(newFilenameBuff, "%s_%smine.#%d", processName, resourceName, filesCount--);

    if(rename(oldFilenameBuff, newFilenameBuff) == -1) {
        perror("rename"); exit(EXIT_FAILURE);
    }
}