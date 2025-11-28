#include "../include/Commun.h"
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <locale.h>
#include <wchar.h>

static char SERVER_IP[TAILLE_IP];
static char login[TAILLE_LOGIN];  // Déclaration de la variable login
static char groupe_actuel[TAILLE_NOM_GROUPE];  // Déclaration de la variable groupe_actuel
static int sockfd_client;  // Déclaration de la variable sockfd_client
static int nb_groupes_rejoints = 0;  // Déclaration de nb_groupes_rejoints
static Groupe groupes[MAX_GROUPES];  // Déclaration de groupes
static int continuer = 1;  // Déclaration de la variable continuer

/* ====================== SIGNAL HANDLER ====================== */

// Définition de la fonction gestionnaire_signal
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[CLIENT] Arrêt demandé (CTRL+C).\n");
        continuer = 0;  // Cela arrêtera la boucle principale
    }
}

/* ====================== UTILITAIRES ====================== */

/* Lecture du fichier de configuration */
int lire_configuration() {
    FILE* f = fopen("config/client.conf", "r");
    if (!f) {
        fprintf(stderr, "[AVERTISSEMENT] Impossible d'ouvrir config/client.conf\n");
        return 0;
    }

    char ligne[256];
    while (fgets(ligne, sizeof(ligne), f)) {
        // Ignorer les lignes vides et les commentaires
        if (ligne[0] == '#' || ligne[0] == '\n' || ligne[0] == '\r')
            continue;

        // Nettoyer la ligne
        nettoyer_chaine(ligne);

        // Parser SERVER_IP=...
        if (strncmp(ligne, "SERVER_IP=", 10) == 0) {
            strncpy(SERVER_IP, ligne + 10, TAILLE_IP - 1);
            SERVER_IP[TAILLE_IP - 1] = '\0';
        }
    }

    fclose(f);
    printf("[CONFIG] IP du serveur: %s\n", SERVER_IP);
    return 1;
}

/* Petit hachage stable pour login */
unsigned long simple_hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/* Avatar UTF-8 entre U+2600 et U+26FF */
const char* get_avatar(const char* login) {
    static char buffer[8];
    setlocale(LC_CTYPE, "");

    unsigned long base_unicode = 0x2600; // ☀
    unsigned long range = 0x26FF - 0x2600 + 1; // 256 symboles
    unsigned long hash_value = simple_hash(login);

    unsigned long codepoint = base_unicode + (hash_value % range);
    snprintf(buffer, sizeof(buffer), "%lc", (wint_t)codepoint);
    return buffer;
}

/* ====================== MENU ====================== */

void afficher_menu() {
    printf("\n=== MENU === (Groupe actuel : %s)\n", groupe_actuel);
    printf("0 - Créer groupe\n");
    printf("1 - Rejoindre groupe\n");
    printf("2 - Lister groupes\n");
    printf("3 - Dialoguer\n");
    printf("4 - Fusionner groupes\n");
    printf("5 - Quitter\n");
    printf("Choix: ");
}

/* ====================== ACTIONS ====================== */

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

    char ip[TAILLE_IP];
    int port;
    if (recevoir_message(sockfd_client, &rep, ip, &port) < 0) return;

    if (strcmp(rep.Ordre, ORDRE_ERR) == 0) {
        printf("[ERREUR] %s\n", rep.Texte);
        return;
    }

    char ip_groupe[TAILLE_IP];
    int port_groupe;
    if (sscanf(rep.Texte, "%[^:]:%d", ip_groupe, &port_groupe) != 2) return;

    printf("[OK] Connexion à '%s' (Port: %d)\n", nom, port_groupe);

    int idx = nb_groupes_rejoints;
    if (idx < MAX_GROUPES) {
        strncpy(groupes[idx].nom, nom, TAILLE_NOM_GROUPE - 1);
        groupes[idx].port = port_groupe;
        groupes[idx].actif = 1;
        nb_groupes_rejoints++;
    }

    struct struct_message msg_con;
    construire_message(&msg_con, ORDRE_CON, login, "");
    envoyer_message(sockfd_client, &msg_con, ip_groupe, port_groupe);
    printf("[INFO] Connecté au groupe '%s'\n", nom);

    strncpy(groupe_actuel, nom, TAILLE_NOM_GROUPE - 1);
}

/* ====================== FUSION TOTALE ====================== */

