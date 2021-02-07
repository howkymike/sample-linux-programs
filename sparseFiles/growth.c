#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <zconf.h>
#include <errno.h>

#define BLOCK_SIZE 4096

enum expansionWay{ante, post, ambo};

typedef struct OffsetData {
    __off_t offset;
    char c;
} OffsetData;

void parseWayArgument(int argc, char* argv[]);
void handleExpansion( size_t size, enum expansionWay way);
void spreadAnte(__off_t size, struct OffsetData curr, __off_t prevDataBlockOff);
__off_t spreadPost(__off_t size, struct OffsetData curr);
__off_t spreadAmbo(__off_t size, struct OffsetData curr, __off_t prevDataBlockOff);
void sowTheField(__off_t off, __off_t size, char c);
struct OffsetData findDetailedData(__off_t off);

int fileD;
__off_t fileSize;


int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s -s <sciezka> <ekspansja> [<ekspansja>]\n", argv[0]);  exit(EXIT_FAILURE);
    }

    char outputPath[FILENAME_MAX];
    int opt;
    while ((opt = getopt(argc, argv, ":s:")) != -1) {
        if(opt == 's')
            strcpy(outputPath, optarg);
        else {
            fprintf(stderr, "Usage: %s -s <sciezka> <ekspansja> [<ekspansja>]\n", argv[0]);  exit(EXIT_FAILURE);
        }
    }

    fileD = open(outputPath, O_RDWR);
    if(fileD == -1) {
        perror("output open"); exit(EXIT_FAILURE);
    }
    fileSize = lseek(fileD, 0, SEEK_END);
    if(fileSize == -1 || lseek(fileD, 0, SEEK_SET) == -1) {
        perror("lseek"); exit(EXIT_FAILURE);
    }

    parseWayArgument(argc, argv);

    close(fileD);
    return 0;
}

void parseWayArgument(int argc, char* argv[]) {  // i.e. valid arg = "post:1K"
    int index = 0;
    size_t rozmiary[2];
    enum expansionWay kierunki[2];
    for(int i = optind; i < argc; i++) { // <kierunek>:<rozmiar> [<kierunek>:<rozmiar>]
        char* endPtr = NULL;
        rozmiary[index] = strtol(argv[i]+5, &endPtr, 10);
        if(rozmiary[index] == 0 && *argv[i] == '\0') {
            perror("strotol"); exit(EXIT_FAILURE);
        }
        if(*endPtr != '\0') {
            if(strcmp(endPtr, "bb") == 0)
                rozmiary[index] *= 512;
            else if(strcmp(endPtr, "K") == 0)
                rozmiary[index] *= 1024;
            else if(strcmp(endPtr, "B") != 0) {
                fprintf(stderr, "Niezdefiniowana jednostka rozmiaru!\n"); exit(EXIT_FAILURE);
            }
        }
        if(index == 1 && rozmiary[0] == rozmiary[index]) {
            fprintf(stderr, "Rozmiar drugiego kierunku musi byc inny!\n"); exit(EXIT_FAILURE);
        }

        if(strncmp(argv[i], "ante:",5) == 0)
            kierunki[index] = ante;
        else if(strncmp(argv[i], "post:", 5) == 0)
            kierunki[index] = post;
        else if(strncmp(argv[i], "ambo:",5) == 0) {
            if( index == 1) {
                fprintf(stderr, "Drugi kierunek moze byc tylko ante lub post!\n"); exit(EXIT_FAILURE);
            }
            kierunki[index] = ambo;
        }
        index++;
    }

    for(int i = 0; i < index; i++)
        handleExpansion(rozmiary[i], kierunki[i]);
}


void handleExpansion(size_t size, enum expansionWay way) {
    OffsetData offsetData = {.offset=0, .c='0'};
    __off_t prevDataBlock = 0;  // poczatek wczesnijeszego bloku z danymi
    while(offsetData.offset < fileSize) {
        offsetData = findDetailedData(offsetData.offset);   // zwraca offset poczatku bloku z danymi
        if(offsetData.offset == -1)
            return;

        __off_t changedOff;
        if(way == ante) {
            spreadAnte(size, offsetData, prevDataBlock);
            changedOff = offsetData.offset + BLOCK_SIZE;
        }
        else if(way == post)
            changedOff = spreadPost(size, offsetData);   // returns end of written data block
        else if(way == ambo)
            changedOff = spreadAmbo(size, offsetData, prevDataBlock);   // returns end of written data block
        prevDataBlock = offsetData.offset;
        if(offsetData.offset == changedOff)
            offsetData.offset += BLOCK_SIZE;    // should never happen
        else
            offsetData.offset = changedOff;
    }
}

