/*
 * ClientISY.c – Version finale AUTOMATIQUE (LAN + WSL)
 * -----------------------------------------------------
 * - Découverte automatique du serveur via broadcast
 * - Récupère automatiquement l’IP du serveur (IP Windows)
 * - Fonctionne avec portproxy Windows → WSL
 * - Aucune saisie d’adresse IP
 * - Dialogue, création de groupes, etc.
 */

#include "../include/Commun.h"
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <locale.h>
#include <wchar.h>
#include <signal.h>

/* ---------------- CONFIG ---------------- */
#define BROADCAST_PORT 9999
static char SERVER_IP[TAILLE_IP] = "127.0.0.1";

static char login[TAILLE_LOGIN];
static int sockfd_client;
static int continuer = 1;
static char groupe_actuel[TAILLE_NOM_GROUPE] = "Aucun";

typedef struct {
    char nom[TAILLE_NOM_GROUPE];
    char ip[TAILLE_IP];
    int port;
    int actif;
} GroupeClient;

static GroupeClient groupes[MAX_GROUPES];
static int nb_groupes_rejoints = 0;


/* ============================================================
   SIGNAL HANDLER
   ============================================================ */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[CLIENT] Fermeture...\n");
        continuer = 0;
        close(sockfd_client);
        exit(0);
    }
}


/* ============================================================
   DÉCOUVERTE AUTOMATIQUE DU SERVEUR
   ============================================================ */
int decouvrir_serveur(char* ip_serveur, size_t size) {
    printf("[DÉCOUVERTE] Recherche du serveur sur le LAN...\n");

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct timeval tv = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BROADCAST_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const char* msg = "SERVER_DISCOVERY";
    sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&addr, sizeof(addr));

    printf("[DÉCOUVERTE] Broadcast envoyé…\n");

    char buffer[256];
    struct sockaddr_in saddr;
    socklen_t slen = sizeof(saddr);

    int bytes = recvfrom(sock, buffer, sizeof(buffer)-1, 0,
                         (struct sockaddr*)&saddr, &slen);

    close(sock);

    if (bytes <= 0) {
        printf("[DÉCOUVERTE] ✗ Aucun serveur trouvé\n");
        return -1;
    }

    buffer[bytes] = '\0';

    if (strncmp(buffer, "SERVER_HERE:", 12) != 0)
        return -1;

    char* ip = buffer + 12;
    while (*ip == ' ' || *ip == '\n' || *ip == '\r' || *ip == '\t')
        ip++;

    strncpy(ip_serveur, ip, size - 1);
    ip_serveur[size - 1] = '\0';

    printf("[DÉCOUVERTE] ✓ Serveur trouvé : %s\n\n", ip_serveur);
    return 0;
}


/* ============================================================
   AVATARS
   ============================================================ */
unsigned long simple_hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

const char* get_avatar(const char* login, const char* ip) {
    static char buffer[8];
    setlocale(LC_CTYPE, "");

    unsigned long base_unicode = 0x2600;
    unsigned long range = 0x26FF - 0x2600 + 1;
    unsigned long hash_value = simple_hash(ip);

    unsigned long codepoint = base_unicode + (hash_value % range);
    snprintf(buffer, sizeof(buffer), "%lc", (wint_t)codepoint);
    return buffer;
}


/* ============================================================
   MENU
   ============================================================ */
void afficher_menu() {
    printf("\n=== MENU === (Groupe : %s)\n", groupe_actuel);
    printf("0 - Créer groupe\n");
    printf("1 - Rejoindre groupe\n");
    printf("2 - Lister groupes\n");
    printf("3 - Dialoguer\n");
    printf("4 - Quitter\n");
    printf("Choix : ");
}


/* ============================================================
   ACTIONS
   ============================================================ */
void creer_groupe() {
    char nom[TAILLE_NOM_GROUPE];
    printf("\nNom du groupe: ");
    fgets(nom, sizeof(nom), stdin);
    nettoyer_chaine(nom);

    struct struct_message msg, rep;
    construire_message(&msg, ORDRE_CRE, login, nom);
    envoyer_message(sockfd_client, &msg, SERVER_IP, PORT_SERVEUR);

    char ip[TAILLE_IP];
    int port;

    if (recevoir_message(sockfd_client, &rep, ip, &port) >= 0) {
        if (!strcmp(rep.Ordre, ORDRE_OK))
            printf("[OK] Groupe créé.\n");
        else
            printf("[ERREUR] %s\n", rep.Texte);
    }
}

