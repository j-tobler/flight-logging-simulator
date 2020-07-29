#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <ctype.h>
#include <zconf.h>
#include <signal.h>

/* Represents an airport, with associated name and port number for network
 * connections */
typedef struct {
    /* The name (or 'id') of the airport */
    char* name;
    /* The associated port number that this airport is listening on */
    char* portNumber;
} Airport;

/* A collection of arguments for the client_handler function, to be used in
 * pthread creation for connected clients */
typedef struct {
    /* The client's file descriptor from which to open read and write
     * streams */
    int fileDescriptor;
    /* The lock shared amongst pthreads to prevent simultaneous interactions
     * with the same memory */
    sem_t* lock;
    /* The array airports registered with the mapper */
    Airport** airports;
    /* The size of the array of airports registered with the mapper */
    int* numAirports;
} ClientPackage;

void* client_handler(void* vars);
void init_lock(sem_t* lock);
void take_lock(sem_t* lock);
void release_lock(sem_t* lock);
int get_airport_index(char* airportName, Airport** airports, int numAirports);
void add_airport(char command[], Airport** airports, int* numAirports);
int is_integer(char* string);
int listen_on_ephemeral_port();
void handle_input(char* message, FILE* stream, Airport** airports,
        int* numAirports);
in_port_t get_port_number(int fileDescriptor);

/* The maximum permitted size of messages sent and received via network
 * communications */
#define MAX_CHARS 79

int main(int argc, char** argv) {
    if (argc != 1) {
        return 1;
    }

    // the maximum number of airports this mapper can register
    size_t maxAirports = 1000;

    /* Initialise the lock that will be used to ensure pthread safety */
    sem_t lock;
    init_lock(&lock);

    /* Initialise the array of airports this mapper will store */
    Airport** airports = malloc(maxAirports * sizeof(Airport*));
    int numAirports = 0;

    /* Begin listening on an ephemeral port, and print that port to stdout */
    int socketFileDescriptor = listen_on_ephemeral_port();
    in_port_t portNumber = get_port_number(socketFileDescriptor);
    printf("%u\n", portNumber);
    fflush(stdout);

    /* Continuously accept and handle callers */
    for (int i = 0; i < maxAirports; i++) {
        int* fileDescriptor = malloc(sizeof(int));
        ClientPackage* clientPackage = malloc(sizeof(ClientPackage));
        if (*fileDescriptor = accept(socketFileDescriptor, 0, 0),
                *fileDescriptor >= 0) {
            clientPackage->fileDescriptor = *fileDescriptor;
            clientPackage->airports = airports;
            clientPackage->numAirports = &numAirports;
            clientPackage->lock = &lock;
            pthread_t* threadID = malloc(sizeof(threadID));
            pthread_create(threadID, 0, client_handler, clientPackage);
        }
    }
}

/**
 * Finds the address of any available ephemeral port, binds a socket to it,
 * begins listening on the socket's file descriptor, and returns the socket's
 * file descriptor if successful.
 * @return the file descriptor of the bound socket, or -1 if an error occurred.
 */
int listen_on_ephemeral_port() {
    /* Retrieve the address of any available ephemeral port */
    struct addrinfo* addressInfo = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo("localhost", 0, &hints, &addressInfo)) {
        freeaddrinfo(addressInfo);
        return -1;
    }
    /* Create a socket and bind it to the address we just retrieved */
    int fileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(fileDescriptor, addressInfo->ai_addr, sizeof(struct sockaddr))) {
        return -1;
    }
    /* Binding succeeded; begin listening on the port */
    if (listen(fileDescriptor, 10)) {
        return -1;
    }
    return fileDescriptor;
}

/**
 * Returns the port number of an allocated socket.
 * @param fileDescriptor - the socket's file descriptor.
 * @return the port number of the given socket, or -1 if an error occurred.
 */
in_port_t get_port_number(int fileDescriptor) {
    struct sockaddr_in socketAddress;
    memset(&socketAddress, 0, sizeof(struct sockaddr_in));
    socklen_t size = sizeof(struct sockaddr_in);
    if (getsockname(fileDescriptor, (struct sockaddr*)&socketAddress, &size)) {
        return -1;
    }
    return ntohs(socketAddress.sin_port);
}

/**
 * Handles a connection to a client, represented as a pthread. Repeatedly
 * attempts to read and process input from the client, until a reading error
 * occurs or the client closes the connection. Input is handled by
 * handle_input.
 * @param vars - a void pointer which can be casted to a type ClientPackage for
 * the retrieval of function arguments.
 * @return - NULL on exit.
 */
