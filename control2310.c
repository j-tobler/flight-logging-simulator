#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <ctype.h>
#include <zconf.h>

/**
 * Struct containing all arguments necessary to run a plane client in its own
 * pthread.
 */
typedef struct {
    /* The file descriptor of the client's socket */
    int fileDescriptor;
    /* The array of IDs of plane which have visited the control */
    char** planes;
    /* Pointer to an integer which represents the size of the planes array */
    int* numPlanes;
    /* This control's information */
    char* info;
    /* The lock shared amongst pthreads to prevent simultaneous interactions
     * with the same memory */
    sem_t* lock;
} PlanePackage;

char* read_line(FILE* stream);
int contains_invalid_characters(char* string);
int verify_message(char* string);
int is_integer(char* string);
int send_info_to_mapper(char* mapperPort, char* id, in_port_t controlPort);
void init_lock(sem_t* lock);
void take_lock(sem_t* lock);
void release_lock(sem_t* lock);
void* client_handler(void* var);
void add_plane(char* plane, char** planes, int* numPlanes);
int listen_on_ephemeral_port();
in_port_t get_port_number(int fileDescriptor);
int is_valid_port_number(char* port);
void accept_clients(PlanePackage defaultPackage, int serverFileDescriptor,
        size_t maxPlanes);

/* The maximum permitted size of messages sent and received via network
 * communications */
#define MAX_CHARS 79

int main(int argc, char** argv) {
    // the maximum number of planes this control can connect to
    size_t maxPlanes = 1000;

    /* Verify args */
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: control2310 id info [mapper]\n");
        exit(1);
    }
    char* id = argv[1];
    char* info = argv[2];
    if (contains_invalid_characters(id) || contains_invalid_characters(info)) {
        fprintf(stderr, "Invalid char in parameter\n");
        exit(2);
    }
    char* mapperPort = NULL;
    if (argc == 4) {
        mapperPort = argv[3];
        if (!is_valid_port_number(mapperPort)) {
            fprintf(stderr, "Invalid port\n");
            exit(3);
        }
    }

    /* Initialise thread locks and plane ports array */
    sem_t lock;
    init_lock(&lock);
    int numPlanes = 0;
    char** planes = malloc(maxPlanes * sizeof(char*));

    /* Begin listening on an ephemeral port, and print that port to stdout */
    int serverFileDescriptor = listen_on_ephemeral_port();
    in_port_t controlPort = get_port_number(serverFileDescriptor);
    printf("%u\n", controlPort);
    fflush(stdout);

    /* If a mapper is given, register the ID and port number of this airport */
    if (mapperPort) {
        if (send_info_to_mapper(mapperPort, id, controlPort) == -1) {
            fprintf(stderr, "Can not connect to map\n");
            exit(4);
        }
    }

    /* Begin accepting and handling clients */
    PlanePackage defaultPlanePackage = {0, planes, &numPlanes, info, &lock};
    accept_clients(defaultPlanePackage, serverFileDescriptor, maxPlanes);
    return 0;
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
        perror("sockname\n");
        return -1;
    }
    return ntohs(socketAddress.sin_port);
}

/**
 * Attempts to connect to a mapper through the given port and write to it the
 * id of this control and the port number it is listening on.
 * @param mapperPort - the port through which to connect to the mapper.
 * @param id - the id of this control.
 * @param controlPort - the port number this control is listening on.
 * @return - 0 on success, else -1 if an error occurred.
 */
int send_info_to_mapper(char* mapperPort, char* id, in_port_t controlPort) {
    /* Retrieve address info */
    struct addrinfo* addressInfo = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP
    if (getaddrinfo("localhost", mapperPort, &hints, &addressInfo)) {
        freeaddrinfo(addressInfo);
        return -1;
    }
    /* Create socket */
    int mapperSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(mapperSocket, addressInfo->ai_addr, sizeof(struct sockaddr))) {
        return -1;
    }
    /* Print information to socket */
    FILE* stream = fdopen(mapperSocket, "w");
    fprintf(stream, "!%s:%u\n", id, controlPort);
    fclose(stream);
    freeaddrinfo(addressInfo);
    return 0;
}

/**
 * Loops forever , accepting pending clients and allocating each of them a
 * pthread to another function.
 * @param defaultPackage - a struct containing the variables required to run
 * the pthread for each client, except for the file descriptor of the client.
 * @param serverFileDescriptor - the file descriptor of the socket on which
 * this control is listening for clients.
 * @param maxPlanes - the maximum number of connections permitted.
 */
void accept_clients(PlanePackage defaultPackage, int serverFileDescriptor,
        size_t maxPlanes) {
    for (int i = 0; i < maxPlanes; i++) {
        /* Wait for a client to connect */
        int* clientFileDescriptor = malloc(sizeof(int));
        if (*clientFileDescriptor = accept(serverFileDescriptor, 0, 0),
                *clientFileDescriptor >= 0) {
            /* Client connected; create a new pthread for them and loop back */
            PlanePackage* planePackage = malloc(sizeof(PlanePackage));
            planePackage->planes = defaultPackage.planes;
            planePackage->numPlanes = defaultPackage.numPlanes;
            planePackage->lock = defaultPackage.lock;
            planePackage->info = defaultPackage.info;
            planePackage->fileDescriptor = *clientFileDescriptor;
            pthread_t* threadID = malloc(sizeof(threadID));
            pthread_create(threadID, 0, client_handler, planePackage);
        }
    }
}

