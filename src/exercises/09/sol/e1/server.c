/*
 * This is part of the ninth session.
 *
 * Goals:
 * - Understand the characteristics of a Simple HTTP Server.
 * - Recap TCP/IP Sockets: socket(), bind(), listen(), accept().
 * - Understand the basic structure of the HTTP/1.1 Protocol (RFC 1945).
 * - Implement a Nano HTTP Server capable of parsing GET requests, 
 * determining MIME types, decoding URLs, finding files, and building 
 * proper HTTP responses.
 *
 * Exercise 1 - Simple HTTP server (multi-threaded)
 *
 * The server listens on a specified port (8080). For each incoming 
 * connection, it spawns a detached worker thread to handle the client.
 * 
 * The worker thread:
 * 1. Reads the HTTP request.
 * 2. Uses regex to verify if it is a valid GET request and extracts the URL.
 * 3. Decodes the URL (e.g., translating "%20" to spaces).
 * 4. Determines the MIME type based on the file extension.
 * 5. Searches for the file case-insensitively in the local directory.
 * 6. Constructs the HTTP response (Header + Body). If the file is missing, 
 * it returns a "404 Not Found". If found, it returns "200 OK".
 * 7. Sends the response back to the client and closes the connection.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

#define PORT 8080
#define BUFFER_SIZE 104857600

/*
 * Get file extension
 *
 * Extracts the extension from a file name string.
 * Returns a pointer to the character immediately following the last dot.
 */
const char *get_file_extension(const char *file_name) {
    const char *dot = strrchr(file_name, '.');
    if (!dot || dot == file_name) {
        return "";
    }
    return dot + 1;
}

/*
 * Get MIME type
 *
 * Determines the appropriate MIME type string to include in the HTTP
 * Content-Type header based on the extracted file extension.
 */
const char *get_mime_type(const char *file_ext) {
    if (strcasecmp(file_ext, "html") == 0 || strcasecmp(file_ext, "htm") == 0) {
        return "text/html";
    } else if (strcasecmp(file_ext, "txt") == 0) {
        return "text/plain";
    } else if (strcasecmp(file_ext, "jpg") == 0 || strcasecmp(file_ext, "jpeg") == 0) {
        return "image/jpeg";
    } else if (strcasecmp(file_ext, "png") == 0) {
        return "image/png";
    } else {
        /* Fallback for unknown file types */
        return "application/octet-stream";
    }
}

/* Case-insensitive string comparison utility */
bool case_insensitive_compare(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
            return false;
        }
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

/*
 * Get file case insensitive
 *
 * Searches the current directory for a file matching the requested name,
 * ignoring case differences. Returns the exact matching filename as found 
 * in the directory structure.
 */
char *get_file_case_insensitive(const char *file_name) {
    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("opendir");
        return NULL;
    }

    struct dirent *entry;
    char *found_file_name = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (case_insensitive_compare(entry->d_name, file_name)) {
            found_file_name = entry->d_name;
            break;
        }
    }

    closedir(dir);
    return found_file_name;
}

/*
 * URL Decode
 *
 * Decodes URL-encoded strings (e.g., converting "%20" into a space character).
 * Allocates memory for the decoded string, which must be freed by the caller.
 */
char *url_decode(const char *src) {
    size_t src_len = strlen(src);
    char *decoded = malloc(src_len + 1);
    size_t decoded_len = 0;

    /* Decode %2x hex sequences */
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            int hex_val;
            sscanf(src + i + 1, "%2x", &hex_val);
            decoded[decoded_len++] = hex_val;
            i += 2;
        } else {
            decoded[decoded_len++] = src[i];
        }
    }

    /* Add null terminator */
    decoded[decoded_len] = '\0';
    return decoded;
}

/*
 * Build HTTP response
 *
 * Constructs the HTTP header and appends the requested file content.
 * Handles both "200 OK" (file found) and "404 Not Found" scenarios.
 */