void lister_groupes() {
    struct struct_message msg, rep;
    construire_message(&msg, ORDRE_LST, login, "");
    envoyer_message(sockfd_client, &msg, SERVER_IP, PORT_SERVEUR);

    char ip[TAILLE_IP];
    int port;

    if (recevoir_message(sockfd_client, &rep, ip, &port) >= 0)
        printf("--- Groupes ---\n%s\n", rep.Texte);
}

void rejoindre_groupe() {
    char nom[TAILLE_NOM_GROUPE];
    printf("\nNom du groupe: ");
    fgets(nom, sizeof(nom), stdin);
    nettoyer_chaine(nom);

    struct struct_message msg, rep;
    construire_message(&msg, ORDRE_JOIN, login, nom);
    envoyer_message(sockfd_client, &msg, SERVER_IP, PORT_SERVEUR);

    char ipg[TAILLE_IP];
    int portg;

    if (recevoir_message(sockfd_client, &rep, ipg, &portg) < 0)
        return;

    if (!strcmp(rep.Ordre, ORDRE_ERR)) {
        printf("[ERREUR] %s\n", rep.Texte);
        return;
    }

    char ip_grp[TAILLE_IP];
    int port_grp;
    sscanf(rep.Texte, "%[^:]:%d", ip_grp, &port_grp);

    int idx = nb_groupes_rejoints++;
    strncpy(groupes[idx].nom, nom, sizeof(groupes[idx].nom));
    strncpy(groupes[idx].ip, ip_grp, sizeof(groupes[idx].ip));
    groupes[idx].port = port_grp;
    groupes[idx].actif = 1;

    struct struct_message cnx;
    construire_message(&cnx, ORDRE_CON, login, "");
    envoyer_message(sockfd_client, &cnx, groupes[idx].ip, groupes[idx].port);

    strncpy(groupe_actuel, nom, sizeof(groupe_actuel));
    printf("[INFO] Connecté au groupe.\n");
}


/* ============================================================
   DIALOGUE
   ============================================================ */
void dialoguer() {
    if (nb_groupes_rejoints == 0) {
        printf("Aucun groupe.\n");
        return;
    }

    printf("\n--- Vos groupes ---\n");
    for (int i = 0; i < nb_groupes_rejoints; i++)
        if (groupes[i].actif)
            printf("%d. %s\n", i, groupes[i].nom);

    printf("\nNuméro: ");
    int choix;
    scanf("%d", &choix);
    while (getchar() != '\n');

    printf("\n[DIALOGUE] Groupe : %s\n", groupes[choix].nom);

    fd_set fds;
    struct timeval tv;
    struct struct_message msg;
    char buffer[TAILLE_TEXTE];
    char ip_src[TAILLE_IP];
    int port_src;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(sockfd_client, &fds);
        FD_SET(STDIN_FILENO, &fds);

        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int maxfd = (sockfd_client > STDIN_FILENO) ? sockfd_client : STDIN_FILENO;
        select(maxfd + 1, &fds, NULL, NULL, &tv);

        if (FD_ISSET(sockfd_client, &fds)) {
            if (recevoir_message(sockfd_client, &msg, ip_src, &port_src) >= 0) {
                if (!strcmp(msg.Ordre, ORDRE_MES))
                    printf("[%s] %s: %s\n",
                           get_avatar(msg.Emetteur, ip_src),
                           msg.Emetteur, msg.Texte);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            fgets(buffer, sizeof(buffer), stdin);
            nettoyer_chaine(buffer);

            if (!strcmp(buffer, "quit"))
                break;

            construire_message(&msg, ORDRE_MES, login, buffer);
            envoyer_message(sockfd_client, &msg,
                            groupes[choix].ip, groupes[choix].port);
        }
    }
}


/* ============================================================
   MAIN
   ============================================================ */
int main() {
    printf("=== CLIENT ISY ===\n");

    /* → AUTOMATIQUE : on découvre l’IP Windows */
    if (decouvrir_serveur(SERVER_IP, sizeof(SERVER_IP)) != 0) {
        printf("Impossible de trouver le serveur.\n");
        return 1;
    }

    sockfd_client = creer_socket_udp();

    printf("Votre nom: ");
    fgets(login, sizeof(login), stdin);
    nettoyer_chaine(login);

    signal(SIGINT, gestionnaire_signal);

    int choix;
    while (continuer) {
        afficher_menu();

        if (scanf("%d", &choix) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');

        switch (choix) {
            case 0: creer_groupe(); break;
            case 1: rejoindre_groupe(); break;
            case 2: lister_groupes(); break;
            case 3: dialoguer(); break;
            case 4: continuer = 0; break;
        }
    }

    close(sockfd_client);
    return 0;
}
