/*
 * Utilitaires.c
 * Fonctions utilitaires communes - Version corrigée
 */

#include "../include/Commun.h"

/* Avatar ASCII basé sur l'IP (derniers chiffres) */
char get_avatar_from_ip(const char* ip) {
    if (ip == NULL || strlen(ip) == 0) return '@';
    
    /* Extraire le dernier octet */
    const char* last_dot = strrchr(ip, '.');
    if (last_dot == NULL) return '@';
    
    int last_octet = atoi(last_dot + 1);
    
    /* Table ASCII visible (33-126) */
    const char avatars[] = "!#$%&*+-=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ^_abcdefghijklmnopqrstuvwxyz~";
    int index = last_octet % (sizeof(avatars) - 1);
    
    return avatars[index];
}

/* Créer un socket UDP */
int creer_socket_udp() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // Option pour réutiliser l'adresse (utile en cas de redémarrage rapide)
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }
    
    return sockfd;
}

/* Bind un socket à un port - MODIFIÉ pour écouter sur 0.0.0.0 */
int bind_socket(int sockfd, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0 - Écoute sur TOUTES les interfaces
    addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }
    
    printf("[SOCKET] Bind réussi sur 0.0.0.0:%d (toutes interfaces)\n", port);
    return 0;
}

/* Envoyer un message */
int envoyer_message(int sockfd, struct struct_message* msg, const char* ip_dest, int port_dest) {
    struct sockaddr_in addr_dest;
    memset(&addr_dest, 0, sizeof(addr_dest));
    addr_dest.sin_family = AF_INET;
    addr_dest.sin_port = htons(port_dest);
    
    if (inet_pton(AF_INET, ip_dest, &addr_dest.sin_addr) <= 0) {
        perror("inet_pton");
        return -1;
    }
    
    ssize_t sent = sendto(sockfd, msg, sizeof(struct struct_message), 0,
                         (struct sockaddr*)&addr_dest, sizeof(addr_dest));
    
    if (sent < 0) {
        perror("sendto");
        return -1;
    }
    return 0;
}

/* Recevoir un message */
int recevoir_message(int sockfd, struct struct_message* msg, char* ip_src, int* port_src) {
    struct sockaddr_in addr_src;
    socklen_t addr_len = sizeof(addr_src);
    
    ssize_t received = recvfrom(sockfd, msg, sizeof(struct struct_message), 0,
                               (struct sockaddr*)&addr_src, &addr_len);
    
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom");
        }
        return -1;
    }
    
    if (ip_src != NULL) {
        inet_ntop(AF_INET, &addr_src.sin_addr, ip_src, TAILLE_IP);
    }
    
    if (port_src != NULL) {
        *port_src = ntohs(addr_src.sin_port);
    }
    
    return 0;
}

/* Construire un message */
void construire_message(struct struct_message* msg, const char* ordre,
                       const char* emetteur, const char* texte) {
    memset(msg, 0, sizeof(struct struct_message));
    strncpy(msg->Ordre, ordre, TAILLE_ORDRE - 1);
    strncpy(msg->Emetteur, emetteur, TAILLE_EMETTEUR - 1);
    strncpy(msg->Texte, texte, TAILLE_TEXTE - 1);
}

/* Nettoyer une chaîne (retirer \n) */
void nettoyer_chaine(char* chaine) {
    size_t len = strlen(chaine);
    if (len > 0 && chaine[len - 1] == '\n') {
        chaine[len - 1] = '\0';
    }
    len = strlen(chaine);
    if (len > 0 && chaine[len - 1] == '\r') {
        chaine[len - 1] = '\0';
    }
}

/* Jouer une notification sonore (bell) */
void jouer_son_notification() {
    printf("\a");
    fflush(stdout);
}

/* Trouver un groupe par nom */
int trouver_groupe(Groupe groupes[], int nb_groupes, const char* nom) {
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif && strcmp(groupes[i].nom, nom) == 0) {
            return i;
        }
    }
    return -1;
}