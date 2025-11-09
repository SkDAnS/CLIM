/*
 * AffichageISY.c
 * Affichage des messages avec avatar ASCII et notification sonore
 */

#include "../include/Commun.h"

static char nom_groupe[TAILLE_NOM_GROUPE];
static char ip_groupe[TAILLE_IP];
static int port_groupe;
static char mon_login[TAILLE_LOGIN];
static int sockfd_affichage;
static int continuer = 1;

void gestionnaire_signal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        continuer = 0;
    }
}

void afficher_message_avec_avatar(const struct struct_message* msg, const char* ip_emetteur) {
    char avatar = get_avatar_from_ip(ip_emetteur);
    
    if (strcmp(msg->Ordre, ORDRE_MES) == 0) {
        printf("[%c] %s: %s\n", avatar, msg->Emetteur, msg->Texte);
        fflush(stdout);
        
        /* Notification sonore si ce n'est pas mon message */
        if (strcmp(msg->Emetteur, mon_login) != 0) {
            jouer_son_notification();
        }
    } else if (strcmp(msg->Ordre, ORDRE_INFO) == 0) {
        printf("[!] %s\n", msg->Texte);
        fflush(stdout);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 5) return EXIT_FAILURE;
    
    strncpy(nom_groupe, argv[1], TAILLE_NOM_GROUPE - 1);
    strncpy(ip_groupe, argv[2], TAILLE_IP - 1);
    port_groupe = atoi(argv[3]);
    strncpy(mon_login, argv[4], TAILLE_LOGIN - 1);
    
    sockfd_affichage = creer_socket_udp();
    if (sockfd_affichage < 0 || bind_socket(sockfd_affichage, 0) < 0) {
        return EXIT_FAILURE;
    }
    
    signal(SIGTERM, gestionnaire_signal);
    signal(SIGINT, gestionnaire_signal);
    
    printf("\n");
    printf("========================================\n");
    printf("  GROUPE: %s\n", nom_groupe);
    printf("========================================\n\n");
    
    struct struct_message msg;
    char ip_emetteur[TAILLE_IP];
    int port_emetteur;
    
    while (continuer) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(sockfd_affichage, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        
        if (select(sockfd_affichage + 1, &readfds, NULL, NULL, &tv) <= 0) continue;
        
        if (recevoir_message(sockfd_affichage, &msg, ip_emetteur, &port_emetteur) >= 0) {
            afficher_message_avec_avatar(&msg, ip_emetteur);
        }
    }
    
    close(sockfd_affichage);
    printf("\n========================================\n");
    printf("  FIN AFFICHAGE\n");
    printf("========================================\n");
    
    return EXIT_SUCCESS;
}