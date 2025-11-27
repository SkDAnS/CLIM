#ifndef COMMUN_H
#define COMMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>

/* ---------------- TAILLES ---------------- */

#define TAILLE_LOGIN 32
#define TAILLE_NOM_GROUPE 32
#define TAILLE_IP 32
#define TAILLE_TEXTE 256

#define MAX_GROUPES 50
#define MAX_MEMBRES_PAR_GROUPE 50

#define PORT_SERVEUR 8000
#define PORT_GROUPE_BASE 8001

/* ---------------- STRUCTURES ---------------- */

typedef struct {
    char Ordre[16];
    char Emetteur[TAILLE_LOGIN];
    char Texte[TAILLE_TEXTE];
} struct_message;   // ⚡ DOIT ÊTRE DÉFINI AVANT TOUTE UTILISATION

typedef struct {
    char login[TAILLE_LOGIN];
    char ip[TAILLE_IP];
    int port;
    int actif;
    int banni;
} Membre;

typedef struct {
    char nom[TAILLE_NOM_GROUPE];
    char moderateur[TAILLE_LOGIN];
    int port;
    int actif;
    pid_t pid_processus;
} Groupe;

/* ---------------- ORDRES ---------------- */

#define ORDRE_CRE "CRE"
#define ORDRE_OK  "OK"
#define ORDRE_ERR "ERR"
#define ORDRE_LST "LST"
#define ORDRE_JOIN "JOIN"
#define ORDRE_CON "CON"
#define ORDRE_MES "MES"
#define ORDRE_INFO "INFO"
#define ORDRE_FUS "FUS"
#define ORDRE_LMEM "LMEM"

/* ---------------- PROTOTYPES ---------------- */

int creer_socket_udp();
int bind_socket(int sockfd, const char *ip, int port);

int envoyer_message(int sockfd, const struct_message *msg, const char *ip, int port);
int recevoir_message(int sockfd, struct_message *msg, char *ip_out, int *port_out);

void construire_message(struct_message *msg,
                        const char *ordre,
                        const char *emetteur,
                        const char *texte);

int trouver_groupe(Groupe *groupes, int nb, const char *nom);
void nettoyer_chaine(char *s);

/* ================= AVATARS & NOTIFICATIONS ================= */
const char* get_avatar_from_ip(const char* ip);
void jouer_son_notification(void);


#endif
