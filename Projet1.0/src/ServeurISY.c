#include "../include/Commun.h"
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

static Groupe groupes[MAX_GROUPES];
static int nb_groupes = 0;
static int sockfd_serveur;
static int continuer = 1;

static char SERVER_IP[TAILLE_IP];
static int SERVER_PORT = PORT_SERVEUR;

/* ============================================================
   Signal handler : tuer tout proprement
   ============================================================ */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[SERVEUR] Arrêt demandé (CTRL+C).\n");

        /* Kill tous les groupes */
        for (int i = 0; i < nb_groupes; i++) {
            if (groupes[i].actif && groupes[i].pid_processus > 0) {
                printf("[SERVEUR] Kill groupe: %s (PID %d)\n",
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
   Lecture du fichier de configuration
   ============================================================ */
int lire_configuration() {
    FILE* f = fopen("config/serveur.conf", "r");
    if (!f) {
        fprintf(stderr, "[ERREUR] Impossible d'ouvrir config/serveur.conf\n");
        fprintf(stderr, "[INFO] Utilisation des valeurs par défaut: IP=%s, PORT=%d\n", 
                SERVER_IP, SERVER_PORT);
        return 0;
    }

    char ligne[256];
    while (fgets(ligne, sizeof(ligne), f)) {
        // Ignorer les lignes vides et les commentaires
        if (ligne[0] == '#' || ligne[0] == '\n' || ligne[0] == '\r')
            continue;

        // Nettoyer la ligne
        nettoyer_chaine(ligne);

        // Parser IP=...
        if (strncmp(ligne, "IP=", 3) == 0) {
            strncpy(SERVER_IP, ligne + 3, TAILLE_IP - 1);
            SERVER_IP[TAILLE_IP - 1] = '\0';
        }
        // Parser PORT=...
        else if (strncmp(ligne, "PORT=", 5) == 0) {
            SERVER_PORT = atoi(ligne + 5);
        }
    }

    fclose(f);
    printf("[CONFIG] IP du serveur: %s\n", SERVER_IP);
    printf("[CONFIG] Port du serveur: %d\n", SERVER_PORT);
    return 1;
}

/* ============================================================
   Création d'un groupe
   ============================================================ */
int creer_groupe(const char* nom, const char* moderateur) {
    if (trouver_groupe(groupes, nb_groupes, nom) >= 0) return -1;
    if (nb_groupes >= MAX_GROUPES) return -1;

    int idx = nb_groupes;

    strncpy(groupes[idx].nom, nom, TAILLE_NOM_GROUPE - 1);
    strncpy(groupes[idx].moderateur, moderateur, TAILLE_LOGIN - 1);

    groupes[idx].port = PORT_GROUPE_BASE + idx;
    groupes[idx].actif = 1;

    printf("[SERVEUR] Création du groupe '%s' (port %d)\n",
           groupes[idx].nom, groupes[idx].port);

    pid_t pid = fork();
    if (pid == 0) {
        char port_str[10];
        sprintf(port_str, "%d", groupes[idx].port);
        execl("./bin/GroupeISY", "GroupeISY", nom, moderateur, port_str, NULL);
        exit(EXIT_FAILURE);
    }

    groupes[idx].pid_processus = pid;
    nb_groupes++;
    return idx;
}

/* ============================================================
   Liste groupes
   ============================================================ */
void envoyer_liste_groupes(const char* ip, int port) {
    struct struct_message msg;
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
   Rejoindre un groupe
   ============================================================ */
void traiter_connexion_groupe(const char* nom, const char* login,
                              const char* ip_client, int port_client) {

    struct struct_message msg;
    int idx = trouver_groupe(groupes, nb_groupes, nom);

    if (idx < 0) {
        construire_message(&msg, ORDRE_ERR, "Serveur", "Groupe introuvable");
        envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
        return;
    }

    char rep[TAILLE_TEXTE];
    snprintf(rep, sizeof(rep), "%s:%d", SERVER_IP, groupes[idx].port);

    construire_message(&msg, ORDRE_OK, "Serveur", rep);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);

    printf("[SERVEUR] %s rejoint '%s'\n", login, nom);
}

/* ============================================================
   Fusion
   ============================================================ */
void fusionner_groupes(const char* data, const char* demandeur) {
    char a[30], b[30], newname[30];
    if (sscanf(data, "%[^:]:%[^:]:%s", a, b, newname) != 3)
        return;

    int g1 = trouver_groupe(groupes, nb_groupes, a);
    int g2 = trouver_groupe(groupes, nb_groupes, b);

    if (g1 < 0 || g2 < 0)
        return;

    kill(groupes[g2].pid_processus, SIGTERM);
    waitpid(groupes[g2].pid_processus, NULL, 0);
    groupes[g2].actif = 0;

    creer_groupe(newname, demandeur);
}

/* ============================================================
   Boucle principale
   ============================================================ */
void boucle_serveur() {
    struct struct_message msg;
    char ip_src[TAILLE_IP];
    int port_src;

    printf("\n[SERVEUR] Écoute sur %s:%d\n\n", SERVER_IP, SERVER_PORT);

    while (continuer) {
        if (recevoir_message(sockfd_serveur, &msg, ip_src, &port_src) < 0)
            continue;

        if (strcmp(msg.Ordre, ORDRE_CRE) == 0) {
            struct struct_message rep;
            if (creer_groupe(msg.Texte, msg.Emetteur) >= 0)
                construire_message(&rep, ORDRE_OK, "Serveur", "Groupe créé");
            else
                construire_message(&rep, ORDRE_ERR, "Serveur", "Erreur création");

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
int main() {
    printf("=== SERVEUR ISY ===\n");

    // Lire la configuration
    lire_configuration();

    sockfd_serveur = creer_socket_udp();
    if (sockfd_serveur < 0) {
        fprintf(stderr, "[ERREUR] Impossible de créer le socket\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    
    if (inet_pton(AF_INET, SERVER_IP, &addr.sin_addr) <= 0) {
        fprintf(stderr, "[ERREUR] Adresse IP invalide: %s\n", SERVER_IP);
        close(sockfd_serveur);
        return EXIT_FAILURE;
    }
    
    addr.sin_port = htons(SERVER_PORT);

    if (bind(sockfd_serveur, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[SERVEUR] Erreur bind");
        close(sockfd_serveur);
        return EXIT_FAILURE;
    }

    signal(SIGINT, gestionnaire_signal);

    boucle_serveur();

    return 0;
}
