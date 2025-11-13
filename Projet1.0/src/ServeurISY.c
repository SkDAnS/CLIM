/*
 * ServeurISY.c
 * Version compatible avec ClientISY.c (découverte automatique + fusion totale)
 *
 * Fonctionnalités :
 *  - Répond au broadcast UDP de découverte ("SERVER_DISCOVERY")
 *  - Gère création, listing, jonction et fusion de groupes
 *  - Lance un processus GroupeISY par groupe créé
 */

#include "../include/Commun.h"
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define BROADCAST_PORT 9999

static Groupe groupes[MAX_GROUPES];
static int nb_groupes = 0;
static int sockfd_serveur;
static int sockfd_broadcast;
static int continuer = 1;

/* ========================== SIGNAL HANDLER ========================== */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[SERVEUR] Arrêt demandé.\n");
        continuer = 0;
    }
}

/* ========================== BROADCAST ========================== */
void* thread_broadcast(void* arg) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[256];

    printf("[BROADCAST] Thread de découverte serveur actif (port %d)\n", BROADCAST_PORT);

    while (continuer) {
        int bytes = recvfrom(sockfd_broadcast, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr*)&addr, &addrlen);
        if (bytes <= 0) continue;

        buffer[bytes] = '\0';

        if (strcmp(buffer, "SERVER_DISCOVERY") == 0) {
            char ip_client[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, ip_client, sizeof(ip_client));

            printf("[BROADCAST] Découverte reçue de %s\n", ip_client);

            char reponse[256];
            char ip_locale[TAILLE_IP];
get_local_ip(ip_locale, sizeof(ip_locale));

snprintf(reponse, sizeof(reponse), "SERVER_HERE:%s", ip_locale);

sendto(sockfd_broadcast, reponse, strlen(reponse), 0,
       (struct sockaddr*)&addr, sizeof(addr));

printf("[BROADCAST] Réponse envoyée : %s\n", ip_locale);


            sendto(sockfd_broadcast, reponse, strlen(reponse), 0,
                   (struct sockaddr*)&addr, sizeof(addr));

            printf("[BROADCAST] Réponse envoyée à %s\n", ip_client);
        }
    }

    close(sockfd_broadcast);
    return NULL;
}

/* ========================== GROUPES ========================== */
int creer_groupe(const char* nom_groupe, const char* moderateur) {
    if (trouver_groupe(groupes, nb_groupes, nom_groupe) >= 0)
        return -1;

    if (nb_groupes >= MAX_GROUPES)
        return -1;

    int idx = nb_groupes;
    strncpy(groupes[idx].nom, nom_groupe, TAILLE_NOM_GROUPE - 1);
    strncpy(groupes[idx].moderateur, moderateur, TAILLE_LOGIN - 1);
    groupes[idx].port = PORT_GROUPE_BASE + idx;
    groupes[idx].nb_membres = 0;
    groupes[idx].actif = 1;

    printf("[SERVEUR] Création du groupe '%s' par %s\n", nom_groupe, moderateur);

    /* Fork GroupeISY */
    pid_t pid = fork();
    if (pid < 0) {
        perror("[SERVEUR] Erreur fork");
        return -1;
    }

    if (pid == 0) {
        char port_str[10];
        sprintf(port_str, "%d", groupes[idx].port);
        execl("./bin/GroupeISY", "GroupeISY", nom_groupe, moderateur, port_str, NULL);
        perror("[SERVEUR] Erreur execl");
        exit(EXIT_FAILURE);
    }

    groupes[idx].pid_processus = pid;
    nb_groupes++;

    printf("[SERVEUR] Groupe '%s' lancé (PID: %d, Port: %d)\n",
           nom_groupe, pid, groupes[idx].port);

    return idx;
}

/* ========================== LISTE GROUPES ========================== */
void envoyer_liste_groupes(const char* ip_client, int port_client) {
    struct struct_message msg;
    char liste[TAILLE_TEXTE] = "";

    int nb = 0;
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif) {
            if (nb > 0) strncat(liste, ", ", sizeof(liste) - strlen(liste) - 1);
            strncat(liste, groupes[i].nom, sizeof(liste) - strlen(liste) - 1);
            nb++;
        }
    }
    if (nb == 0) strcpy(liste, "Aucun groupe");

    construire_message(&msg, ORDRE_INFO, "Serveur", liste);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);

    printf("[SERVEUR] Liste envoyée à %s:%d (%d groupes)\n", ip_client, port_client, nb);
}

/* ========================== REJOINDRE GROUPE ========================== */
void traiter_connexion_groupe(const char* nom_groupe, const char* login,
                              const char* ip_client, int port_client) {
    struct struct_message msg;
    int idx = trouver_groupe(groupes, nb_groupes, nom_groupe);

    if (idx < 0 || !groupes[idx].actif) {
        construire_message(&msg, ORDRE_ERR, "Serveur", "Groupe introuvable");
        envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
        return;
    }

    char info[TAILLE_TEXTE];
char ip_locale[TAILLE_IP];
get_local_ip(ip_locale, sizeof(ip_locale));

snprintf(info, sizeof(info), "%s:%d", ip_locale, groupes[idx].port);
    construire_message(&msg, ORDRE_OK, "Serveur", info);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);

    printf("[SERVEUR] %s rejoint '%s'\n", login, nom_groupe);
}

