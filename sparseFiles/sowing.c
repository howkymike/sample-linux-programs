#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define MAX_TEXT_SIZE 1024

void handleText();
void handleNumber(char* arg);
__off_t selectRandomLocation(size_t neededSize);
__off_t selectFreeLocation(size_t neededSize);
__off_t checkIfEnoughSpace(__off_t startOff, size_t size);

int fileD;
long int fileSize;
char text_buffer[MAX_TEXT_SIZE];


///////////////////////////////////
// Przykładowe wywołanie: ./zasiew -s dziurawy.txt -f tekst aaa bbb
///////////////////////////////////
int main(int argc, char* argv[]) {
    if(argc < 5) {
        printf("Usage %s <nazwa> -s <sciezka> -f <format> val_1 ... val_N\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    srand(time(NULL));

    char sciezka[FILENAME_MAX];
    int iftext = 0;
    int opt;
    while ((opt = getopt(argc, argv, ":s:f:")) != -1) {
        switch (opt) {
            case 's':
                strcpy(sciezka, optarg);
                break;
            case 'f': {
                if(strcmp(optarg, "liczba") == 0) {
                    iftext = 0;
                } else if(strcmp(optarg, "tekst") == 0) {
                    iftext = 1;
                } else {
                    fprintf(stderr, "-f must be 'liczba' or 'tekst'\n");
                    exit(EXIT_FAILURE);
                }
                break;
            }
            default:
                fprintf(stderr, "Usage %s <nazwa> -s <sciezka> -f <format> val_1 ... val_N\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if(strlen(sciezka) == 0) {
        fprintf(stderr, "Podaj sciezke!\n");
        exit(EXIT_FAILURE);
    }


    fileD = open(sciezka, O_RDWR, 770);
    if(fileD == -1) {
        perror("output open"); exit(EXIT_FAILURE);
    }
    fileSize = lseek(fileD, 0, SEEK_END);;

    if(iftext == 0) {
        for(int i = optind; i < argc; i++) {
            handleNumber(argv[i]);
        }
    } else {
        char* buffPtr = text_buffer;
        for(int i = optind; i < argc; i++) {
                memcpy(buffPtr, argv[i], strlen(argv[i]));
                buffPtr += strlen(argv[i]);
        }
        handleText();
    }

    close(fileD);
    return 0;
}


void handleText() {
    selectRandomLocation(strlen(text_buffer));
    if(write(fileD, text_buffer, strlen(text_buffer)) == -1) {
        perror("write"); exit(EXIT_FAILURE);
    }
}

void handleNumber(char* arg) {
    char* endPtr = NULL;
    long int number = strtol(arg, &endPtr, 0);
    if(*arg == '\0' || *endPtr != '\0') {
        fprintf(stderr, "Failed to parse a number\n");
        exit(EXIT_FAILURE);
    }
    selectRandomLocation(sizeof(long int));
    if(write(fileD, &number, sizeof(long int)) == -1) {
        perror("write"); exit(EXIT_FAILURE);
    }
}


__off_t selectRandomLocation(size_t neededSize) {
    off_t off = rand() % fileSize;
    off = lseek(fileD, off, SEEK_HOLE);
    if(off == -1){
        perror("lseek");
        exit(EXIT_FAILURE);
    }
    if(checkIfEnoughSpace(off, neededSize) == 0) {
        return off;
    }
    // jezeli nie udalo sie wylosowac, to szukamy od poczatku
    return selectFreeLocation(neededSize);
}

__off_t selectFreeLocation(size_t neededSize) { // szuka jakiejkolwiek wolnej lokalizacji ktora pomiesci rozmiar neededSize
    off_t off = 0;
    while(1) {
        errno = 0;
        off = lseek(fileD, off, SEEK_HOLE); // szukamy wolnego meiejsca
        if( off == -1) {
            perror("lseek selectFreeLocation");  // no space left (?)
            exit(EXIT_FAILURE);
        }
        off_t secondOff = checkIfEnoughSpace(off, neededSize);  // patrzymy czy dziura ma wystarczajaco duzo miejsca
        if( secondOff == 0) // jezeli ma to zwracamy jej poczatek
            return off;

        off = secondOff; // jezeli nie to zaczynamy poszukiwania od poaczatku jakis danych (lub konca pliku)
        if(off>= fileSize) {    // jezeli jest to koniec pliku to zwracamy blad
            fprintf(stderr, "No space left!\n");
            exit(EXIT_FAILURE);
        }
    }
}


__off_t checkIfEnoughSpace(__off_t startOff, size_t size) { // sprawdza miejsce i zwraca albo 0 (jak sie uda znalezc) albo poczatek nastepnych danych/konca pliku
    __off_t secondOff = lseek(fileD, startOff, SEEK_DATA);
    if(secondOff == -1) {   // should be ENXIO flag, there is no data after the offset
        secondOff = lseek(fileD, 0, SEEK_END);
        if(secondOff == -1) {
            perror("lseek"); exit(EXIT_FAILURE);
        }
    }
    if(secondOff - startOff >= size) {
        if(lseek(fileD, startOff, SEEK_SET) == -1) {    // powracam do poprzedniej lokalizacji
            perror("lseek"); exit(EXIT_FAILURE);
        }
        return 0;
    }
    return secondOff;
}