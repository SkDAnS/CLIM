/*
 * ServeurISY.c
 * Version finale pour WSL + portproxy Windows (manuel)
 * ----------------------------------------------------------------------
 * - Ce serveur tourne dans WSL
 * - Windows redirige 0.0.0.0:8000-8010 → IP_WSL:8000-8010 (portproxy)
 * - Les clients se connectent à l’IP Windows (192.168.x.x)
 * - Le serveur n’a PLUS aucune logique PowerShell
 * - Aucune détection IP Windows (inutile)
 * - Communication 100% fonctionnelle entre PC extérieurs et WSL
 * ----------------------------------------------------------------------
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

/* ---------------------------------------------------------
   L’IP WSL (172.x.x.x)
   Elle est détectée automatiquement via l’interface eth0
   --------------------------------------------------------- */
static char IP_WSL[TAILLE_IP] = "0.0.0.0";

/* ---------------------------------------------------------
   Détecter l’IP de WSL (eth0)
   --------------------------------------------------------- */
void recuperer_ip_wsl_locale() {
    printf("[INIT] Détection de l'IP WSL locale...\n");

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;

        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, "eth0") == 0) {

            struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, IP_WSL, sizeof(IP_WSL));

            printf("[SUCCÈS] IP WSL détectée : %s (eth0)\n", IP_WSL);
            break;
        }
    }
    freeifaddrs(ifaddr);
}

/* ---------------------------------------------------------
   Gestion signal pour arrêter proprement
   --------------------------------------------------------- */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[SERVEUR] Arrêt demandé.\n");
        continuer = 0;
    }
}

/* ---------------------------------------------------------
   THREAD : réponse au broadcast des clients
   --------------------------------------------------------- */
void* thread_broadcast(void* arg) {
    (void)arg; // éviter warning unused parameter

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[256];

    printf("[BROADCAST] Écoute active sur port %d…\n", BROADCAST_PORT);

    while (continuer) {
        int bytes = recvfrom(sockfd_broadcast, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr*)&addr, &addrlen);
        if (bytes <= 0) continue;

        buffer[bytes] = '\0';

        if (strcmp(buffer, "SERVER_DISCOVERY") == 0) {

            /* ----------------------------------------------------
               IMPORTANT :
               On répond avec l’IP que Windows transmet aux clients.
               Windows utilise son IP locale (192.168.x.x)
               grâce au portproxy → WSL.
               ---------------------------------------------------- */

            char rep[256];
            /* On renvoie l’IP de l’expéditeur (Windows joue la passerelle) */
            inet_ntop(AF_INET, &addr.sin_addr, rep, sizeof(rep));

            char final[256];
snprintf(final, sizeof(final), "SERVER_HERE:%.240s", rep);

            sendto(sockfd_broadcast, final, strlen(final), 0,
                   (struct sockaddr*)&addr, sizeof(addr));

            printf("[BROADCAST] Demande reçue → réponse: %s\n", final);
        }
    }
    return NULL;
}

/* ---------------------------------------------------------
   Créer un groupe
   --------------------------------------------------------- */
int creer_groupe(const char* nom_groupe, const char* moderateur) {
    if (trouver_groupe(groupes, nb_groupes, nom_groupe) >= 0) return -1;
    if (nb_groupes >= MAX_GROUPES) return -1;

    int idx = nb_groupes;

    strncpy(groupes[idx].nom, nom_groupe, sizeof(groupes[idx].nom) - 1);
    strncpy(groupes[idx].moderateur, moderateur, sizeof(groupes[idx].moderateur) - 1);

    groupes[idx].port = PORT_GROUPE_BASE + idx;
    groupes[idx].actif = 1;

    printf("[SERVEUR] Création du groupe '%s' (port %d)\n",
           nom_groupe, groupes[idx].port);

    pid_t pid = fork();
    if (pid == 0) {
        char port_str[10];
        sprintf(port_str, "%d", groupes[idx].port);
        execl("./bin/GroupeISY", "GroupeISY", nom_groupe, moderateur, port_str, NULL);
        exit(EXIT_FAILURE);
    }

    groupes[idx].pid_processus = pid;
    nb_groupes++;

    return idx;
}

/* ---------------------------------------------------------
   Envoyer liste de groupes
   --------------------------------------------------------- */
