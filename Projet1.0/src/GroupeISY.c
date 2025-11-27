#include "../include/Commun.h"
#include <signal.h>
#include <arpa/inet.h>
#include <string.h>

static char nom_groupe[TAILLE_NOM_GROUPE];
static char moderateur[TAILLE_LOGIN];
static int port_groupe;

static int sockfd_groupe;
static int continuer = 1;

static Membre membres[MAX_MEMBRES_PAR_GROUPE];
static int nb_membres = 0;

/* ---------------------------------------------------------
   SIGNAL
--------------------------------------------------------- */
void gestionnaire_signal(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
        continuer = 0;
}

/* ---------------------------------------------------------
   AJOUT MEMBRE
--------------------------------------------------------- */
int ajouter_membre(const char *login, const char *ip, int port)
{
    for (int i = 0; i < nb_membres; i++) {
        if (!strcmp(membres[i].login, login)) {

            membres[i].actif = 1;
            strncpy(membres[i].ip, ip, TAILLE_IP-1);
            membres[i].port = port;
            return 0;
        }
    }

    if (nb_membres >= MAX_MEMBRES_PAR_GROUPE) return -1;

    strncpy(membres[nb_membres].login, login, TAILLE_LOGIN-1);
    strncpy(membres[nb_membres].ip, ip, TAILLE_IP-1);
    membres[nb_membres].port = port;
    membres[nb_membres].actif = 1;
    membres[nb_membres].banni = 0;

    nb_membres++;

    printf("[GROUPE %s] Nouveau membre: %s (%s)\n",
           nom_groupe, login, ip);

    return 1;
}

/* ---------------------------------------------------------
   REDISTRIBUER
--------------------------------------------------------- */
void redistribuer_message(const struct_message *msg)
{
    int nb = 0;

    for (int i = 0; i < nb_membres; i++) {
        if (membres[i].actif && !membres[i].banni) {
            envoyer_message(sockfd_groupe, msg,
                            membres[i].ip,
                            membres[i].port);
            nb++;
        }
    }

    printf("[GROUPE %s] %s -> %d membres\n",
           nom_groupe, msg->Emetteur, nb);
}

/* ---------------------------------------------------------
   LISTE MEMBRES
--------------------------------------------------------- */
void envoyer_liste_membres(const char *ip, int port)
{
    char liste[TAILLE_TEXTE] = "";
    int nb = 0;

    for (int i = 0; i < nb_membres; i++) {
        if (membres[i].actif && !membres[i].banni) {
            if (nb > 0) strncat(liste, ", ", sizeof(liste)-strlen(liste)-1);
            strncat(liste, membres[i].login, sizeof(liste)-strlen(liste)-1);
            nb++;
        }
    }

    if (nb==0)
        strcpy(liste, "Aucun membre");

    struct_message msg;
    construire_message(&msg, ORDRE_INFO, "Serveur", liste);

    envoyer_message(sockfd_groupe, &msg, ip, port);
}

/* ---------------------------------------------------------
   LOOP
--------------------------------------------------------- */
void boucle_groupe()
{
    struct_message msg;
    char ip_client[TAILLE_IP];
    int port_client;

    printf("[GROUPE %s] Écoute sur 0.0.0.0:%d\n",
           nom_groupe, port_groupe);

    while (continuer) {

        if (recevoir_message(sockfd_groupe, &msg, ip_client, &port_client) < 0)
            continue;

        if (!strcmp(msg.Ordre, ORDRE_CON))
            ajouter_membre(msg.Emetteur, ip_client, port_client);

        else if (!strcmp(msg.Ordre, ORDRE_MES))
            redistribuer_message(&msg);

        else if (!strcmp(msg.Ordre, ORDRE_LMEM))
            envoyer_liste_membres(ip_client, port_client);
    }
}

/* ---------------------------------------------------------
   MAIN
--------------------------------------------------------- */
int main(int argc, char *argv[])
{
    if (argc < 4) return EXIT_FAILURE;

    strncpy(nom_groupe, argv[1], TAILLE_NOM_GROUPE-1);
    strncpy(moderateur, argv[2], TAILLE_LOGIN-1);
    port_groupe = atoi(argv[3]);

    printf("[GROUPE %s] Modérateur=%s Port=%d\n",
           nom_groupe, moderateur, port_groupe);

    sockfd_groupe = creer_socket_udp();

    if (bind_socket(sockfd_groupe, "0.0.0.0", port_groupe) < 0)
        return EXIT_FAILURE;

    signal(SIGTERM, gestionnaire_signal);
    signal(SIGINT, gestionnaire_signal);

    boucle_groupe();

    close(sockfd_groupe);
    printf("[GROUPE %s] Fermé\n", nom_groupe);

    return EXIT_SUCCESS;
}
