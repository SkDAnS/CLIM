
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include "../include/Commun.h"

/* ============================================================
   VARIABLES GLOBALES
   ============================================================ */

static Groupe groupes[MAX_GROUPES];
static int nb_groupes = 0;

static int sockfd_serveur;
static int continuer = 1;

static char IP_SERVEUR[TAILLE_IP] = "0.0.0.0";
static int PORT_SERVEUR_CFG = 8000;

/* ============================================================
   LECTURE CONFIG SERVEUR
   ============================================================ */

void load_server_config(char *ip_out, int *port_out)
{
    FILE *f = fopen("config/serveur.conf", "r");
    if (!f) {
        perror("serveur.conf");
        exit(1);
    }

    char key[64], val[128], line[256];
    while (fgets(line, sizeof(line), f)) {

        if (sscanf(line, "%63[^=]=%127s", key, val) == 2) {

            if (!strcmp(key, "IP"))
                strncpy(ip_out, val, TAILLE_IP - 1);

            else if (!strcmp(key, "PORT"))
                *port_out = atoi(val);
        }
    }

    fclose(f);
}

/* ============================================================
   SIGNAL : STOP SERVEUR + GROUPES
   ============================================================ */
void gestionnaire_signal(int sig)
{
    if (sig == SIGINT) {

        printf("\n[SERVEUR] Arrêt demandé.\n");

        for (int i = 0; i < nb_groupes; i++) {
            if (groupes[i].actif && groupes[i].pid_processus > 0) {
                printf("[SERVEUR] Kill groupe '%s' (PID %d)\n",
                       groupes[i].nom, groupes[i].pid_processus);
                kill(groupes[i].pid_processus, SIGTERM);
                waitpid(groupes[i].pid_processus, NULL, 0);
            }
        }

        close(sockfd_serveur);
        printf("[SERVEUR] Fermeture propre.\n");
        exit(0);
    }
}

/* ============================================================
   CRÉATION GROUPE
   ============================================================ */

int creer_groupe(const char* nom, const char* moderateur)
{
    if (trouver_groupe(groupes, nb_groupes, nom) >= 0)
        return -1;

    if (nb_groupes >= MAX_GROUPES)
        return -1;

    int idx = nb_groupes;

    strncpy(groupes[idx].nom, nom, TAILLE_NOM_GROUPE - 1);
    strncpy(groupes[idx].moderateur, moderateur, TAILLE_LOGIN - 1);

    groupes[idx].port = PORT_GROUPE_BASE + idx;
    groupes[idx].actif = 1;

    printf("[SERVEUR] Création du groupe '%s' (port %d)\n",
           nom, groupes[idx].port);

    pid_t pid = fork();
    if (pid == 0) {
        char port_str[16];
        sprintf(port_str, "%d", groupes[idx].port);

        execl("./bin/GroupeISY",
              "GroupeISY",
              nom,
              moderateur,
              port_str,
              NULL);

        exit(EXIT_FAILURE);
    }

    groupes[idx].pid_processus = pid;
    nb_groupes++;

    return idx;
}

/* ============================================================
   LISTE GROUPES
   ============================================================ */

void envoyer_liste_groupes(const char* ip, int port)
{
    struct_message msg;
    char liste[TAILLE_TEXTE] = "";
    int count = 0;

    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif) {
            if (count > 0)
                strncat(liste, ", ", sizeof(liste) - strlen(liste) - 1);

            strncat(liste, groupes[i].nom, sizeof(liste) - strlen(liste) - 1);
            count++;
        }
    }

    if (count == 0)
        strcpy(liste, "Aucun groupe");

    construire_message(&msg, ORDRE_INFO, "Serveur", liste);
    envoyer_message(sockfd_serveur, &msg, ip, port);
}

/* ============================================================
   REJOINDRE UN GROUPE
   ============================================================ */

void traiter_connexion_groupe(const char* nom, const char* login,
                              const char* ip_client, int port_client)
{
    struct_message msg;
    int idx = trouver_groupe(groupes, nb_groupes, nom);

    if (idx < 0) {
        construire_message(&msg, ORDRE_ERR, "Serveur", "Groupe introuvable");
        envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
        return;
    }

    char rep[TAILLE_TEXTE];
    snprintf(rep, sizeof(rep), "%s:%d", IP_SERVEUR, groupes[idx].port);

    construire_message(&msg, ORDRE_OK, "Serveur", rep);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);

    printf("[SERVEUR] %s rejoint '%s'\n", login, nom);
}

/* ============================================================
   FUSION
   ============================================================ */

void fusionner_groupes(const char* data, const char* demandeur)
{
    char a[30], b[30];
    if (sscanf(data, "%[^:]:%s", a, b) != 2)
        return;

    int g1 = trouver_groupe(groupes, nb_groupes, a);
    int g2 = trouver_groupe(groupes, nb_groupes, b);

    if (g1 < 0 || g2 < 0)
        return;

    kill(groupes[g2].pid_processus, SIGTERM);
    waitpid(groupes[g2].pid_processus, NULL, 0);
    groupes[g2].actif = 0;

    creer_groupe(a, demandeur);
}

/* ============================================================
   BOUCLE PRINCIPALE
   ============================================================ */

void boucle_serveur()
{
    struct_message msg;
    char ip_src[TAILLE_IP];
    int port_src;

    printf("[SERVEUR] Écoute sur %s:%d\n\n", IP_SERVEUR, PORT_SERVEUR_CFG);

    while (continuer) {

        if (recevoir_message(sockfd_serveur, &msg, ip_src, &port_src) < 0)
            continue;

        if (!strcmp(msg.Ordre, ORDRE_CRE)) {
            struct_message rep;

            if (creer_groupe(msg.Texte, msg.Emetteur) >= 0)
                construire_message(&rep, ORDRE_OK, "Serveur", "Groupe créé");
            else
                construire_message(&rep, ORDRE_ERR, "Serveur", "Erreur");

            envoyer_message(sockfd_serveur, &rep, ip_src, port_src);
        }

        else if (!strcmp(msg.Ordre, ORDRE_LST)) {
            envoyer_liste_groupes(ip_src, port_src);
        }

        else if (!strcmp(msg.Ordre, ORDRE_JOIN)) {
            traiter_connexion_groupe(msg.Texte, msg.Emetteur, ip_src, port_src);
        }

        else if (!strcmp(msg.Ordre, ORDRE_FUS)) {
            fusionner_groupes(msg.Texte, msg.Emetteur);
        }
    }
}

/* ============================================================
   MAIN
   ============================================================ */

int main()
{
    printf("=== SERVEUR ISY ===\n");

    load_server_config(IP_SERVEUR, &PORT_SERVEUR_CFG);

    printf("IP serveur  : %s\n", IP_SERVEUR);
    printf("Port serveur: %d\n\n", PORT_SERVEUR_CFG);

    sockfd_serveur = creer_socket_udp();

    if (bind_socket(sockfd_serveur, IP_SERVEUR, PORT_SERVEUR_CFG) < 0) {
        fprintf(stderr, "[ERREUR] Impossible de binder %s:%d\n",
                IP_SERVEUR, PORT_SERVEUR_CFG);
        return EXIT_FAILURE;
    }

    signal(SIGINT, gestionnaire_signal);

    boucle_serveur();

    return 0;
}
