#include "../include/Commun.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <locale.h>
#include <wchar.h>
#include <sys/select.h>

/* ---------------- CONFIG ---------------- */

static char SERVER_IP[TAILLE_IP];
static char CLIENT_IP[TAILLE_IP];
static char login[TAILLE_LOGIN];
static int display_port = 9001;

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

/* ---------------- AVATARS ---------------- */

unsigned long simple_hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

const char* get_avatar(const char* login) {
    static char buffer[8];
    setlocale(LC_CTYPE, "");

    unsigned long base_unicode = 0x2600;
    unsigned long range = 0x26FF - 0x2600 + 1;
    unsigned long codepoint = base_unicode + (simple_hash(login) % range);

    snprintf(buffer, sizeof(buffer), "%lc", (wint_t)codepoint);
    return buffer;
}

/* ---------------- CONFIG CLIENT ---------------- */

void load_client_config(char *client_ip_out, char *username_out, int *display_port_out)
{
    FILE *f = fopen("client.conf", "r");
    if (!f) {
        perror("[ERREUR] client.conf");
        exit(1);
    }

    char key[64], val[128], line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%63[^=]=%127s", key, val) == 2) {

            if (strcmp(key, "client_ip") == 0)
                strncpy(client_ip_out, val, TAILLE_IP - 1);

            else if (strcmp(key, "username") == 0)
                strncpy(username_out, val, TAILLE_LOGIN - 1);

            else if (strcmp(key, "display_port") == 0)
                *display_port_out = atoi(val);
        }
    }

    fclose(f);
}

void save_client_config(const char *client_ip, const char *username, int display_port)
{
    FILE *f = fopen("client.conf", "w");
    if (!f) {
        perror("[ERREUR] écriture client.conf");
        return;
    }

    fprintf(f, "client_ip=%s\n", client_ip);
    fprintf(f, "username=%s\n", username);
    fprintf(f, "display_port=%d\n", display_port);

    fclose(f);
}

/* ---------------- UTIL ---------------- */

void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[CLIENT] CTRL-C...\n");
        continuer = 0;
    }
}

void afficher_menu() {
    printf("\n=== MENU === (Groupe actuel : %s)\n", groupe_actuel);
    printf("0 - Creer groupe\n");
    printf("1 - Rejoindre groupe\n");
    printf("2 - Lister groupes\n");
    printf("3 - Dialoguer\n");
    printf("4 - Fusionner groupes\n");
    printf("5 - Quitter\n");
    printf("Choix: ");
}

/* ---------------- ACTIONS ---------------- */

