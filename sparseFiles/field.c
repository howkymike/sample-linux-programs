#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <zconf.h>

int parseInt(char* arg);

int main(int argc, char* argv[]) {
    if(argc != 4) {
        printf("Usage %s <nazwa> -r <rozmar>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int rozmiar = 0;
    int opt;
    while ((opt = getopt(argc, argv, ":r:")) != -1) {
        if(opt == 'r') {
            rozmiar = parseInt(optarg);
        } else {
            fprintf(stderr, "Usage %s <nazwa> -r <rozmar>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    if(rozmiar == 0) {
        fprintf(stderr, "-r flag is mandatory!");
        exit(EXIT_FAILURE);
    }

    char outputPath[FILENAME_MAX];
    if(optind < argc) {
        strcpy(outputPath, argv[optind]);
    } else {
        fprintf(stderr, "rozmiar is mandatory!\n");
        exit(EXIT_FAILURE);
    }


    // tworzymy nowy plik
    int outputFD = open(outputPath, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if(outputFD == -1) {
        perror("output open"); exit(EXIT_FAILURE);
    }
//    lseek(outputFD, rozmiar, SEEK_CUR);
    ftruncate(outputFD, rozmiar);
    return 0;
}



int parseInt(char* arg) {
    char* endPtr = NULL;
    int intArg = (int) strtol(arg, &endPtr, 10);
    if (*arg != '\0' && *endPtr == '\0') {   // correct conversion to int
        return intArg*8*1000;   // rozmiar liczony w jednostkach 8 KB
    }
    fprintf(stderr, "Incorrect rozmiar!\n");
    exit(EXIT_FAILURE);
}