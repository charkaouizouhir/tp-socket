#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdio.h>

#define PORT 3504
#define MAXDATASIZE 102400
#define TIMEOUT_SEC 2  

int download_in_progress = 0;

void interrupt_handler(int signum) {
    if (download_in_progress) {
        printf("\n[INFO] Download interrupted by user.\n");
        download_in_progress = 0; 
    }
}

void upload_file(int sockfd, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("fopen");
        send(sockfd, "ERROR: File not found.\n", 23, 0);
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    send(sockfd, &file_size, sizeof(file_size), 0);

    char *file_buffer = (char *)malloc(file_size);
    fread(file_buffer, 1, file_size, file);
    send(sockfd, file_buffer, file_size, 0);  
    free(file_buffer);

    fclose(file);
    printf("[INFO] File %s uploaded successfully.\n", filename);
}

void download_file(int sockfd, char *filename) {
    char buffer[MAXDATASIZE];
    ssize_t bytes_received;
    size_t total_bytes_received = 0;
    struct timeval timeout;
    fd_set readfds;

    printf("[INFO] Waiting for server to send file: %s\n", filename);

    bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0) {
        printf("[ERROR] Failed to receive server response.\n");
        return;
    }

    buffer[bytes_received] = '\0';
    if (strncmp(buffer, "READY", 5) == 0) {
        FILE *file = fopen(filename, "wb");
        if (!file) {
            perror("fopen");
            return;
        }

        printf("[INFO] Downloading file: %s\n", filename);

        while (1) {
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);

            timeout.tv_sec = TIMEOUT_SEC;
            timeout.tv_usec = 0;

            int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
            if (ready == -1) {
                perror("select");
                break;
            } else if (ready == 0) {
                printf("[INFO] Download timeout reached. Completing download...\n");
                break;
            } else {
                bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
                if (bytes_received <= 0) {
                    break;
                }

                buffer[bytes_received] = '\0';

                if (bytes_received >= 3 && strstr(buffer, "EOF") != NULL) {
                    fwrite(buffer, 1, bytes_received - 3, file);
                    printf("[INFO] EOF signal received. File transfer complete.\n");
                    break;
                }

                fwrite(buffer, 1, bytes_received, file);
                total_bytes_received += bytes_received;

                if (total_bytes_received % 1024 == 0) {
                    printf("[INFO] Downloaded %zu bytes...\n", total_bytes_received);
                }

                memset(buffer, 0, sizeof(buffer)); 
            }
        }

        fclose(file);
        printf("[INFO] File %s downloaded successfully (%zu bytes).\n", filename, total_bytes_received);
    }
     else {
        printf("[ERROR] Server did not send file.\n");
    }
}

int main() {
    int sockfd;
    int a=0;
    char command[MAXDATASIZE];
    struct sockaddr_in server_addr;
    char server_ip[INET_ADDRSTRLEN]; 

    printf("Entrez l'adresse IP du serveur  : ");
    fgets(server_ip, sizeof(server_ip), stdin);
    server_ip[strcspn(server_ip, "\n")] = '\0';

    if (strlen(server_ip) == 0) {
        strcpy(server_ip, "127.0.0.1");
    }

    struct sockaddr_in temp_addr;
    if (inet_aton(server_ip, &temp_addr.sin_addr) == 0) {
        fprintf(stderr, "[ERROR] Adresse IP invalide : %s\n", server_ip);
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_aton(server_ip, &server_addr.sin_addr);
    memset(&(server_addr.sin_zero), 0, 8);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("connect");
        close(sockfd);
        exit(1);
    }

    while (1) {
        printf("Enter command: \n" 
        "LIST:pour lister les fichiers de serveur :\n"
        "DOWNLOAD <nom Fichier> : pour telecharger un fichier: \n "
        "UPLOAD <nom Fichier> : pour envoyer un fichier au serveur:"
        "\nDELETE <nomFichier> :pour supprimer un fichier dans le serveur"
        " \nEXIT :pour annuler la connection avec le serveur :\n");
        
        fgets(command, MAXDATASIZE, stdin);
        command[strcspn(command, "\n")] = '\0'; 

        if (strncmp(command, "LIST", 4) == 0) {
            send(sockfd, command, strlen(command), 0);
            char buffer[MAXDATASIZE];
            ssize_t bytes_received;

            memset(buffer, 0, sizeof(buffer));  
            while ((bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[bytes_received] = '\0';  
                if (strncmp(buffer, "EOF", 3) == 0) {
                    printf("------ End of file list ------\n");
                    break;
                }
                printf("%s", buffer);
                memset(buffer, 0, sizeof(buffer));  
            }
        } else if (strncmp(command, "DOWNLOAD ", 9) == 0) {
            download_in_progress = 1;
            send(sockfd, command, strlen(command), 0);
            download_file(sockfd, command + 9);
            memset(command, 0, sizeof(command));
            download_in_progress = 0;
        } else if (strncmp(command, "UPLOAD ", 7) == 0) {
            send(sockfd, command, strlen(command), 0);
            upload_file(sockfd, command + 7);  
            memset(command, 0, sizeof(command));
        } else if (strncmp(command, "EXIT", 4) == 0) {
            send(sockfd, command, strlen(command), 0);
            memset(command, 0, sizeof(command));
            break;
        } else if (strncmp(command, "DELETE ", 7) == 0) {
            send(sockfd, command, strlen(command), 0);
            char response[MAXDATASIZE];
            ssize_t bytes_received = recv(sockfd, response, sizeof(response) - 1, 0);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                printf("%s\n", response);
            }
        } else {
            printf("[ERROR] Unknown command.\n");
        }
    }

    close(sockfd);
    return 0;
}
