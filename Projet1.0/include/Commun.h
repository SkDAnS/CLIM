#ifndef COMMUN_H
#define COMMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>

/* CONSTANTES */
#define PORT_SERVEUR 8000
#define PORT_GROUPE_BASE 8001
#define MAX_GROUPES 50
#define MAX_MEMBRES_PAR_GROUPE 20

#define TAILLE_ORDRE 4
#define TAILLE_EMETTEUR 20
#define TAILLE_TEXTE 100
#define TAILLE_NOM_GROUPE 30
#define TAILLE_LOGIN 20
#define TAILLE_IP 16

/* ORDRES */
#define ORDRE_CON "CON"
#define ORDRE_DECI "DEC"
#define ORDRE_MES "MES"
#define ORDRE_CRE "CRE"
#define ORDRE_SUP "SUP"
#define ORDRE_LST "LST"
#define ORDRE_JOIN "JOI"
#define ORDRE_QUIT "QUI"
#define ORDRE_DEL "DEL"
#define ORDRE_FUS "FUS"
#define ORDRE_LMEM "LME"
#define ORDRE_OK "OK"
#define ORDRE_ERR "ERR"
#define ORDRE_INFO "INF"

/* STRUCTURES */
struct struct_message {
    char Ordre[TAILLE_ORDRE];
    char Emetteur[TAILLE_EMETTEUR];
    char Texte[TAILLE_TEXTE];
};

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
    pid_t pid_processus;
    int nb_membres;
    int actif;
    char ip[TAILLE_IP];  // Ajoutez un champ pour l'IP
    Membre membres[MAX_MEMBRES_PAR_GROUPE];
} Groupe;


typedef struct {
    int actif;
    int commande;
    char cible[TAILLE_LOGIN];
    char groupe_fusion[TAILLE_NOM_GROUPE];
    int nb_membres;
    Membre membres[MAX_MEMBRES_PAR_GROUPE];
} SHM_Groupe;

typedef struct {
    int actif;
    int terminer;
    char nom_groupe[TAILLE_NOM_GROUPE];
    char ip_groupe[TAILLE_IP];
    int port_groupe;
} SHM_Affichage;

/* PROTOTYPES */
char get_avatar_from_ip(const char* ip);
int creer_socket_udp();
int bind_socket(int sockfd, int port);
int envoyer_message(int sockfd, struct struct_message* msg, const char* ip_dest, int port_dest);
int recevoir_message(int sockfd, struct struct_message* msg, char* ip_src, int* port_src);
void construire_message(struct struct_message* msg, const char* ordre, const char* emetteur, const char* texte);
void nettoyer_chaine(char* chaine);
void jouer_son_notification();
int trouver_groupe(Groupe groupes[], int nb_groupes, const char* nom);

#endif
