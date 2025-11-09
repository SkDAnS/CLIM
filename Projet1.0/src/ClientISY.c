/*
 * ClientISY.c
 * Version finale : Avatars automatiques entre U+2600 et U+26FF (☀ → ⛿)
 * - Chaque utilisateur a un symbole UTF-8 unique
 * - Génération stable selon login/IP
 * - Dialogue fluide et fusion totale
 */

#include "../include/Commun.h"
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <locale.h>
#include <wchar.h>

/* ----- CONFIGURATION ----- */
/* Mettre à 1 pour activer la reconnaissance IP automatique */
#define ENABLE_IP_RECOG 1

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

/* ====================== UTILITAIRES ====================== */

void get_local_ip(char* buffer, size_t size) {
#if ENABLE_IP_RECOG
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        strncpy(buffer, "127.0.0.1", size - 1);
        buffer[size-1] = '\0';
        return;
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, "lo") != 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *) ifa->ifa_addr;
            if (inet_ntop(AF_INET, &sa->sin_addr, buffer, size) != NULL) {
                freeifaddrs(ifaddr);
                return;
            }
        }
    }
    freeifaddrs(ifaddr);
#endif
    strncpy(buffer, "127.0.0.1", size - 1);
    buffer[size-1] = '\0';
}

/* Petit hachage stable pour login/IP */
unsigned long simple_hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/* Avatar UTF-8 entre U+2600 et U+26FF */
const char* get_avatar(const char* login, const char* ip) {
    static char buffer[8];
    setlocale(LC_CTYPE, "");

    unsigned long base_unicode = 0x2600; // ☀
    unsigned long range = 0x26FF - 0x2600 + 1; // 256 symboles
    unsigned long hash_value;

#if ENABLE_IP_RECOG
    if (ip && strlen(ip) > 0)
        hash_value = simple_hash(ip);
    else
        hash_value = simple_hash(login);
#else
    hash_value = simple_hash(login);
#endif

    unsigned long codepoint = base_unicode + (hash_value % range);
    snprintf(buffer, sizeof(buffer), "%lc", (wint_t)codepoint);
    return buffer;
}

void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[CLIENT] CTRL-C...\n");
        continuer = 0;
    }
}

/* ====================== MENU ====================== */

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

/* ====================== ACTIONS ====================== */

void creer_groupe() {
    char nom[TAILLE_NOM_GROUPE];
    printf("\nNom du groupe: ");
    if (fgets(nom, TAILLE_NOM_GROUPE, stdin) == NULL) return;
    nettoyer_chaine(nom);
    if (strlen(nom) == 0) return;

    struct struct_message msg, rep;
    construire_message(&msg, ORDRE_CRE, login, nom);
    envoyer_message(sockfd_client, &msg, "127.0.0.1", PORT_SERVEUR);

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
    envoyer_message(sockfd_client, &msg, "127.0.0.1", PORT_SERVEUR);

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
    envoyer_message(sockfd_client, &msg, "127.0.0.1", PORT_SERVEUR);

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
        strncpy(groupes[idx].ip, ip_groupe, TAILLE_IP - 1);
        groupes[idx].port = port_groupe;
        groupes[idx].actif = 1;
        nb_groupes_rejoints++;
    }

    struct struct_message msg_con;
    construire_message(&msg_con, ORDRE_CON, login, "");
    envoyer_message(sockfd_client, &msg_con, groupes[idx].ip, groupes[idx].port);
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
        printf("[ERREUR] L’un des groupes sélectionnés est inactif.\n");
        return;
    }

    char nouveau_nom[TAILLE_NOM_GROUPE];
    snprintf(nouveau_nom, sizeof(nouveau_nom) - 1, "%s_%s", groupes[g1].nom, groupes[g2].nom);
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
    envoyer_message(sockfd_client, &msg, "127.0.0.1", PORT_SERVEUR);

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

    printf("Numero: ");
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
                    printf("[%s] %s: %s\n", get_avatar(msg.Emetteur, ip_src),
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

/* ====================== MAIN ====================== */

int main(void) {
    printf("=== CLIENT ISY ===\n");

    sockfd_client = creer_socket_udp();
    if (sockfd_client < 0) return EXIT_FAILURE;

    char ip_locale[TAILLE_IP];
    get_local_ip(ip_locale, sizeof(ip_locale));

#if ENABLE_IP_RECOG
    // 🔹 Mode reconnaissance IP activé
    FILE* f = fopen("utilisateurs_connus.txt", "r");
    int found = 0;

    if (f) {
        char line[128], ip_stored[TAILLE_IP], nom_stored[TAILLE_LOGIN];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "%15s -> %19s", ip_stored, nom_stored) == 2) {
                if (strcmp(ip_stored, ip_locale) == 0) {
                    strncpy(login, nom_stored, TAILLE_LOGIN - 1);
                    login[TAILLE_LOGIN - 1] = '\0';
                    found = 1;
                    break;
                }
            }
        }
        fclose(f);
    }

    if (found) {
        printf("Bienvenue de retour %s (IP: %s)\n", login, ip_locale);
    } else {
        printf("Nouvel utilisateur détecté (IP: %s)\n", ip_locale);
        printf("Entrez votre nom: ");
        if (fgets(login, TAILLE_LOGIN, stdin) == NULL) return EXIT_FAILURE;
        nettoyer_chaine(login);

        // 🔹 Sauvegarde dans le fichier
        FILE* fw = fopen("utilisateurs_connus.txt", "a");
        if (fw) {
            fprintf(fw, "%s -> %s\n", ip_locale, login);
            fclose(fw);
        }
        printf("Bienvenue %s ! Votre identité a été enregistrée.\n", login);
    }
#else
    // 🔹 Mode sans reconnaissance IP
    printf("Entrez votre nom: ");
    if (fgets(login, TAILLE_LOGIN, stdin) == NULL) return EXIT_FAILURE;
    nettoyer_chaine(login);
    printf("Bonjour %s ! (Reconnaissance IP désactivée)\n", login);
#endif

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
    printf("[CLIENT] Termine\n");
    return EXIT_SUCCESS;
}
