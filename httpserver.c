#include <stdio.h>
#include <unistd.h>
#include <regex.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include "queue.h"
#include "rwlock.h"
#include "helper_funcs.h"

typedef struct RequestObj *Request;
typedef struct RequestObj {
    char *method;
    char *URI;
    char *version;
    char *content_length;
    char *request_id;
} RequestObj;
typedef struct FileLockStruct {
    rwlock_t *rwlock;
    char *URI;
    int count;
} FileLockStruct;
typedef struct {
    FileLockStruct *array;
    int size;
} FileLockArray;
FileLockStruct *newLockArray(int size) {
    FileLockStruct *fl_array = calloc(size, sizeof(FileLockStruct));
    for (int i = 0; i < size; i++) {
        fl_array[i].rwlock = rwlock_new(N_WAY, 1);
        fl_array[i].URI = NULL;
        fl_array[i].count = 0;
    }
    return fl_array;
}
int find_emptyPos(FileLockStruct *array, int size) {
    for (int i = 0; i < size; i++) {
        if (array[i].count == 0) {
            return i;
        }
    }
    return -1;
}
int find_filePos(FileLockStruct *array, char *URI, int size) {
    for (int i = 0; i < size; i++) {
        if (array[i].URI != NULL && strcmp(array[i].URI, URI) == 0) {
            return i;
        }
    }
    return -1;
}
int add_fileLock(FileLockStruct *array, char *URI, int size) {
    int pos = find_filePos(array, URI, size);
    if (pos != -1) {
        array[pos].count++;
    } else {
        pos = find_emptyPos(array, size);
        array[pos].count++;
        array[pos].URI = calloc(strlen(URI), sizeof(char));
        strcpy(array[pos].URI, URI);
    }
    return pos;
}
void remove_fileLock(FileLockStruct *array, char *URI, int size) {
    int pos = find_filePos(array, URI, size);
    if (pos != -1) {
        array[pos].count--;
        if (array[pos].count == 0) {
            free(array[pos].URI);
            array[pos].URI = NULL;
        }
    } else {
        fprintf(stderr, "Fatal error: Can't remove nonexistent file lock.\n");
        exit(1);
    }
}
void reader_file_lock(FileLockStruct *array, char *URI, int size) {
    int pos = add_fileLock(array, URI, size);
    reader_lock(array[pos].rwlock);
}
void reader_file_unlock(FileLockStruct *array, char *URI, int size) {
    int pos = find_filePos(array, URI, size);
    reader_unlock(array[pos].rwlock);
    remove_fileLock(array, URI, size);
}
void writer_file_lock(FileLockStruct *array, char *URI, int size) {
    int pos = add_fileLock(array, URI, size);
    writer_lock(array[pos].rwlock);
}
void writer_file_unlock(FileLockStruct *array, char *URI, int size) {
    int pos = find_filePos(array, URI, size);
    writer_unlock(array[pos].rwlock);
    remove_fileLock(array, URI, size);
}
//global file lock array
FileLockArray fl_array;

