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


        /*************************************************************************
         * 
         * 4 Risposta: Estensione del file e tipo MIME
         *
         *************************************************************************/

const char *get_file_extension(const char *file_name) {
    const char *dot = strrchr(file_name, '.');
    if (!dot || dot == file_name) {
        return "";
    }
    return dot + 1;
}
        /*************************************************************************
         * FINE - 4 Risposta: Estensione del file e tipo MIME
         *************************************************************************/


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
        return "application/octet-stream";
    }
}

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

        /*************************************************************************
         * 
         * 5 Risposta: Ricerca del file
         *
         *************************************************************************/

char *get_file_case_insensitive(const char *file_name) {
    DIR *dir = opendir(".");
    if (dir == NULL) {
        handle_error("opendir");
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
        /*************************************************************************
         * FINE - 5 Risposta: Ricerca del file
         *************************************************************************/


        /*************************************************************************
         * 
         * 3 Ascolto: Codifica ed effettivo nome del file
         *
         *************************************************************************/

char *url_decode(const char *src) {
    size_t src_len = strlen(src);
    char *decoded = malloc(src_len + 1);
    size_t decoded_len = 0;

    // decode %2x to hex
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

    // add null terminator
    decoded[decoded_len] = '\0';
    return decoded;
}
        /*************************************************************************
         * FINE 3 Ascolto: Codifica ed effettivo nome del file
         *************************************************************************/

        /*************************************************************************
         * 
         * 7 Risposta: Creazione Header HTTP
         *
         *************************************************************************/


void build_http_response(const char *file_name, 
                        const char *file_ext, 
                        char *response, 
                        size_t *response_len) {
    // build HTTP header
char *header;    
        /*************************************************************************
         * FINE - 7 Risposta: Creazione Header HTTP
         *************************************************************************/



        /*************************************************************************
         * COMPLETARE
         * 6 Risposta: Esistenza del file
         *
         *************************************************************************/
    int file_fd;

    // if file not exist, response is 404 Not Found
    
        /*************************************************************************
         * FINE 6 Risposta: Esistenza del file
         *************************************************************************/

    // get file size for Content-Length
    struct stat file_stat;
    fstat(file_fd, &file_stat);
    off_t file_size = file_stat.st_size;

    // copy header to response buffer
    *response_len = 0;
    memcpy(response, header, strlen(header));
    *response_len += strlen(header);

    // copy file to response buffer
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, 
                            response + *response_len, 
                            BUFFER_SIZE - *response_len)) > 0) {
        *response_len += bytes_read;
    }
    free(header);
    close(file_fd);
}

void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    ssize_t ret; // used for return codes of system functions
    int recv_bytes = 0;
    int bytes_sent = 0;
    
    // receive request data from client and store into buffer
    do {
        ret = recv(client_fd, buffer + recv_bytes, BUFFER_SIZE - recv_bytes - 1, 0);
        if (ret == -1 && errno == EINTR) continue;   // syscall interrotta da segnale
        if (ret == -1) handle_error("Cannot read from the socket"); // altro tipo di errore, il server web termina
        if (ret == 0) break;      // peer ha chiuso la connessione
        recv_bytes += ret; // aggiornamento dei byte ricevuti
    } while (recv_bytes < BUFFER_SIZE - 1 && buffer[recv_bytes - 1] != '\n');

    if (recv_bytes == 0) {
        // nessun dato ricevuto
        close(client_fd);
        free(buffer);
        free(arg);
        return NULL;
    }

    if (recv_bytes > 0) {
        buffer[recv_bytes] = '\0'; // aggiungo terminatore

        /*************************************************************************
         * 
         * 2 Ascolto: Verificare se GET valida
         *
         *************************************************************************/
        // check if request is GET
        regex_t regex;
        regcomp(&regex, "^GET /([^ ]*) HTTP/1", REG_EXTENDED);
        regmatch_t matches[2];

        /*************************************************************************
         * FINE - 2 Ascolto: Verificare se GET valida
         *************************************************************************/


        if (regexec(&regex, buffer, 2, matches, 0) == 0) {
            // extract filename from request and decode URL
            buffer[matches[1].rm_eo] = '\0';
            const char *url_encoded_file_name = buffer + matches[1].rm_so;
            char *file_name = url_decode(url_encoded_file_name);

            // get file extension
            char file_ext[32];
            strcpy(file_ext, get_file_extension(file_name));

            // build HTTP response
            char *response = (char *)malloc(BUFFER_SIZE * 2 * sizeof(char));
            size_t response_len;
            build_http_response(file_name, file_ext, response, &response_len);
        /*************************************************************************
         * COMPLETARE
         * 8 Risposta: Invio messaggio HTTP
         * 
         *************************************************************************/

            // send HTTP response to client

        /*************************************************************************
         * FINE - 8 Risposta: Invio messaggio HTTP
         *************************************************************************/

            free(response);
            free(file_name);
        }
        regfree(&regex);
    }
    close(client_fd);
    free(arg);
    free(buffer);
    return NULL;
}

int main(int argc, char *argv[]) {
    int server_fd;
    struct sockaddr_in server_addr;
    int ret = 0; //used for return codes of system functions 

    // create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        handle_error("socket failed");
    }

    // config socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // bind socket to port
    if (bind(server_fd, 
            (struct sockaddr *)&server_addr, 
            sizeof(server_addr)) < 0) {
        handle_error("bind failed");
    }

    // listen for connections
    if (listen(server_fd, 10) < 0) {
        handle_error("listen failed");
    }

    printf("Server listening on port %d\n", PORT);
    while (1) {
        // client info
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));

        // accept client connection
        if ((*client_fd = accept(server_fd, 
                                (struct sockaddr *)&client_addr, 
                                &client_addr_len)) < 0) {
            handle_error("accept failed");
            continue;
        }

        /*************************************************************************
         * COMPLETARE
         * 1 Ascolto - Elaborare le richieste dei client (thread separati)
         *
         *************************************************************************/
        // create a new thread to handle client request
        pthread_t thread_id;
        /*************************************************************************
         * FINE - Elaborare le richieste dei client (thread separati)
         *************************************************************************/

       
        ret = pthread_detach(thread_id);
        if (ret) handle_error_en (ret, "Could not detach the thread");
    }

    close(server_fd);
    return 0;
}