void fusionner_groupes() {
    if (nb_groupes_rejoints < 2) {
        printf("\n[ERREUR] Vous devez avoir rejoint au moins deux groupes pour les fusionner.\n");
        return;
    }

    printf("\n--- Mes groupes ---\n");
    for (int i = 0; i < nb_groupes_rejoints; i++)
        if (groupes[i].actif) printf("%d. %s\n", i, groupes[i].nom);

    int g1, g2;
    printf("Groupe 1 (à garder): ");
    if (scanf("%d", &g1) != 1) { while (getchar() != '\n'); return; }
    printf("Groupe 2 (à fusionner): ");
    if (scanf("%d", &g2) != 1) { while (getchar() != '\n'); return; }
    while (getchar() != '\n');

    if (g1 < 0 || g1 >= nb_groupes_rejoints || g2 < 0 || g2 >= nb_groupes_rejoints) {
        printf("[ERREUR] Indices invalides.\n");
        return;
    }

    if (!groupes[g1].actif || !groupes[g2].actif) {
        printf("[ERREUR] L'un des groupes sélectionnés est inactif.\n");
        return;
    }

    char nouveau_nom[TAILLE_NOM_GROUPE];
    snprintf(nouveau_nom, sizeof(nouveau_nom), "%.*s_%.*s",
         (int)(sizeof(nouveau_nom)/2 - 2), groupes[g1].nom,
         (int)(sizeof(nouveau_nom)/2 - 2), groupes[g2].nom);
    nouveau_nom[sizeof(nouveau_nom) - 1] = '\0';

    printf("Entrez le nom du groupe fusionné (par défaut: %s): ", nouveau_nom);
    char saisie[TAILLE_NOM_GROUPE];
    if (fgets(saisie, sizeof(saisie), stdin)) {
        nettoyer_chaine(saisie);
        if (strlen(saisie) > 0)
            strncpy(nouveau_nom, saisie, TAILLE_NOM_GROUPE - 1);
    }

    char data[TAILLE_TEXTE];
    snprintf(data, sizeof(data), "%s:%s:%s", groupes[g1].nom, groupes[g2].nom, nouveau_nom);
    struct struct_message msg;
    construire_message(&msg, ORDRE_FUS, login, data);
    envoyer_message(sockfd_client, &msg, SERVER_IP, PORT_SERVEUR);

    printf("[INFO] Demande de fusion envoyée au serveur...\n");
    printf("[INFO] Tous les anciens groupes seront supprimés.\n");
    printf("[INFO] Votre groupe actuel est désormais 'Aucun'.\n");

    for (int i = 0; i < nb_groupes_rejoints; i++)
        groupes[i].actif = 0;

    nb_groupes_rejoints = 0;
    strncpy(groupe_actuel, "Aucun", TAILLE_NOM_GROUPE - 1);
}

/* ====================== DIALOGUE ====================== */

void dialoguer() {
    if (nb_groupes_rejoints == 0) {
        printf("\nAucun groupe rejoint\n");
        return;
    }

    printf("\n--- Mes groupes ---\n");
    for (int i = 0; i < nb_groupes_rejoints; i++)
        if (groupes[i].actif)
            printf("%d. %s\n", i, groupes[i].nom);

    printf("Numéro: ");
    int choix;
    if (scanf("%d", &choix) != 1) {
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n');
    if (choix < 0 || choix >= nb_groupes_rejoints || !groupes[choix].actif) return;

    printf("\n[DIALOGUE] Groupe: %s\n", groupes[choix].nom);
    printf("(Tape 'quit' pour quitter)\n\n");

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

        int maxfd = (sockfd_client > STDIN_FILENO) ? sockfd_client : STDIN_FILENO;
        int activity = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0 && errno != EINTR) continue;

        /* Réception réseau (affiche TOUS les messages, y compris les tiens) */
        if (FD_ISSET(sockfd_client, &readfds)) {
            if (recevoir_message(sockfd_client, &msg, ip_src, &port_src) >= 0) {
                if (strcmp(msg.Ordre, ORDRE_MES) == 0) {
                    printf("[%s] %s: %s\n", get_avatar(msg.Emetteur),
                           msg.Emetteur, msg.Texte);
                    fflush(stdout);
                } else if (strcmp(msg.Ordre, ORDRE_INFO) == 0) {
                    printf("[INFO] %s\n", msg.Texte);
                    fflush(stdout);
                }
            }
        }

        /* Envoi utilisateur */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;
            nettoyer_chaine(buffer);
            if (strlen(buffer) == 0) continue;
            if (strcmp(buffer, "quit") == 0) break;

            // Envoi du message (affichage géré par réception)
            construire_message(&msg, ORDRE_MES, login, buffer);
            envoyer_message(sockfd_client, &msg, groupes[choix].ip, groupes[choix].port);
        }
    }

    printf("\n[FIN DIALOGUE] Retour au menu principal.\n");
}

/* ====================== MAIN ====================== */

int main(void) {
    printf("=== CLIENT ISY ===\n\n");

    // Lire la configuration
    lire_configuration();
    
    printf("📡 Connexion au serveur : %s\n\n", SERVER_IP);

    sockfd_client = creer_socket_udp();
    if (sockfd_client < 0) return EXIT_FAILURE;

    printf("Entrez votre nom: ");
    if (fgets(login, TAILLE_LOGIN, stdin) == NULL) return EXIT_FAILURE;
    nettoyer_chaine(login);
    printf("Bonjour %s !\n", login);

    signal(SIGINT, gestionnaire_signal);

    int choix;
    while (continuer) {
        afficher_menu();
        if (scanf("%d", &choix) != 1) {
            while (getchar() != '\n');
            printf("[ERREUR] Commande invalide.\n");
            continue;
        }
        while (getchar() != '\n');

        switch (choix) {
            case 0: creer_groupe(); break;
            case 1: rejoindre_groupe(); break;
            case 2: lister_groupes(); break;
            case 3: dialoguer(); break;
            case 4: fusionner_groupes(); break;
            case 5: continuer = 0; break;
            default: printf("[ERREUR] Choix invalide.\n"); break;
        }
    }

    close(sockfd_client);
    printf("[CLIENT] Terminé\n");
    return EXIT_SUCCESS;
}
