/**
 * webserver.c -- A webserver written in C
 * 
 * Test with curl (if you don't have it, install it):
 * 
 *    curl -D - http://localhost:3490/
 *    curl -D - http://localhost:3490/d20
 *    curl -D - http://localhost:3490/date
 * 
 * You can also test the above URLs in your browser! They should work!
 * 
 * Posting Data:
 * 
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!' http://localhost:3490/save
 * 
 * (Posting data is harder to test from a browser.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include "net.h"
#include "file.h"
#include "mime.h"
#include "cache.h"
#include <stdlib.h>

#define PORT "80"  // the port users will be connecting to

#define SERVER_FILES "./serverfiles"
#define SERVER_ROOT "./serverroot"

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 * 
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, void *body, int content_length) {
    const int max_response_size = 262144; // 2**18
    char response[max_response_size];

    time_t rawtime;
    time(&rawtime);
    char* date = asctime(localtime(&rawtime));

    snprintf(response, max_response_size, 
        "%s\nDate: %sConnection: close\nContent-Length: %d\nContent-Type: %s\n\n%s",
          header,   date,                   content_length,     content_type, (char*)body);

    int response_length = strlen(response);

    // Send it all!
    int rv = send(fd, response, response_length, 0);

    if (rv < 0) {
        perror("send");
    }

    return rv;
}

/**
 * Send a 404 response
 */
void resp_404(int fd) {
    char filepath[4096];
    struct file_data *filedata; 
    char *mime_type;

    // Fetch the 404.html file
    snprintf(filepath, sizeof filepath, "%s/404.html", SERVER_FILES);
    filedata = file_load(filepath);

    if (filedata == NULL) {
        // TODO: make this non-fatal
        fprintf(stderr, "cannot find system 404 file\n");
        exit(3);
    }

    mime_type = mime_type_get(filepath);

    send_response(fd, "HTTP/1.1 404 NOT FOUND", mime_type, filedata->data, filedata->size);

    file_free(filedata);
}

/**
 * Read and return a file from disk or cache
 */
void resp_file(int fd, struct cache *cache, char *request_path) {
    char filepath[4096];
    struct file_data *filedata; 
    char *mime_type;

    // Fetch the 404.html file
    snprintf(filepath, sizeof filepath, "%s%s", SERVER_ROOT, request_path);
    filedata = file_load(filepath);

    if (filedata == NULL) {
        // TODO: make this non-fatal
        fprintf(stderr, "cannot find system %s file\n", request_path);
        exit(3);
    }

    mime_type = mime_type_get(filepath);

    send_response(fd, "HTTP/1.1 200 OK", mime_type, filedata->data, filedata->size);

    file_free(filedata);
}

/**
 * @brief handle get request
 * 
 * @param fd socket to send the answer to
 * @param cache cache
 * @param path path requested
 * @param request entire requested message
 */
void handle_get_request(int fd, struct cache *cache, char* path, char* request) {
    printf("GET: %s", path);

    char prof[100] = {0};
    if (sscanf(path, "/profile/%s", prof) == 1) {
        // serve profile
        resp_file(fd, cache, "/profile.html");
    } else {
        // serve homepage
        resp_file(fd, cache, "/index.html");
    }
}

/**
 * @brief handle post request
 * 
 * @param fd socket to send the answer to
 * @param cache cache
 * @param path path requested
 * @param request entire requested message
 */
void handle_post_request(int fd, struct cache *cache, char* path, char* request) {
    printf("POST: %s", request);
    resp_404(fd);
}

/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd, struct cache *cache) {
    const int request_buffer_size = 65536; // 64K
    char request[request_buffer_size];

    // Read request
    int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

    if (bytes_recvd < 0) {
        perror("recv");
        return;
    }

    char type[10];
    char path[107];


    // Read the first two components of the first line of the request
    sscanf(request, "%s %s", type, path);
 
    if (strcmp(type, "GET") == 0) {
        // If GET, handle the get endpoints
        handle_get_request(fd, cache, path, request);
    } else if (strcmp(type, "POST") == 0){
        // If POST, handle the get endpoints
        handle_post_request(fd, cache, path, request);
    } else {
        // Otherwise, 404 Error
        resp_404(fd);
    }
}

/**
 * Main
 */
int main(void) {
    int newfd;  // listen on sock_fd, new connection on newfd
    struct sockaddr_storage their_addr; // connector's address information
    char s[INET6_ADDRSTRLEN];

    struct cache *cache = cache_create(10, 0);

    // Get a listening socket
    int listenfd = get_listener_socket(PORT);

    if (listenfd < 0) {
        fprintf(stderr, "webserver: fatal error getting listening socket\n");
        exit(1);
    }

    printf("webserver: waiting for connections on port %s...\n", PORT);

    // This is the main loop that accepts incoming connections and
    // responds to the request. The main parent process
    // then goes back to waiting for new connections.
    
    while(1) {
        socklen_t sin_size = sizeof their_addr;

        // Parent process will block on the accept() call until someone
        // makes a new connection:
        newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
        if (newfd == -1) {
            perror("accept");
            continue;
        }

        // Print out a message that we got the connection
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);

        // newfd is a new socket descriptor for the new connection.
        // listenfd is still listening for new connections.

        handle_http_request(newfd, cache);

        close(newfd);
    }

    // Unreachable code

    return 0;
}

