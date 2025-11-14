/*
 * ServeurISY.c — Version finale LAN / WSL auto-IP
 * --------------------------------------------------------------
 * - Détection automatique IP WSL (eth0)
 * - Le serveur écoute directement sur IP WSL (ex: 172.29.x.x)
 * - Windows redirige l’IP LAN → IP WSL via portproxy
 * - Les clients extérieurs communiquent via IP Windows
 */

#include "../include/Commun.h"
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <string.h>
#include <pthread.h>

#define BROADCAST_PORT 9999

static Groupe groupes[MAX_GROUPES];
static int nb_groupes = 0;
static int sockfd_serveur;
static int sockfd_broadcast;
static int continuer = 1;

static char IP_WSL[TAILLE_IP] = "0.0.0.0";


/* ============================================================
   Signal handler (CTRL+C)
   ============================================================ */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[SERVEUR] Arrêt demandé.\n");
        continuer = 0;
    }
}


/* ============================================================
   Détection automatique de l’IP WSL (interface eth0)
   ============================================================ */
void detecter_ip_wsl() {
    struct ifaddrs *ifaddr, *ifa;

    printf("[INIT] Détection IP WSL...\n");

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, "eth0") == 0) {

            struct sockaddr_in* a = (struct sockaddr_in*)ifa->ifa_addr;

            inet_ntop(AF_INET, &a->sin_addr, IP_WSL, sizeof(IP_WSL));
            printf("[SUCCÈS] IP WSL = %s\n", IP_WSL);
            break;
        }
    }

    freeifaddrs(ifaddr);
}


/* ============================================================
   Thread de broadcast (répond à SERVER_DISCOVERY)
   ============================================================ */
void* thread_broadcast(void* arg) {
    (void)arg;

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char buffer[256];

    printf("[BROADCAST] En écoute sur port %d…\n", BROADCAST_PORT);

    while (continuer) {
        int bytes = recvfrom(sockfd_broadcast, buffer, sizeof(buffer)-1, 0,
                             (struct sockaddr*)&addr, &len);
        if (bytes <= 0)
            continue;

        buffer[bytes] = '\0';

        if (strcmp(buffer, "SERVER_DISCOVERY") == 0) {

            char ip_client[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, ip_client, sizeof(ip_client));

            char rep[256];
            snprintf(rep, sizeof(rep), "SERVER_HERE:%s", ip_client);

            sendto(sockfd_broadcast, rep, strlen(rep), 0,
                   (struct sockaddr*)&addr, sizeof(addr));

            printf("[BROADCAST] Réponse envoyée : %s\n", rep);
        }
    }

    return NULL;
}


/* ============================================================
   Création d’un groupe
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
   Envoi liste groupes
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

    int idx = trouver_groupe(groupes, nb_groupes, nom);
    struct struct_message msg;

    if (idx < 0) {
        construire_message(&msg, ORDRE_ERR, "Serveur", "Groupe introuvable");
        envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
        return;
    }

    char rep[TAILLE_TEXTE];
    snprintf(rep, sizeof(rep), "%s:%d", ip_client, groupes[idx].port);

    construire_message(&msg, ORDRE_OK, "Serveur", rep);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);

    printf("[SERVEUR] %s rejoint '%s' via %s\n", login, nom, ip_client);
}


/* ============================================================
   Fusion de groupes
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

    printf("\n[SERVEUR] Écoute sur %s:%d\n", IP_WSL, PORT_SERVEUR);
    printf("[INFO] Les clients doivent se connecter à l’IP Windows.\n");
    printf("[INFO] Windows redirige automatiquement vers %s\n\n", IP_WSL);

    while (continuer) {
        if (recevoir_message(sockfd_serveur, &msg, ip_src, &port_src) < 0)
            continue;

        if (strcmp(msg.Ordre, ORDRE_CRE) == 0) {
            struct struct_message rep;
            if (creer_groupe(msg.Texte, msg.Emetteur) >= 0)
                construire_message(&rep, ORDRE_OK, "Serveur", "Groupe créé");
            else
                construire_message(&rep, ORDRE_ERR, "Serveur", "Création impossible");

            envoyer_message(sockfd_serveur, &rep, ip_src, port_src);
        }

        else if (strcmp(msg.Ordre, ORDRE_LST) == 0) {
            envoyer_liste_groupes(ip_src, port_src);
        }

        else if (strcmp(msg.Ordre, ORDRE_JOIN) == 0) {
            traiter_connexion_groupe(msg.Texte, msg.Emetteur, ip_src, port_src);
        }

        else if (strcmp(msg.Ordre, ORDRE_FUS) == 0) {
            fusionner_groupes(msg.Texte, msg.Emetteur);
        }
    }
}


/* ============================================================
   MAIN
   ============================================================ */
int main() {
    printf("=== SERVEUR ISY – WSL AUTO-IP ===\n");

    detecter_ip_wsl();

    /* Création socket UDP serveur */
    sockfd_serveur = creer_socket_udp();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, IP_WSL, &addr.sin_addr);
    addr.sin_port = htons(PORT_SERVEUR);

    if (bind(sockfd_serveur, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[SERVEUR] Erreur bind");
        return EXIT_FAILURE;
    }

    /* Broadcast */
    sockfd_broadcast = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in baddr;
    memset(&baddr, 0, sizeof(baddr));
    baddr.sin_family = AF_INET;
    baddr.sin_port = htons(BROADCAST_PORT);
    baddr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd_broadcast, (struct sockaddr*)&baddr, sizeof(baddr));

    pthread_t tid;
    pthread_create(&tid, NULL, thread_broadcast, NULL);

    signal(SIGINT, gestionnaire_signal);

    boucle_serveur();

    return 0;
}
