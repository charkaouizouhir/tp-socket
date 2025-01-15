#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 3504
#define TAILLE_MAX 102400
#define TEMPS_D_ATTENTE 2

void lister_fichiers(int clientfd) {
    DIR *d;
    struct dirent *dir;
    char tampon[TAILLE_MAX];
    memset(tampon, 0, sizeof(tampon));

    d = opendir(".");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {
                strcat(tampon, dir->d_name);
                strcat(tampon, "\n");
            }
        }
        closedir(d);
    }
    send(clientfd, tampon, strlen(tampon), 0);
    send(clientfd, "FIN", 3, 0);

    memset(tampon, 0, sizeof(tampon));
}

void envoyer_fichier(int clientfd, const char *nom_fichier) {
    FILE *fichier = fopen(nom_fichier, "rb");
    if (!fichier) {
        perror("fopen");
        send(clientfd, "ERREUR : Fichier introuvable.\n", 28, 0);
        return;
    }

    send(clientfd, "PRET", 4, 0);
    printf("[INFO] Envoi du fichier : %s\n", nom_fichier);

    char tampon[TAILLE_MAX];
    size_t bytes_lus;

    while ((bytes_lus = fread(tampon, 1, sizeof(tampon), fichier)) > 0) {
        if (send(clientfd, tampon, bytes_lus, 0) == -1) {
            perror("send");
            fclose(fichier);
            return;
        }
        printf("[INFO] %zu octets envoyés\n", bytes_lus);
        memset(tampon, 0, sizeof(tampon));
    }

    strcpy(tampon, "FIN");
    send(clientfd, tampon, strlen(tampon), 0);

    fclose(fichier);
    memset(tampon, 0, sizeof(tampon));
    printf("[INFO] Fichier %s envoyé avec succès.\n", nom_fichier);
}

void recevoir_fichier(int clientfd, const char *nom_fichier) {
    FILE *fichier = fopen(nom_fichier, "wb");

    if (!fichier) {
        perror("fopen");
        return;
    }

    long taille_fichier;
    recv(clientfd, &taille_fichier, sizeof(taille_fichier), 0);

    char *tampon_fichier = (char *)malloc(taille_fichier);
    ssize_t octets_recus = 0;
    while (octets_recus < taille_fichier) {
        ssize_t resultat = recv(clientfd, tampon_fichier + octets_recus, taille_fichier - octets_recus, 0);
        if (resultat <= 0) {
            perror("recv");
            break;
        }
        octets_recus += resultat;
    }

    fwrite(tampon_fichier, 1, octets_recus, fichier);
    free(tampon_fichier);

    fclose(fichier);
    printf("[INFO] Fichier %s reçu avec succès (%ld octets).\n", nom_fichier, octets_recus);
}

void supprimer_fichier(int clientfd, const char *nom_fichier) {
    if (remove(nom_fichier) == 0) {
        send(clientfd, "Fichier supprimé avec succès.\n", 30, 0);
        printf("[INFO] Fichier %s supprimé avec succès.\n", nom_fichier);
    } else {
        perror("remove");
        send(clientfd, "ERREUR : Fichier introuvable ou impossible à supprimer.\n", 56, 0);
    }
}

int main(void) {
    int sockfd, clientfd;
    struct sockaddr_in serveur_addr, client_addr;
    socklen_t taille_client;
    char commande[TAILLE_MAX];

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    serveur_addr.sin_family = AF_INET;
    serveur_addr.sin_port = htons(PORT);
    serveur_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(serveur_addr.sin_zero), 0, 8);

    if (bind(sockfd, (struct sockaddr *)&serveur_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        close(sockfd);
        exit(1);
    }

    if (listen(sockfd, 5) == -1) {
        perror("listen");
        close(sockfd);
        exit(1);
    }

    printf("[INFO] Serveur démarré sur le port %d.\n", PORT);

    while (1) {
        taille_client = sizeof(struct sockaddr_in);
        if ((clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &taille_client)) == -1) {
            perror("accept");
            continue;
        }

        char adresse_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), adresse_ip, INET_ADDRSTRLEN);
        printf("[INFO] Connexion de %s.\n", adresse_ip);

        while (recv(clientfd, commande, sizeof(commande), 0) > 0) {
            commande[strcspn(commande, "\n")] = '\0';

            if (strncmp(commande, "LISTE", 5) == 0) {
                lister_fichiers(clientfd);
            } else if (strncmp(commande, "TELECHARGER ", 12) == 0) {
                envoyer_fichier(clientfd, commande + 12);
            } else if (strncmp(commande, "ENVOYER ", 8) == 0) {
                recevoir_fichier(clientfd, commande + 8);
            } else if (strncmp(commande, "SORTIR", 6) == 0) {
                printf("Client déconnecté.\n");
                break;
            } else if (strncmp(commande, "SUPPRIMER ", 10) == 0) {
                supprimer_fichier(clientfd, commande + 10);
            } else {
                printf("[ERREUR] Commande inconnue.\n");
            }

            memset(commande, 0, sizeof(commande));
        }

        close(clientfd);
    }

    close(sockfd);
    return 0;
}
