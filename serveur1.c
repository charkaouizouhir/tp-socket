#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define PORT 3638
#define MAXDATASIZE 102400
#define TIMEOUT_SEC 2

void list_files(int clientfd) {
    DIR *d;
    struct dirent *dir;
    char buffer[MAXDATASIZE];
    memset(buffer, 0, sizeof(buffer));  

    d = opendir(".");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {  
                strcat(buffer, dir->d_name);
                strcat(buffer, "\n");
            }
        }
        closedir(d);
    }
    send(clientfd, buffer, strlen(buffer), 0);
    send(clientfd, "EOF", 3, 0);

    memset(buffer, 0, sizeof(buffer));  
}

void send_file(int clientfd, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("fopen");
        send(clientfd, "ERROR: File not found.\n", 23, 0);
        return;
    }

    send(clientfd, "READY", 5, 0);
    printf("[INFO] Sending file: %s\n", filename);

    char buffer[MAXDATASIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(clientfd, buffer, bytes_read, 0) == -1) {
            perror("send");
            fclose(file);
            return;
        }
        printf("[INFO] Sent %zu bytes\n", bytes_read);
        memset(buffer, 0, sizeof(buffer)); 
    }

    strcpy(buffer, "EOF");
    send(clientfd, buffer, strlen(buffer), 0);

    fclose(file);
    memset(buffer, 0, sizeof(buffer)); 
    printf("[INFO] File %s sent successfully.\n", filename);
}

void receive_file(int clientfd, const char *filename) {
    FILE *file = fopen(filename, "wb");

    if (!file) {
        perror("fopen");
        return;
    }

    long file_size;
    recv(clientfd, &file_size, sizeof(file_size), 0);
    
    char *file_buffer = (char *)malloc(file_size);
    ssize_t bytes_received = 0;
    while (bytes_received < file_size) {
        ssize_t result = recv(clientfd, file_buffer + bytes_received, file_size - bytes_received, 0);
        if (result <= 0) {
            perror("recv");
            break;
        }
        bytes_received += result;
    }

    fwrite(file_buffer, 1, bytes_received, file);
    free(file_buffer);

    fclose(file);
    printf("[INFO] File %s received successfully (%ld bytes).\n", filename, bytes_received);
}

void delete_file(int clientfd, const char *filename) {
    if (remove(filename) == 0) {
        send(clientfd, "File deleted successfully.\n", 26, 0);
        printf("[INFO] File %s deleted successfully.\n", filename);
    } else {
        perror("remove");
        send(clientfd, "ERROR: File not found or could not be deleted.\n", 46, 0);
    }
}

int main(void) {
    int sockfd, clientfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size;
    char command[MAXDATASIZE];

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), 0, 8);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        close(sockfd);
        exit(1);
    }

    if (listen(sockfd, 5) == -1) {
        perror("listen");
        close(sockfd);
        exit(1);
    }

    printf("[INFO] Server started on port %d.\n", PORT);

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        if ((clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
            perror("accept");
            continue;
        }

        char ip_address[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_address, INET_ADDRSTRLEN);
        printf("[INFO] Connection from %s.\n", ip_address);

        while (recv(clientfd, command, sizeof(command), 0) > 0) {
            command[strcspn(command, "\n")] = '\0'; 

            if (strncmp(command, "LIST", 4) == 0) {
                list_files(clientfd);
            } else if (strncmp(command, "DOWNLOAD ", 9) == 0) {
                send_file(clientfd, command + 9);
            } else if (strncmp(command, "UPLOAD ", 7) == 0) {
                receive_file(clientfd, command + 7);
            } else if (strncmp(command, "EXIT", 4) == 0) {
                printf("Client disconnected.\n");
                break;
            } else if (strncmp(command, "DELETE ", 7) == 0) {
                delete_file(clientfd, command + 7);
            } else {
                printf("[ERREUR] Unknown command.\n");
            }

            memset(command, 0, sizeof(command));  
        }

        close(clientfd);
    }

    close(sockfd);
    return 0;
}