void build_http_response(const char *file_name, const char *file_ext, char *response, size_t *response_len) {
    
    /* Build HTTP header */
    const char *mime_type = get_mime_type(file_ext);
    char *header = (char *)malloc(BUFFER_SIZE * sizeof(char));
    snprintf(header, BUFFER_SIZE, "HTTP/1.1 200 OK\r\n" "Content-Type: %s\r\n" "\r\n", mime_type);

    /* Check file existence: if file does not exist, response is 404 Not Found */
    int file_fd = open(file_name, O_RDONLY);
    if (file_fd == -1) {
        snprintf(response, BUFFER_SIZE,
                 "HTTP/1.1 404 Not Found\r\n"
                 "Content-Type: text/plain\r\n"
                 "\r\n"
                 "404 Not Found");
        *response_len = strlen(response);
        free(header);
        return;
    }

    /* Get file size for Content-Length (optional but good practice) */
    struct stat file_stat;
    fstat(file_fd, &file_stat);
    off_t file_size = file_stat.st_size;

    /* Copy header to response buffer */
    *response_len = 0;
    memcpy(response, header, strlen(header));
    *response_len += strlen(header);

    /* Copy file content to response buffer */
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, response + *response_len, BUFFER_SIZE - *response_len)) > 0) {
        *response_len += bytes_read;
    }
    
    free(header);
    close(file_fd);
}

/*
 * Client handler (thread function)
 *
 * Executed by a spawned worker thread. Reads the raw HTTP request, 
 * parses it to extract the desired file, builds the response, and 
 * sends it back to the client over the socket.
 */
void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));

    /* Receive request data from client and store into buffer */
    ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
    if (bytes_received > 0) {

        /*
         * Verify if the request is a valid GET method using Regular Expressions.
         *
         * Extracts the requested path (ignoring leading '/').
         */
        regex_t regex;
        regcomp(&regex, "^GET /([^ ]*) HTTP/1", REG_EXTENDED);
        regmatch_t matches[2];

        if (regexec(&regex, buffer, 2, matches, 0) == 0) {
            
            /* Extract filename from request and decode URL */
            buffer[matches[1].rm_eo] = '\0';
            const char *url_encoded_file_name = buffer + matches[1].rm_so;
            char *file_name = url_decode(url_encoded_file_name);

            /* Get file extension */
            char file_ext[32];
            strcpy(file_ext, get_file_extension(file_name));

            /* Build HTTP response buffer (header + body) */
            char *response = (char *)malloc(BUFFER_SIZE * 2 * sizeof(char));
            size_t response_len;
            build_http_response(file_name, file_ext, response, &response_len);

            /* Send full HTTP response to the client */
            send(client_fd, response, response_len, 0);

            free(response);
            free(file_name);
        }
        regfree(&regex);
    }
    
    /* Close the connection and release resources */
    close(client_fd);
    free(arg);
    free(buffer);
    return NULL;
}

int main(int argc, char *argv[]) {
    int server_fd;
    struct sockaddr_in server_addr;
    int ret = 0; /* Used for return codes of system functions */

    /* Create server socket (IPv4, TCP) */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* Configure socket parameters */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /* Bind socket to port */
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    /* Listen for connections */
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);
    
    /* Main listening loop */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        /* Allocate memory for the client descriptor to safely pass it to the thread */
        int *client_fd = malloc(sizeof(int));

        /* Accept client connection */
        if ((*client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) < 0) {
            perror("accept failed");
            free(client_fd);
            continue;
        }

        /*
         * Spawn a new detached thread to handle the client request concurrently,
         * allowing the main thread to immediately return to accepting new connections.
         */
        pthread_t thread_id;
        ret = pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
        if (ret) perror("Could not create a new thread");
       
        ret = pthread_detach(thread_id);
        if (ret) perror("Could not detach the thread");
    }

    close(server_fd);
    return 0;
}