void spreadAnte(__off_t size, struct OffsetData curr, __off_t prevDataBlockOff) { // curr - poczatek bloku z danymi
    // zasiej wczesniej (ale sprawdz najpierw czy nie zachodzi)
    __off_t sowOff;
    __off_t sowSize = size;
    if(prevDataBlockOff + BLOCK_SIZE + size < curr.offset) {        // bez problemu zasiewamy
        sowOff = curr.offset - size;
        if(sowOff < 0)
            sowOff = 0;
    } else {    // zasiewamy do do konca bloku wczesniejszych danych
        sowOff = prevDataBlockOff + BLOCK_SIZE;
        sowSize = curr.offset - sowOff;
    }
    if(sowSize > 0)
        sowTheField(sowOff, sowSize, curr.c);
}


__off_t spreadPost( __off_t size, OffsetData curr) {
    // zasiej pozniejsze (sprawdz czy nie zachodzi)
    __off_t nextblockDataOff = lseek(fileD, curr.offset + BLOCK_SIZE, SEEK_DATA); // poszukaj nastepnego bloku danych
    if(nextblockDataOff == -1) {
        if(errno == ENXIO)
            nextblockDataOff = fileSize; // nie ma danych
        else {
            perror("lseek"); exit(EXIT_FAILURE);
        }
    }

    __off_t sowOff = curr.offset + BLOCK_SIZE;
    __off_t sowSize = size;
    if(curr.offset + BLOCK_SIZE + size >= nextblockDataOff)    // siejemy do nastepnego bloku
        sowSize = nextblockDataOff - (curr.offset + BLOCK_SIZE);

    __off_t numberOfAllocatedBlocks = 0;
    if(sowSize > 0) {
        sowTheField(sowOff, sowSize, curr.c);
        numberOfAllocatedBlocks = (int)(sowSize / BLOCK_SIZE) + 1;
    }
    return curr.offset + BLOCK_SIZE + numberOfAllocatedBlocks * BLOCK_SIZE;
}


__off_t spreadAmbo(__off_t size, OffsetData curr, __off_t prevDataBlockOff) {
    spreadAnte(size, curr, prevDataBlockOff);

    // zasiej pozniejsze (tutaj nastepne pola jeszcze nie sa rozsiane wiec ewentualnie do polowy tylko !)
    __off_t nextblockDataOff = lseek(fileD, curr.offset + BLOCK_SIZE, SEEK_DATA); // poszukaj nastepnego bloku danych
    if(nextblockDataOff == -1) {
        if(errno == ENXIO)
            nextblockDataOff = fileSize; // nie ma danych
        else {
            perror("lseek"); exit(EXIT_FAILURE);
        }
    }

    __off_t sowOff = curr.offset + BLOCK_SIZE;
    __off_t sowSize = size;
    if(curr.offset + BLOCK_SIZE + size >= nextblockDataOff)
        sowSize = (nextblockDataOff - (curr.offset + BLOCK_SIZE))/2;

    __off_t numberOfAllocatedBlocks = 0;
    if(sowSize > 0) {
        sowTheField(sowOff, sowSize, curr.c);
        numberOfAllocatedBlocks = (int)(sowSize / BLOCK_SIZE) + 1;
    }
    return curr.offset + BLOCK_SIZE + numberOfAllocatedBlocks * BLOCK_SIZE;
}

struct OffsetData findDetailedData(__off_t off) {
    OffsetData offsetData = {.offset=-1,.c=0};
    __off_t blockDataOff = lseek(fileD, off, SEEK_DATA);
    if(blockDataOff == -1) {
        if(errno == ENXIO)
            return offsetData; // nie ma danych
        perror("lseek"); exit(EXIT_FAILURE);
    }
    offsetData.offset = blockDataOff;

    char buffer[BLOCK_SIZE];
    if(read(fileD, buffer, BLOCK_SIZE) == -1) {
        perror("read"); exit(EXIT_FAILURE);
    }

    for(int i =0; i < BLOCK_SIZE; i++) {
        if(buffer[i] != '\0') {
            offsetData.c = buffer[i];
            return offsetData;
        }
    }

    printf("Data not found in the DATA block\n Probably zeros are written!\n");    // nie powinno sie zdarzyc, ktos wpisal zle dane!
    return offsetData;
}


void sowTheField(__off_t off, __off_t size, char c) {
    if(lseek(fileD, off, SEEK_SET) == -1) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }
    char* buffer = (char*) malloc(size);
    if(buffer == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memset(buffer, c, size);
    if(write(fileD, buffer, size) == -1) {
        perror("write");
        exit(EXIT_FAILURE);
    }
}