/* ========================== FUSION GROUPES ========================== */
void fusionner_groupes(const char* data, const char* demandeur) {
    char g1[TAILLE_NOM_GROUPE], g2[TAILLE_NOM_GROUPE], nouveau_nom[TAILLE_NOM_GROUPE];
    if (sscanf(data, "%[^:]:%[^:]:%s", g1, g2, nouveau_nom) != 3) {
        printf("[SERVEUR] Format de fusion invalide\n");
        return;
    }

    int idx1 = trouver_groupe(groupes, nb_groupes, g1);
    int idx2 = trouver_groupe(groupes, nb_groupes, g2);

    if (idx1 < 0 || idx2 < 0) {
        printf("[SERVEUR] Groupes introuvables pour fusion\n");
        return;
    }

    printf("[SERVEUR] Fusion demandée par %s : '%s' + '%s' -> '%s'\n",
           demandeur, g1, g2, nouveau_nom);

    if (groupes[idx2].pid_processus > 0) {
        kill(groupes[idx2].pid_processus, SIGTERM);
        waitpid(groupes[idx2].pid_processus, NULL, 0);
        groupes[idx2].actif = 0;
    }

    creer_groupe(nouveau_nom, demandeur);
}

/* ========================== BOUCLE PRINCIPALE ========================== */
void boucle_serveur() {
    struct struct_message msg;
    char ip_client[TAILLE_IP];
    int port_client;

    printf("\n[SERVEUR] En attente de messages clients...\n");

    while (continuer) {
        if (recevoir_message(sockfd_serveur, &msg, ip_client, &port_client) < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        if (strcmp(msg.Ordre, ORDRE_CRE) == 0) {
            int idx = creer_groupe(msg.Texte, msg.Emetteur);
            struct struct_message rep;
            if (idx >= 0)
                construire_message(&rep, ORDRE_OK, "Serveur", "Groupe créé");
            else
                construire_message(&rep, ORDRE_ERR, "Serveur", "Erreur création");
            envoyer_message(sockfd_serveur, &rep, ip_client, port_client);

        } else if (strcmp(msg.Ordre, ORDRE_LST) == 0) {
            envoyer_liste_groupes(ip_client, port_client);

        } else if (strcmp(msg.Ordre, ORDRE_JOIN) == 0) {
            traiter_connexion_groupe(msg.Texte, msg.Emetteur, ip_client, port_client);

        } else if (strcmp(msg.Ordre, ORDRE_FUS) == 0) {
            fusionner_groupes(msg.Texte, msg.Emetteur);
        }
    }
}

/* ========================== TERMINAISON ========================== */
void terminer_tous_groupes() {
    printf("\n[SERVEUR] Arrêt des groupes...\n");
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif && groupes[i].pid_processus > 0) {
            kill(groupes[i].pid_processus, SIGTERM);
            waitpid(groupes[i].pid_processus, NULL, 0);
        }
    }
}



/* Fonction utilitaire pour récupérer l'IP locale du serveur */
void get_local_ip(char* buffer, size_t size) {
    FILE* f = fopen("/etc/resolv.conf", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "nameserver", 10) == 0) {
                char ip[64];
                if (sscanf(line, "nameserver %63s", ip) == 1) {
                    strncpy(buffer, ip, size - 1);
                    buffer[size - 1] = '\0';
                    fclose(f);
                    return;   // OK trouvé → on renvoie l'IP Windows
                }
            }
        }
        fclose(f);
    }

    // fallback
    strncpy(buffer, "127.0.0.1", size - 1);
    buffer[size - 1] = '\0';
}


/* ========================== MAIN ========================== */
int main() {
    printf("=== SERVEUR ISY (Auto-Discovery) ===\n");

    sockfd_serveur = creer_socket_udp();
    if (sockfd_serveur < 0 || bind_socket(sockfd_serveur, PORT_SERVEUR) < 0) {
        fprintf(stderr, "Erreur initialisation serveur UDP\n");
        return EXIT_FAILURE;
    }

    /* --- Socket Broadcast pour la découverte --- */
    sockfd_broadcast = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_broadcast < 0) {
        perror("[BROADCAST] Erreur socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr_b;
    memset(&addr_b, 0, sizeof(addr_b));
    addr_b.sin_family = AF_INET;
    addr_b.sin_port = htons(BROADCAST_PORT);
    addr_b.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd_broadcast, (struct sockaddr*)&addr_b, sizeof(addr_b)) < 0) {
        perror("[BROADCAST] Erreur bind");
        return EXIT_FAILURE;
    }

    pthread_t tid_broadcast;
    pthread_create(&tid_broadcast, NULL, thread_broadcast, NULL);

    signal(SIGINT, gestionnaire_signal);
    for (int i = 0; i < MAX_GROUPES; i++) groupes[i].actif = 0;

    boucle_serveur();
    terminer_tous_groupes();

    close(sockfd_serveur);
    close(sockfd_broadcast);
    printf("[SERVEUR] Terminé proprement.\n");
    return EXIT_SUCCESS;
}
