#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <zconf.h>

char** create_log(char** ports, int numPorts, char* id, int* logSize,
        int* failedConnection);
void display_log(char** log, int logSize);
int connect_to_port(char* port);
int parse_to_port_numbers(char** airports, int numAirports, char* mapper);
char* read_line(FILE* stream);
int is_valid_port_number(char* port);
int verify_port_numbers(char** ports, int numPorts);
int verify_message(char* string);
int is_integer(char* string);

/* The maximum permitted size of messages sent and received via network
 * communications */
#define MAX_CHARS 79

int main(int argc, char** argv) {
    /* Verify args */
    if (argc < 3) {
        fprintf(stderr, "Usage: roc2310 id mapper {airports}\n");
        exit(1);
    }
    char* id = argv[1];
    char* mapper = argv[2];
    int mapperGiven = strcmp("-", mapper) != 0;
    if (mapperGiven && !is_valid_port_number(mapper)) {
        fprintf(stderr, "Invalid mapper port\n");
        exit(2);
    }
    /* Process airport IDs & port numbers into a list of valid port numbers */
    int numAirports = argc - 3;
    char* airports[numAirports];
    for (int i = 0; i < numAirports; i++) {
        airports[i] = argv[i + 3];
    }
    if (!mapperGiven) {
        if (!verify_port_numbers(airports, numAirports)) {
            fprintf(stderr, "Mapper required\n");
            fflush(stderr);
            exit(3);
        }
    } else {
        int result = parse_to_port_numbers(airports, numAirports, mapper);
        if (result == -1) {
            fprintf(stderr, "Failed to connect to mapper\n");
            fflush(stderr);
            exit(4);
        }
        if (result == -2) {
            fprintf(stderr, "No map entry for destination\n");
            fflush(stderr);
            exit(5);
        }
    }

    int failed = 0;
    int logSize = 0;
    char** log = create_log(airports, numAirports, id, &logSize, &failed);

    /* Display log and exit */
    display_log(log, logSize);
    if (failed) {
        fprintf(stderr, "Failed to connect to at least one destination\n");
        exit(6);
    }
    return 0;
}

/**
 * Connects to each given port in turn, writes the given id to it, reads back
 * a line of input from it, and stores that input in a log to be returned.
 * If a failure to connect to any port occurs, sets the integer pointed to by
 * failedConnection to 1.
 * Sets the integer pointed to by logSize to the size of the returned log.
 * @param ports - the array of ports to connect to.
 * @param numPorts - the number of ports to connect to.
 * @param id - the id to write to the port once connected, to request info.
 * @param logSize - pointer to the value to set to the size of the returned
 * log.
 * @param failedConnection - pointer to the value to set to 1 if any
 * connections fail.
 * @return - an array of all the information read from connections; the log.
 */
char** create_log(char** ports, int numPorts, char* id, int* logSize,
        int* failedConnection) {
    char** log = malloc(numPorts * sizeof(char*));
    *logSize = 0;
    for (int i = 0; i < numPorts; i++) {
        /* Connect to the port */
        int fileDescriptor = connect_to_port(ports[i]);
        if (fileDescriptor == -1) {
            *failedConnection = 1; // failed to connect to port
            continue;
        }
        int fileDescriptorCopy = dup(fileDescriptor);
        FILE* readStream = fdopen(fileDescriptor, "r");
        FILE* writeStream = fdopen(fileDescriptorCopy, "w");
        /* Get information from the port */
        fprintf(writeStream, "%s\n", id);
        fflush(writeStream);
        char* info = read_line(readStream);
        if (!info || !verify_message(info)) {
            // connection dropped, or info contains invalid text
            *failedConnection = 1;
            continue;
        }
        info[strlen(info) - 1] = 0; // truncate trailing '\n'
        log[(*logSize)++] = info;
        /* Disconnect from the port */
        fclose(readStream);
        fclose(writeStream);
    }
    return log;
}

/**
 * Prints the given log to stdout, newline separated.
 * @param log - the log to print.
 * @param logSize - the size of the log to print.
 */
void display_log(char** log, int logSize) {
    for (int i = 0; i < logSize; i++) {
        printf("%s\n", log[i]);
        fflush(stdout);
    }
}

/**
 * Tries to connect to the given port.
 * If successful, returns the file descriptor of the connected socket.
 * If unsuccessful, returns -1.
 * @param port - to connect to.
 * @return - the file descriptor of the connected socket, or -1 if connection
 * failed.
 */
int connect_to_port(char* port) {
    /* Retrieve address info of localhost */
    struct addrinfo* addressInfo = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP
    if (getaddrinfo("localhost", port, &hints, &addressInfo)) {
        freeaddrinfo(addressInfo);
        return -1;
    }
    /* Create socket, connect to it and return its file descriptor */
    int fileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fileDescriptor, addressInfo->ai_addr,
            sizeof(struct sockaddr))) {
        return -1;
    }
    return fileDescriptor;
}

/**
 * Takes an array containing a combination of airport IDs and port numbers, and
 * attempts to use the given mapper to convert all airport IDs to their
 * corresponding port number.
 * mapper.
 * @param airports - the combined list of airport IDs and port numbers to
 * parse.
 * @param numAirports - the size of the combined list of IDs and port numbers.
 * @param mapper - the port which the mapper is listening on.
 * @return - 0 if successful, -1 if the connection to mapper failed, or -2 if
 * the mapper did not recognise an airport id.
 */
int parse_to_port_numbers(char** airports, int numAirports, char* mapper) {
    /* Connect to mapper */
    int fileDescriptor = connect_to_port(mapper);
    if (fileDescriptor == -1) {
        return -1; // failed connection
    }
    int fileDescriptorCopy = dup(fileDescriptor);
    FILE* readStream = fdopen(fileDescriptor, "r");
    FILE* writeStream = fdopen(fileDescriptorCopy, "w");
    /* For each airport id, use the mapper to convert to port if required */
    for (int i = 0; i < numAirports; i++) {
        if (!is_valid_port_number(airports[i])) {
            // request port number from mapper for this id
            fprintf(writeStream, "?%s\n", airports[i]);
            fflush(writeStream);
            char* portNumber = read_line(readStream);
            if (!portNumber || !verify_message(portNumber)) {
                return -2; // reading error, or invalid text in mapper output
            }
            portNumber[strlen(portNumber) - 1] = 0; // truncate trailing '\n'
            if (strcmp(portNumber, ";") == 0) {
                return -2; // no map entry for airport
            }
            // assume whatever else mapper returned is a valid port number
            airports[i] = portNumber;
        }
    }
    return 0;
}

/**
 * Reads the smallest of: a line of input from the specified stream, or the
 * globally specified maximum number of characters permitted in network
 * communications.
 * @param stream - from which to read the line of input.
 * @return - a null-terminated string, or NULL if a reading error occurred.
 */
char* read_line(FILE* stream) {
    size_t bufferSize = MAX_CHARS + 1; // +1 to include null terminator
    char* buffer = malloc(bufferSize * sizeof(char));
    return fgets(buffer, bufferSize, stream);
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
 * Verifies whether every string in the given array represents a valid port
 * number (see @is_valid_port_number).
 * @param ports - the array of strings to verify.
 * @param numPorts - the size of the array of strings to verify.
 * @return - 1 if every string in the given array is valid, else 0.
 */
int verify_port_numbers(char** ports, int numPorts) {
    for (int i = 0; i < numPorts; i++) {
        if (!is_valid_port_number(ports[i])) {
            return 0;
        }
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