void* client_handler(void* vars) {
    /* Unpack variables from the ClientPackage which vars points to */
    ClientPackage clientPackage = *(ClientPackage*)vars;
    int fileDescriptor = clientPackage.fileDescriptor;
    sem_t* lock = clientPackage.lock;
    Airport** airports = clientPackage.airports;
    int* numAirports = clientPackage.numAirports;
    /* Open reading and writing streams to the given file descriptor */
    int fileDescriptorCopy = dup(fileDescriptor);
    FILE* readStream = fdopen(fileDescriptor, "r");
    FILE* writeStream = fdopen(fileDescriptorCopy, "w");

    /* Continuously read and process input */
    while (1) {
        /* Retrieve and verify input */
        size_t bufferSize = MAX_CHARS + 1; // +1 to include null terminator
        char* message = malloc(bufferSize * sizeof(char));
        if (!fgets(message, bufferSize, readStream)) {
            break; // error reading from client; close connection
        }
        message[strlen(message) - 1] = 0; // truncate trailing '\n'
        size_t len = strlen(message);
        if ((message[0] == '?' || message[0] == '!') && len < 2) {
            continue; // message is invalid; ignore
        }

        /* Process input */
        take_lock(lock);
        handle_input(message, writeStream, airports, numAirports);
        release_lock(lock);
    }

    fclose(readStream);
    fclose(writeStream);
    return NULL;
}

/**
 * Handles input from a client, according to the following specification:
 * Command      Purpose
 * ?ID          Send the port number for the airport called ID
 * !ID:PORT     Add airport called ID with PORT as the port number
 * @            Send back all names and their corresponding ports
 * @param message - the input from the client to be handled.
 * @param stream - the stream to write to when sending back output.
 * @param airports - an array of all registered airports, to read and add to.
 * @param numAirports - the size of the array of registered airports given.
 */
void handle_input(char* message, FILE* stream, Airport** airports,
        int* numAirports) {
    if (message[0] == '?') {
        /* If a registered airport with the given id exists, send back its port
         * number. Otherwise, send back a semicolon */
        int index = get_airport_index(&message[1], airports, *numAirports);
        if (index == -1) {
            fprintf(stream, ";\n");
        } else {
            fprintf(stream, "%s\n", airports[index]->portNumber);
        }
        fflush(stream);
    } else if (message[0] == '!') {
        /* Register the airport id and port number specified in the message */
        add_airport(&message[1], airports, numAirports);
    } else if (strcmp(message, "@") == 0) {
        /* Display a list of all registered airport id's and associated port
         * numbers */
        for (int i = 0; i < *numAirports; i++) {
            fprintf(stream, "%s:%s\n", airports[i]->name,
                    airports[i]->portNumber);
        }
        fflush(stream);
    }
}

/**
 * Searches the given array of airports for an entry with id matching the given
 * airport name.
 * @param airportName - the id to search for.
 * @param airports - the array of airports to search within.
 * @param numAirports - the size of the given array of airports.
 * @return - the index of the associated entry if found, else -1.
 */
int get_airport_index(char* airportName, Airport** airports, int numAirports) {
    for (int i = 0; i < numAirports; i++) {
        if (strcmp(airportName, airports[i]->name) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Takes a command in the form of "ID:PORT", where ID is an airport ID, and
 * PORT is its associated port number, and adds the airport ID and port number
 * to the given array of registered airports, if the ID does not already exist
 * in the array. Airports are inserted into the array such as to maintain
 * lexicographic ordering of airport IDs.
 * @param command - a string containing the airport ID and port number,
 * represented in the syntax "ID:PORT".
 * @param airports - the array of registered airports.
 * @param numAirports - the size of the given array of airports.
 */
void add_airport(char command[], Airport** airports, int* numAirports) {
    /* Process and verify command syntax */
    char* airportName = strtok(command, ":");
    if (get_airport_index(airportName, airports, *numAirports) != -1) {
        return; // id already exists
    }
    char* portNumber = strtok(NULL, ":");
    if (!portNumber || strlen(portNumber) < 1 || !is_integer(portNumber)) {
        return; // missing or invalid port number
    }
    /* Create new airport with the given id and port number */
    Airport* airport = malloc(sizeof(Airport));
    airport->name = airportName;
    airport->portNumber = portNumber;
    /* Insert this airport into the correct position in the airports list */
    // find appropriate index
    int insertionIndex = 0;
    while (insertionIndex < *numAirports &&
            strcmp(airportName, airports[insertionIndex]->name) >= 0) {
        insertionIndex++;
    }
    // shift all airports with index >= this index one position to the right
    for (int i = *numAirports - 1; i >= insertionIndex; i--) {
        airports[i + 1] = airports[i];
    }
    // insert airport into this index
    airports[insertionIndex] = airport;
    (*numAirports)++;
}

/**
 * Verifies that the given string is not empty and only contains digits.
 * @param string - the string to verify.
 * @return - 1 if the given string is valid, else 0.
 */
int is_integer(char* string) {
    for (int i = 0; i < strlen(string); i++) {
        if (!isdigit(string[i])) {
            return 0;
        }
    }
    return 1;
}

/**
 * Initialises a semaphore lock to be used by pthreads.
 * @param lock - the semaphore to be initialised.
 */
void init_lock(sem_t* lock) {
    sem_init(lock, 0, 1);
}

/**
 * Allocates a lock to the calling pthread, such that other threads will block
 * if they call this function.
 * @param lock - the semaphore representing the lock.
 */
void take_lock(sem_t* lock) {
    sem_wait(lock);
}

/**
 * De-allocates a lock from the calling pthread, such that it is available for
 * other pthreads to take.
 * @param lock - the semaphore representing the lock.
 */
void release_lock(sem_t* lock) {
    sem_post(lock);
}