Request newRequest() {
    Request R;
    R = malloc(sizeof(RequestObj));
    R->method = NULL;
    R->URI = NULL;
    R->version = NULL;
    R->content_length = NULL;
    R->request_id = NULL;
    return (R);
}
void freeRequest(Request *pR) {
    if (pR != NULL && *pR != NULL) {
        if ((*pR)->method != NULL) {
            free((*pR)->method);
        }
        if ((*pR)->URI != NULL) {
            free((*pR)->URI);
        }
        if ((*pR)->version != NULL) {
            free((*pR)->version);
        }
        if ((*pR)->content_length != NULL) {
            free((*pR)->content_length);
        }
        free(*pR);
        *pR = NULL;
    }
}
ssize_t parseRequest(char *requestBuffer, Request request, int *status_code) {
    const char *re = "^([a-zA-Z]{0,8}) (/[a-zA-Z0-9.-]{1,63}) (HTTP/[0-9]\\.[0-9])\r\n(.|\n)*$";
    regex_t regex;
    regmatch_t pmatch[5];
    int *sc_ptr = status_code;
    if (regcomp(&regex, re, REG_EXTENDED | REG_NEWLINE)) {
        *sc_ptr = 500;
        return -1;
    }
    if (regexec(&regex, requestBuffer, 5, pmatch, 0)) {
        *sc_ptr = 400;
        return -1;
    }
    request->method = calloc(pmatch[1].rm_eo - pmatch[1].rm_so + 1, sizeof(char));
    request->URI = calloc(pmatch[2].rm_eo - pmatch[2].rm_so + 1, sizeof(char));
    request->version = calloc(pmatch[3].rm_eo - pmatch[3].rm_so + 1, sizeof(char));

    strncpy(request->method, requestBuffer + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
    strncpy(
        request->URI, requestBuffer + pmatch[2].rm_so + 1, pmatch[2].rm_eo - pmatch[2].rm_so - 1);
    strncpy(request->version, requestBuffer + pmatch[3].rm_so, pmatch[3].rm_eo - pmatch[3].rm_so);

    ssize_t request_bytes
        = strlen(request->method) + strlen(request->URI) + strlen(request->version) + 3 + 2;
    regfree(&regex);

    return (request_bytes);
}
void shiftBuffer(char *buf, ssize_t max_size, ssize_t shift) {
    //Shifts a buffer by shift amount and fill up the buffer with data from socket
    ssize_t remaining_bytes = max_size - shift;
    char tempbuf[remaining_bytes];
    memcpy(tempbuf, buf + shift, remaining_bytes);
    memset(buf, '\0', max_size);
    memcpy(buf, tempbuf, remaining_bytes);
}
void getContentLength(char *headerBuffer, Request request, int *status_code) {
    char *cl_pointer = strstr(headerBuffer, "Content-Length: ");
    int content_length = 0;
    if (cl_pointer != NULL) {
        sscanf(cl_pointer, "Content-Length: %d", &content_length);
    }
    char cl_string[10];
    sprintf(cl_string, "%d", content_length);
    request->content_length = calloc(10, sizeof(char));
    strcpy(request->content_length, cl_string);
    char *newline_pointer = strstr(headerBuffer, "\r\n\r\n");
    if (newline_pointer == NULL) {
        *status_code = 500;
    } else {
        shiftBuffer(headerBuffer, 2048, newline_pointer - headerBuffer + 4);
    }
}
void getRequestID(char *headerBuffer, Request request) {
    char *cl_pointer = strstr(headerBuffer, "Request-Id: ");
    int request_id = 0;
    if (cl_pointer != NULL) {
        sscanf(cl_pointer, "Request-Id: %d", &request_id);
    }
    char id_string[10];
    sprintf(id_string, "%d", request_id);
    request->request_id = calloc(10, sizeof(char));
    strcpy(request->request_id, id_string);
}
int getRequest(Request request, int *status_code) {
    if (strcmp(request->version, "HTTP/1.1") != 0) {
        *status_code = 505;
        return -1;
    }
    int fd = open(request->URI, O_RDONLY);
    if (fd == -1) {
        *status_code = 404;
        return -1;
    }
    char testbuf[1];
    int l = read_n_bytes(fd, testbuf, 1); //Try reading to see if valid file
    if (l == -1) {
        *status_code = 403;
        return -1;
    }
    int content_length = lseek(fd, 0, SEEK_END);

    close(fd);
    *status_code = 200;
    return (content_length);
}
int putRequest(char *messageBuffer, int socket, Request request, int *status_code) {
    if (strcmp(request->version, "HTTP/1.1") != 0) {
        *status_code = 505;
        return -1;
    }
    *status_code = 200;
    int fd = open(request->URI, O_WRONLY | O_TRUNC, 0);
    if (fd == -1) {
        *status_code = 201;
        fd = creat(request->URI, 0666);
        if (fd == -1) {
            *status_code = 500;
            return -1;
        }
    }
    //Need to write remainder bytes after parsing header fields
    size_t content_length_num = atoi(request->content_length);
    size_t bytes_written = write_n_bytes(fd, messageBuffer, strlen(messageBuffer));
    pass_n_bytes(socket, fd, content_length_num - bytes_written);
    close(fd);
    return 0;
}
int messagebufEmpty(char *messageBuffer) {
    for (size_t i = 0; i < 2048; i++) {
        if (messageBuffer[i] != 0) {
            return 0;
        }
    }
    return 1;
}
void audit_log(Request request, int *status_code) {
    fprintf(
        stderr, "%s,%s,%d,%s\n", request->method, request->URI, *status_code, request->request_id);
}
void response(int socket, Request request, int *status_code, int content_length) {
    char response[2048];
    if (strcmp(request->version, "HTTP/1.1") != 0) {
        *status_code = 505;
    }
    char sc_string[10];
    sprintf(sc_string, "%d ", *status_code);
    char status_phrase[30];
    if (*status_code == 200) {
        strcpy(status_phrase, "OK\0");
    } else if (*status_code == 201) {
        strcpy(status_phrase, "Created\0");
    } else if (*status_code == 400) {
        strcpy(status_phrase, "Bad Request\0");
    } else if (*status_code == 403) {
        strcpy(status_phrase, "Forbidden\0");
    } else if (*status_code == 404) {
        strcpy(status_phrase, "Not Found\0");
    } else if (*status_code == 500) {
        strcpy(status_phrase, "Internal Server Error\0");
    } else if (*status_code == 501) {
        strcpy(status_phrase, "Not Implemented\0");
    } else if (*status_code == 505) {
        strcpy(status_phrase, "Version Not Supported\0");
    }
    if (strcmp(request->method, "PUT") == 0 || content_length == -1) {
        content_length = strlen(status_phrase) + 1;
    }
    char cl_string[10];
    sprintf(cl_string, "%d", content_length); //Content Length
    //Message Body
    int response_length;
    if (strcmp(request->method, "GET") == 0 && *status_code == 200) {
        response_length
            = snprintf(response, sizeof(response), "%s%s%s\r\nContent-Length: %d\r\n\r\n",
                "HTTP/1.1 ", sc_string, status_phrase, content_length);
        write_n_bytes(socket, response, response_length);
        int fd = open(request->URI, O_RDONLY, 0);
        pass_n_bytes(fd, socket, content_length);
        close(fd);
        audit_log(request, status_code);
    } else {
        response_length
            = snprintf(response, sizeof(response), "%s%s%s\r\nContent-Length: %d\r\n\r\n%s\n",
                "HTTP/1.1 ", sc_string, status_phrase, content_length, status_phrase);
        write_n_bytes(socket, response, response_length);
        audit_log(request, status_code);
    }
}
void *server_thread(void *arg) {
    queue_t *request_queue = (queue_t *) arg;
    while (1) {
        int *socket_pointer;
        queue_pop(request_queue, (void **) &socket_pointer);
        int socket = *socket_pointer;
        int *status_code = malloc(sizeof(int));
        char *requestBuffer = calloc(2048, sizeof(char));
        char *headerBuffer = calloc(2048, sizeof(char));
        char *messageBuffer = calloc(2048, sizeof(char));
        char *headerBufferCopy = calloc(2048, sizeof(char));
        read_until(socket, requestBuffer, 2048, "\r\n\r\n"); //read in request-line + remainder
        Request request = newRequest();
        int request_bytes = parseRequest(requestBuffer, request, status_code);
        if (request_bytes == -1) { //If parsing fails, produce a response and nothing else
            request->method = calloc(4, sizeof(char));
            strcpy(request->method, "NONE");
            request->version = calloc(8, sizeof(char));
            strcpy(request->version, "HTTP/1.1");
            response(socket, request, status_code, -1);
        } else { //If parsing doesn't fail, continue
            shiftBuffer(requestBuffer, 2048, request_bytes);
            memcpy(headerBuffer, requestBuffer, 2048);
            memcpy(headerBufferCopy, headerBuffer, 2048);
            getContentLength(headerBuffer, request, status_code);
            getRequestID(headerBufferCopy, request);
            memcpy(messageBuffer, headerBuffer, 2048);

            if (strcmp(request->method, "GET") == 0) {
                if (messagebufEmpty(messageBuffer) == 1) {
                    reader_file_lock(fl_array.array, request->URI, fl_array.size);
                    int file_length = getRequest(request, status_code);
                    response(socket, request, status_code, file_length);
                    reader_file_unlock(fl_array.array, request->URI, fl_array.size);
                } else {
                    *status_code = 400;
                    response(socket, request, status_code, -1);
                }
            } else if (strcmp(request->method, "PUT") == 0) {
                writer_file_lock(fl_array.array, request->URI, fl_array.size);
                putRequest(messageBuffer, socket, request, status_code);
                response(socket, request, status_code, -1);
                writer_file_unlock(fl_array.array, request->URI, fl_array.size);
            } else {
                *status_code = 501;
                response(socket, request, status_code, -1);
            }
        }
        char *garbage_buf[2048];
        int garbage_bytes = 1;
        while (garbage_bytes != 0) {
            garbage_bytes = read(socket, garbage_buf, 2048);
        }
        freeRequest(&request);
        free(status_code);
        free(requestBuffer);
        free(headerBuffer);
        free(headerBufferCopy);
        free(messageBuffer);
        close(socket);
    }
}
void process_args(int argc, char **argv, int *num_threads, int *port_number) {
    getopt(argc, argv, "t:");
    if (argc == 4) {
        if (optarg == NULL) {
            *num_threads = 4;
        } else {
            *num_threads = atoi(optarg);
        }
        if (argv[optind] == NULL) {
            fprintf(stderr, "Invalid command\n");
            exit(1);
        } else {
            *port_number = atoi(argv[optind]);
        }
    } else if (argc == 2) {
        *num_threads = 4;
        if (optind == 1) {
            *port_number = atoi(argv[optind]);
        } else {
            fprintf(stderr, "Invalid command\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "Invalid command\n");
        exit(1);
    }
}
int main(int argc, char **argv) {
    int num_threads = 0;
    int port_number = 0;

    process_args(argc, argv, &num_threads, &port_number);

    if (port_number < 1 || port_number > 65536) {
        fprintf(stderr, "Invalid Port\n");
        exit(1);
    }

    pthread_t threads[num_threads];
    queue_t *request_queue = queue_new(num_threads);
    fl_array.array = newLockArray(num_threads);
    fl_array.size = num_threads;

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, server_thread, (void *) request_queue);
    }
    Listener_Socket sock;
    listener_init(&sock, port_number);
    while (1) {
        int *socket_pointer = malloc(sizeof(int));
        *socket_pointer = listener_accept(&sock);
        queue_push(request_queue, socket_pointer);
    }
}
