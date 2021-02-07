#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <zconf.h>
#include <errno.h>

#define BUFFER_SIZE 4096

// PYTANIE!!! Bez flagi '!' odczytujemy same zera, ale pomijamy te zera w dziurach, tak??? (albo nie..)

void findData(int fileD, __off_t fileSize);
void printData(int fileD);
void printDataRow(char c, int count);
void readNormal(int fileD, __off_t fileSize);
__off_t printHole(int fileD, __off_t currOff);


int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s <sciezka> -!  (optional)\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // parsujemy argumenty
    int printHolesFlag = 1;
    int opt;
    while ((opt = getopt(argc, argv, ":!")) != -1) {
        if(opt == '!')
            printHolesFlag = 0; // nie printujemy dziur
        else {
            fprintf(stderr, "Usage: %s <sciezka> -!  (optional)", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    char outputPath[FILENAME_MAX];
    if(optind < argc) {
        strcpy(outputPath, argv[optind]);
    } else {
        fprintf(stderr, "<sciezka> is  mandatory!\n");
        exit(EXIT_FAILURE);
    }

    // otwieramy plik
    int fileD = open(outputPath, O_RDONLY);
    if(fileD == -1) {
        perror("output open"); exit(EXIT_FAILURE);
    }

    // rozmiar
    __off_t fileSize = lseek(fileD, 0, SEEK_END);   // get fileSize
    if(fileSize == -1) {
        perror("lseek");  exit(EXIT_FAILURE);
    }
    if(lseek(fileD, 0, SEEK_SET) == -1) {    // WROC NA POCZATEK
        perror("lseek");  exit(EXIT_FAILURE);
    }

    // znajdujemy dane
    if(printHolesFlag)
        findData(fileD, fileSize);
    else
        readNormal(fileD, fileSize);

    // zamykamy plik
    close(fileD);
    return 0;
}



void findData(int fileD, __off_t fileSize) {
    __off_t currOff = 0;

    // szukaj na zmienae dziur i danych, zaczynajac od dziur (wczesniej sprawdzam czy na poczatku sa dane)
    while(currOff < fileSize) {
        errno = 0;
        __off_t off = lseek(fileD, currOff, SEEK_DATA); // szukamy danych
        if( off == -1) {
            if(errno == ENXIO)  { // no data, print hole and exit
                printHole(fileD, currOff);
                return;
            }
            perror("lseek"); exit(EXIT_FAILURE);
        }

        if(off == currOff) {  // jezeli obecny blok jest blokiem danych to przeczytaj
            printData(fileD);
            currOff += BUFFER_SIZE;
            continue;
        } else {    // jezeli nie bylo danych to wroc na poczatek
            off = lseek(fileD, currOff, SEEK_SET);
            if( off == -1) {
                perror("lseek");  exit(EXIT_FAILURE);
            }
        }

        off = printHole(fileD, off);
        currOff = off;
    }
}

void printData(int fileD) {
    char buffer[BUFFER_SIZE];
    if(read(fileD, &buffer, BUFFER_SIZE) == -1) {
        perror("read"); exit(EXIT_FAILURE);
    }
    int count = 1;
    for(int i = 1; i < BUFFER_SIZE; i++) {
        if(buffer[i-1] != buffer[i]) {
            printDataRow(buffer[i-1], count);
            count = 1;
            continue;
        }
        count++;
    }
    printDataRow(buffer[BUFFER_SIZE - 1], count);
}


void printDataRow(char c, int count) {
    if(c >= 32 && c < 127) {
        printf("%c : %d\n", c, count);
    } else {
        printf("%d : %d\n", c, count);
    }
}

__off_t printHole(int fileD, __off_t currOff) {
    __off_t endOff = lseek(fileD, currOff, SEEK_DATA);
    errno = 0;
    if(endOff == -1) {
        endOff = lseek(fileD, 0, SEEK_END);
        if(endOff == -1) {
            perror("lseek"); exit(EXIT_FAILURE);
        }
    }
    __off_t size = endOff - currOff;
    printf("!HOLE! (%ld)\n", size);
    return endOff;
}



void readNormal(int fileD, __off_t fileSize) {
    __off_t off = 0;
    char buffer[BUFFER_SIZE];
    if(read(fileD, &buffer, BUFFER_SIZE) == -1) {
        perror("read"); exit(EXIT_FAILURE);
    }

    int count = 0;
    char prevChar = buffer[0];
    while(1) {
        for(int i = 0; i < BUFFER_SIZE; i++) {
            if(buffer[i] != prevChar) {
                printDataRow(prevChar, count);
                prevChar = buffer[i];
                count = 1;
                continue;
            }
            count++;
        }
        off += BUFFER_SIZE;
        if(off >= fileSize)
            break;
        if(read(fileD, &buffer, BUFFER_SIZE) == -1) {
            perror("read"); exit(EXIT_FAILURE);
        }
    }
    printDataRow(buffer[BUFFER_SIZE-1], count);
}