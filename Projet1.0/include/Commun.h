#ifndef COMMUN_H
#define COMMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

/* Paramètres généraux */

#define SERVER_PORT       8000        /* Port du ServeurISY */
#define GROUP_PORT_BASE   8100        /* Port de base des groupes */
#define MAX_GROUPS        10
#define MAX_GROUP_NAME    32
#define MAX_USERNAME      20
#define MAX_TEXT          100
#define MAX_CLIENTS_GROUP 16
#define MAX_EMOJI         8           /* Taille du champ emoji (UTF-8) */

#define SHM_CLIENT_KEY    0x1234      /* SHM ClientISY <-> AffichageISY */
#define SHM_GROUP_KEY_BASE 0x2000     /* SHM ServeurISY <-> GroupeISY (optionnel) */

/* Ordres possible dans les messages réseau */
#define ORDRE_CMD "CMD"   /* Commande envoyée au serveur */
#define ORDRE_RPL "RPL"   /* Réponse serveur */
#define ORDRE_CON "CON"   /* Connexion à un groupe (auprès de GroupeISY) */
#define ORDRE_MSG "MES"   /* Message normal d’un utilisateur */
/* Ordre de gestion envoyé par le serveur au groupe (ex: MIGRATE) */
#define ORDRE_MGR "MGR"

/* Structure de message réseau (énoncé) */
typedef struct {
    char ordre[4];                 /* "CMD", "MES", ... (3 + '\0') */
    char emetteur[MAX_USERNAME];   /* Nom utilisateur */
    char emoji[MAX_EMOJI];         /* Emoji Unicode (UTF-8) */
    char groupe[MAX_GROUP_NAME];   /* Nom du groupe si besoin */
    char texte[MAX_TEXT];          /* Commande ou contenu du message */
} ISYMessage;

/* Description d’un groupe côté serveur */
typedef struct {
    int  actif;
    char nom[MAX_GROUP_NAME];
    char moderateur[MAX_USERNAME];
    int  port_groupe;              /* port UDP du GroupeISY */
    key_t shm_key;                 /* pour SHM groupe (statistiques, optionnel) */
    int   shm_id;
    pid_t pid;                     /* pid du processus GroupeISY */
} GroupeInfo;

/* SHM ClientISY <-> AffichageISY
 * Minimal : seulement un flag pour demander l'arrêt de l'affichage. */
typedef struct {
    int running;                   /* 1 = continuer, 0 = arrêter */
    char notify[MAX_TEXT];         /* message pour inviter a migrer */
    int  notify_flag;              /* 1 = nouvelle notification */
    char sound_name[256];          /* nom du fichier son pour notifications */
} ClientDisplayShm;

/* SHM ServeurISY <-> GroupeISY (ex : statistiques) – optionnel */
typedef struct {
    int nb_messages;
    int nb_clients;
} GroupStats;

/* Fonctions utilitaires communes */

static inline void check_fatal(int cond, const char *msg)
{
    if (cond) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

static inline int create_udp_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    check_fatal(sock < 0, "socket");
    return sock;
}

static inline void fill_sockaddr(struct sockaddr_in *addr,
                                 const char *ip, int port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port   = htons(port);
    addr->sin_addr.s_addr = (ip == NULL) ?
                            htonl(INADDR_ANY) :
                            inet_addr(ip);
}

/* Retourne un emoji Unicode basé sur le nom d'utilisateur.
 * Utilise la plage U+1F600 à U+1F64F (48 emojis).
 * Permet d'obtenir un emoji déterministe pour chaque nom.
 */
static inline void choose_emoji_from_username(const char *username, char *emoji_buf)
{
    if (!username || username[0] == '\0') {
        strcpy(emoji_buf, "😀");  /* Default: U+1F600 */
        return;
    }

    /* Calcul du hash du nom d'utilisateur */
    size_t n = strlen(username);
    unsigned int hash = 0;
    for (size_t i = 0; i < n; ++i) {
        hash = (hash * 31u) + (unsigned char)username[i];
    }

    /* Sélectionner un emoji dans la plage U+1F600 à U+1F64F (48 emojis) */
    int emoji_index = hash % 48;
    unsigned int codepoint = 0x1F600 + emoji_index;

    /* Encoder le codepoint en UTF-8 */
    if (codepoint <= 0x7F) {
        /* 1 byte */
        emoji_buf[0] = (char)codepoint;
        emoji_buf[1] = '\0';
    } else if (codepoint <= 0x7FF) {
        /* 2 bytes */
        emoji_buf[0] = (char)(0xC0 | (codepoint >> 6));
        emoji_buf[1] = (char)(0x80 | (codepoint & 0x3F));
        emoji_buf[2] = '\0';
    } else if (codepoint <= 0xFFFF) {
        /* 3 bytes */
        emoji_buf[0] = (char)(0xE0 | (codepoint >> 12));
        emoji_buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        emoji_buf[2] = (char)(0x80 | (codepoint & 0x3F));
        emoji_buf[3] = '\0';
    } else {
        /* 4 bytes (for emojis) */
        emoji_buf[0] = (char)(0xF0 | (codepoint >> 18));
        emoji_buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        emoji_buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        emoji_buf[3] = (char)(0x80 | (codepoint & 0x3F));
        emoji_buf[4] = '\0';
    }
}

#endif /* COMMUN_H */
