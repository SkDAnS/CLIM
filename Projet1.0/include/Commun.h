#ifndef COMMUN_H
#define COMMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Paramètres généraux */

#define SERVER_PORT       8000        /* Port du ServeurISY */
#define GROUP_PORT_BASE   8100        /* Port de base des groupes */
#define MAX_GROUPS        10
#define MAX_GROUP_NAME    32
#define MAX_USERNAME      20
#define MAX_TEXT          100
#define MAX_CLIENTS_GROUP 16

#define SHM_CLIENT_KEY    0x1234      /* SHM ClientISY <-> AffichageISY */
#define SHM_GROUP_KEY_BASE 0x2000     /* SHM ServeurISY <-> GroupeISY (optionnel) */

/* Ordres possible dans les messages réseau */
#define ORDRE_CMD "CMD"   /* Commande envoyée au serveur */
#define ORDRE_RPL "RPL"   /* Réponse serveur */
#define ORDRE_CON "CON"   /* Connexion à un groupe (auprès de GroupeISY) */
#define ORDRE_MSG "MES"   /* Message normal d’un utilisateur */

/* Structure de message réseau (énoncé) */
typedef struct {
    char ordre[4];                 /* "CMD", "MES", ... (3 + '\0') */
    char emetteur[MAX_USERNAME];   /* Nom utilisateur */
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
} GroupeInfo;

/* SHM ClientISY <-> AffichageISY
 * Minimal : seulement un flag pour demander l’arrêt de l’affichage. */
typedef struct {
    int running;                   /* 1 = continuer, 0 = arrêter */
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

#endif /* COMMUN_H */