void envoyer_liste_groupes(const char* ip, int port) {
    char texte[TAILLE_TEXTE] = "";
    int count = 0;

    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif) {
            if (count > 0) strncat(texte, ", ", sizeof(texte) - strlen(texte) - 1);
            strncat(texte, groupes[i].nom, sizeof(texte) - strlen(texte) - 1);
            count++;
        }
    }

    if (count == 0) strcpy(texte, "Aucun groupe");

    struct struct_message msg;
    construire_message(&msg, ORDRE_INFO, "Serveur", texte);
    envoyer_message(sockfd_serveur, &msg, ip, port);
}

/* ---------------------------------------------------------
   Rejoindre un groupe
   --------------------------------------------------------- */
void traiter_connexion_groupe(const char* nom, const char* login,
                              const char* ip_client, int port_client) {

    struct struct_message msg;
    int idx = trouver_groupe(groupes, nb_groupes, nom);

    if (idx < 0) {
        construire_message(&msg, ORDRE_ERR, "Serveur", "Groupe inexistant");
        envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
        return;
    }

    /* ---------------------------------------------------------
       IMPORTANT :
       On renvoie l’IP VUE PAR LE CLIENT (celle de Windows).
       Grâce au portproxy → WSL reçoit le message.
       --------------------------------------------------------- */
    char info[TAILLE_TEXTE];
    snprintf(info, sizeof(info), "%s:%d", ip_client, groupes[idx].port);

    construire_message(&msg, ORDRE_OK, "Serveur", info);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);

    printf("[SERVEUR] %s rejoint '%s' via %s:%d\n",
           login, nom, ip_client, groupes[idx].port);
}

/* ---------------------------------------------------------
   Fusionner groupes
   --------------------------------------------------------- */
void fusionner_groupes(const char* data, const char* demandeur) {
    char g1[TAILLE_NOM_GROUPE], g2[TAILLE_NOM_GROUPE], newname[TAILLE_NOM_GROUPE];

    if (sscanf(data, "%[^:]:%[^:]:%s", g1, g2, newname) != 3) return;

    int idx1 = trouver_groupe(groupes, nb_groupes, g1);
    int idx2 = trouver_groupe(groupes, nb_groupes, g2);

    if (idx1 < 0 || idx2 < 0) return;

    if (groupes[idx2].pid_processus > 0) {
        kill(groupes[idx2].pid_processus, SIGTERM);
        waitpid(groupes[idx2].pid_processus, NULL, 0);
    }
    groupes[idx2].actif = 0;

    creer_groupe(newname, demandeur);
}

/* ---------------------------------------------------------
   Boucle principale
   --------------------------------------------------------- */
void boucle_serveur() {
    struct struct_message msg;
    char ip_src[TAILLE_IP];
    int port_src;

    printf("\n[SERVEUR] En attente sur 0.0.0.0:%d\n", PORT_SERVEUR);
    printf("[INFO] Les clients doivent se connecter à l'IP Windows.\n");
    printf("[INFO] Windows redirige automatiquement vers WSL.\n\n");

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

        } else if (strcmp(msg.Ordre, ORDRE_LST) == 0) {
            envoyer_liste_groupes(ip_src, port_src);

        } else if (strcmp(msg.Ordre, ORDRE_JOIN) == 0) {
            traiter_connexion_groupe(msg.Texte, msg.Emetteur, ip_src, port_src);

        } else if (strcmp(msg.Ordre, ORDRE_FUS) == 0) {
            fusionner_groupes(msg.Texte, msg.Emetteur);
        }
    }
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */
int main() {
    printf("=== SERVEUR ISY – MODE WSL + PORTPROXY WINDOWS ===\n");

    recuperer_ip_wsl_locale();

    /* Socket serveur UDP */
    sockfd_serveur = creer_socket_udp();
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  
    addr.sin_port = htons(PORT_SERVEUR);

    if (bind(sockfd_serveur, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[SERVEUR] Erreur bind");
        return EXIT_FAILURE;
    }

    /* Socket broadcast */
    sockfd_broadcast = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in baddr;
    memset(&baddr, 0, sizeof(baddr));
    baddr.sin_family = AF_INET;
    baddr.sin_port = htons(BROADCAST_PORT);
    baddr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd_broadcast, (struct sockaddr*)&baddr, sizeof(baddr));

    pthread_t tid_broadcast;
    pthread_create(&tid_broadcast, NULL, thread_broadcast, NULL);

    signal(SIGINT, gestionnaire_signal);

    boucle_serveur();

    return 0;
}