void creer_groupe() {
    char nom[TAILLE_NOM_GROUPE];
    printf("\nNom du groupe: ");
    if (fgets(nom, TAILLE_NOM_GROUPE, stdin) == NULL) return;
    nettoyer_chaine(nom);
    if (strlen(nom) == 0) return;

    struct struct_message msg, rep;
    construire_message(&msg, ORDRE_CRE, login, nom);
    envoyer_message(sockfd_client, &msg, SERVER_IP, PORT_SERVEUR);

    char ip[TAILLE_IP];
    int port;
    if (recevoir_message(sockfd_client, &rep, ip, &port) >= 0) {
        if (strcmp(rep.Ordre, ORDRE_OK) == 0)
            printf("[OK] Groupe '%s' créé\n", nom);
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
        printf("\n--- Groupes ---\n%s\n", rep.Texte);
}

void rejoindre_groupe() {
    char nom[TAILLE_NOM_GROUPE];
    printf("\nNom du groupe: ");
    if (fgets(nom, TAILLE_NOM_GROUPE, stdin) == NULL) return;
    nettoyer_chaine(nom);
    if (strlen(nom) == 0) return;

    struct struct_message msg, rep;
    construire_message(&msg, ORDRE_JOIN, login, nom);
    envoyer_message(sockfd_client, &msg, SERVER_IP, PORT_SERVEUR);

    char ip_grp[TAILLE_IP];
    int port_grp;

    if (recevoir_message(sockfd_client, &rep, ip_grp, &port_grp) < 0) return;

    if (strcmp(rep.Ordre, ORDRE_ERR) == 0) {
        printf("[ERREUR] %s\n", rep.Texte);
        return;
    }

    if (sscanf(rep.Texte, "%[^:]:%d", ip_grp, &port_grp) != 2) return;

    printf("[OK] Connexion à '%s' (Port: %d)\n", nom, port_grp);

    int idx = nb_groupes_rejoints++;
    strncpy(groupes[idx].nom, nom, TAILLE_NOM_GROUPE - 1);
    strncpy(groupes[idx].ip, ip_grp, TAILLE_IP - 1);
    groupes[idx].port = port_grp;
    groupes[idx].actif = 1;

    struct struct_message msg_con;
    construire_message(&msg_con, ORDRE_CON, login, "");
    envoyer_message(sockfd_client, &msg_con, ip_grp, port_grp);

    strncpy(groupe_actuel, nom, TAILLE_NOM_GROUPE - 1);
}

void fusionner_groupes() {
    if (nb_groupes_rejoints < 2) {
        printf("\n[ERREUR] Vous devez avoir rejoint au moins deux groupes.\n");
        return;
    }

    printf("\n--- Mes groupes ---\n");
    for (int i = 0; i < nb_groupes_rejoints; i++)
        if (groupes[i].actif) printf("%d. %s\n", i, groupes[i].nom);

    int g1, g2;
    printf("Groupe 1: ");
    scanf("%d", &g1);
    printf("Groupe 2: ");
    scanf("%d", &g2);
    while (getchar()!='\n');

    if (g1 < 0 || g1 >= nb_groupes_rejoints || g2 < 0 || g2 >= nb_groupes_rejoints) {
        printf("[ERREUR] Invalides.\n");
        return;
    }

    char data[TAILLE_TEXTE];
    snprintf(data, sizeof(data), "%s:%s",
             groupes[g1].nom, groupes[g2].nom);

    struct struct_message msg;
    construire_message(&msg, ORDRE_FUS, login, data);
    envoyer_message(sockfd_client, &msg, SERVER_IP, PORT_SERVEUR);

    printf("[INFO] Fusion demandée.\n");
}

void dialoguer() {
    if (nb_groupes_rejoints == 0) {
        printf("\nAucun groupe rejoint\n");
        return;
    }

    printf("\n--- Mes groupes ---\n");
    for (int i = 0; i < nb_groupes_rejoints; i++)
        if (groupes[i].actif)
            printf("%d. %s\n", i, groupes[i].nom);

    printf("Numero: ");
    int choix; scanf("%d", &choix);
    while (getchar()!='\n');
    if (choix < 0 || choix >= nb_groupes_rejoints) return;

    printf("\n[DIALOGUE] Groupe: %s\n", groupes[choix].nom);

    fd_set readfds;
    struct timeval tv;
    struct struct_message msg;
    char buffer[TAILLE_TEXTE];
    char ip_src[TAILLE_IP];
    int port_src;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd_client, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int maxfd = (sockfd_client > STDIN_FILENO ? sockfd_client : STDIN_FILENO);
        int activity = select(maxfd+1,&readfds,NULL,NULL,&tv);

        if (FD_ISSET(sockfd_client, &readfds)) {
            if (recevoir_message(sockfd_client,&msg,ip_src,&port_src)>=0) {
                if (!strcmp(msg.Ordre, ORDRE_MES)) {
                    printf("[%s] %s: %s\n",
                           get_avatar(msg.Emetteur),
                           msg.Emetteur,
                           msg.Texte);
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (!fgets(buffer, sizeof(buffer), stdin)) break;
            nettoyer_chaine(buffer);
            if (!strcmp(buffer,"quit")) break;

            construire_message(&msg, ORDRE_MES, login, buffer);
            envoyer_message(sockfd_client, &msg,
                            groupes[choix].ip,
                            groupes[choix].port);
        }
    }
}

/* ---------------- MAIN ---------------- */

int main(void) {
    printf("=== CLIENT ISY ===\n");

    load_client_config(CLIENT_IP, login, &display_port);
    load_server_config(SERVER_IP); /* Serveur lu dans serveur.conf */

    printf("Client IP  : %s\n", CLIENT_IP);
    printf("Username   : %s\n", login);
    printf("Serveur IP : %s\n", SERVER_IP);
    printf("Port aff.  : %d\n\n", display_port);

    printf("Entrez votre nom (%s par défaut): ", login);

    char saisie[TAILLE_LOGIN];
    if (fgets(saisie, sizeof(saisie), stdin)) {
        nettoyer_chaine(saisie);
        if (strlen(saisie) > 0 && strcmp(saisie, login) != 0) {
            printf("Nouvel utilisateur détecté : %s\n", saisie);
            strncpy(login, saisie, TAILLE_LOGIN - 1);
            save_client_config(CLIENT_IP, login, display_port);
        } else {
            printf("Bienvenue de retour %s !\n", login);
        }
    }

    sockfd_client = creer_socket_udp();
    signal(SIGINT, gestionnaire_signal);

    int choix;
    while (continuer) {
        afficher_menu();
        if (scanf("%d",&choix)!=1) {
            while(getchar()!='\n');
            continue;
        }
        while (getchar()!='\n');

        switch (choix) {
            case 0: creer_groupe(); break;
            case 1: rejoindre_groupe(); break;
            case 2: lister_groupes(); break;
            case 3: dialoguer(); break;
            case 4: fusionner_groupes(); break;
            case 5: continuer=0; break;
            default: break;
        }
    }

    close(sockfd_client);
    printf("[CLIENT] Termine\n");
    return EXIT_SUCCESS;
}