/**
 * Function for handling client connections as established as pthreads.
 * Continuously attempts to read plane IDs from the client and store them in
 * the array contained in the given package. When "log" is read, sends back
 * this array followed by a period, closes the connection and returns.
 * @param var - A void pointer which may be casted to a PlanePackage pointer
 * for retrieval of function arguments as specified in the documentation for
 * PlanePackage.
 * @return - NULL on completion.
 */
void* client_handler(void* var) {
    /* Unpack variables */
    PlanePackage planePackage = *(PlanePackage*)var;
    int fileDescriptor = planePackage.fileDescriptor;
    char** planes = planePackage.planes;
    int* numPlanes = planePackage.numPlanes;
    char* info = planePackage.info;
    sem_t* lock = planePackage.lock;
    /* Create read and write streams from the given file descriptor */
    int fileDescriptorCopy = dup(fileDescriptor);
    FILE* readStream = fdopen(fileDescriptor, "r");
    FILE* writeStream = fdopen(fileDescriptorCopy, "w");

    while (1) {
        /* Retrieve and verify input */
        size_t bufferSize = MAX_CHARS + 1; // +1 to include null terminator
        char* id = malloc(bufferSize * sizeof(char));
        if (!fgets(id, bufferSize, readStream)) {
            break; // error reading from plane; close connection
        }
        if (!verify_message(id)) {
            continue; // id is invalid; ignore
        }
        id[strlen(id) - 1] = 0; // truncate trailing '\n'

        /* Process input: if "log" was received, display the plane's log
         * followed by a period and close connection.
         * Otherwise, send the control's information and log the input */
        take_lock(lock);
        if (strcmp("log", id) == 0) {
            for (int i = 0; i < *numPlanes; i++) {
                fprintf(writeStream, "%s\n", planes[i]);
            }
            fprintf(writeStream, ".\n");
            fflush(writeStream);
            break;
        } else {
            fprintf(writeStream, "%s\n", info);
            fflush(writeStream);
            add_plane(id, planes, numPlanes);
        }
        release_lock(lock);
    }
    fclose(readStream);
    fclose(writeStream);
    return NULL;
}

/**
 * Inserts the given plane into the given array of planes at an index
 * sufficient for preserving lexicographic ordering of the array contents.
 * Note: this function manipulates the index of planes initially present in the
 * array.
 * @param plane - the plane to be added to the array.
 * @param planes - the array to which to allocate the plane.
 * @param numPlanes - a pointer to an integer representing the number of planes
 * in given array.
 */
void add_plane(char* plane, char** planes, int* numPlanes) {
    /* Find appropriate index */
    int insertionIndex = 0;
    while (insertionIndex < *numPlanes &&
            strcmp(plane, planes[insertionIndex]) >= 0) {
        insertionIndex++;
    }
    /* Shift all airports with index >= this index one position to the right */
    for (int i = *numPlanes - 1; i >= insertionIndex; i--) {
        planes[i + 1] = planes[i];
    }
    /* Insert airport into this index */
    planes[insertionIndex] = plane;
    (*numPlanes)++;
}

/**
 * Verifies whether the given string represents a valid port number.
 * Valid port numbers are defined as integers between 1 and 65535 inclusive.
 * @param port - the string to verify.
 * @return - 1 if the given string is valid, else 0.
 */
int is_valid_port_number(char* port) {
    if (!is_integer(port)) {
        return 0;
    }
    int portInt = atoi(port);
    if (portInt < 1 || portInt > 65535) {
        return 0;
    }
    return 1;
}

/**
 * Verifies null-terminated text read over a network. Valid text must:
 * 1. Be newline terminated.
 * 2. Be non-empty (contain at least one char before the newline terminator).
 * 3. Contain none of the following chars: '\n' (but for rule 1), '\r', ':'.
 * @param string - the string to verify.
 * @return - 1 if the given string is valid, else 0.
 */
int verify_message(char* string) {
    size_t len = strlen(string);
    if (len < 2) {
        return 0; // text is not newline terminated, or is empty
    }
    if (string[len - 1] != '\n') {
        return 0; // text is not newline terminated
    }
    for (int i = 0; i < len - 1; i++) {
        if (string[i] == '\n' || string[i] == '\r' || string[i] == ':') {
            return 0; // text contains invalid chars
        }
    }
    return 1;
}

/**
 * Checks if the given null-terminated string contains any of the following
 * characters: '\n', '\r', ':'.
 * @param string - the string to examine.
 * @return - 1 if the string contains any of the specified characters, else 0.
 */
int contains_invalid_characters(char* string) {
    for (int i = 0; i < strlen(string); i++) {
        if (string[i] == '\n' || string[i] == '\r' || string[i] == ':') {
            return 1; // text contains invalid chars
        }
    }
    return 0;
}

/**
 * Verifies that the given string is not empty and only contains digits.
 * @param string - the string to verify.
 * @return - 1 if the given string is valid, else 0.
 */
int is_integer(char* string) {
    int i;
    for (i = 0; i < strlen(string); i++) {
        if (!isdigit(string[i])) {
            return 0;
        }
    }
    return i > 0;
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
