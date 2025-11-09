/*
 * ServeurISY.c
 * Serveur principal gérant les groupes
 */

#include "../include/Commun.h"

static Groupe groupes[MAX_GROUPES];
static int nb_groupes = 0;
static int sockfd_serveur;
static int continuer = 1;

void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[SERVEUR] Terminaison...\n");
        continuer = 0;
    }
}

int creer_groupe(const char* nom_groupe, const char* moderateur) {
    if (trouver_groupe(groupes, nb_groupes, nom_groupe) >= 0) {
        return -1;
    }
    
    if (nb_groupes >= MAX_GROUPES) {
        return -1;
    }
    
    int idx = nb_groupes;
    strncpy(groupes[idx].nom, nom_groupe, TAILLE_NOM_GROUPE - 1);
    strncpy(groupes[idx].moderateur, moderateur, TAILLE_LOGIN - 1);
    groupes[idx].port = PORT_GROUPE_BASE + idx;
    groupes[idx].nb_membres = 0;
    groupes[idx].actif = 1;
    
    printf("[SERVEUR] %s: Creation groupe '%s'\n", moderateur, nom_groupe);
    
    /* Fork GroupeISY */
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("[SERVEUR] Erreur fork");
        return -1;
    }
    
    if (pid == 0) {
        /* Processus fils */
        char port_str[10];
        sprintf(port_str, "%d", groupes[idx].port);
        
        execl("./bin/GroupeISY", "GroupeISY", nom_groupe, moderateur, port_str, (char*)NULL);
        
        perror("[SERVEUR] Erreur execl");
        exit(EXIT_FAILURE);
    }
    
    groupes[idx].pid_processus = pid;
    nb_groupes++;
    
    printf("[SERVEUR] Groupe '%s' cree (PID: %d, Port: %d)\n", nom_groupe, pid, groupes[idx].port);
    
    return idx;
}

void envoyer_liste_groupes(const char* ip_client, int port_client) {
    struct struct_message msg;
    char liste[TAILLE_TEXTE] = "";
    int nb = 0;
    
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif) {
            if (nb > 0) strncat(liste, ",", TAILLE_TEXTE - strlen(liste) - 1);
            strncat(liste, groupes[i].nom, TAILLE_TEXTE - strlen(liste) - 1);
            nb++;
        }
    }
    
    if (nb == 0) strcpy(liste, "Aucun groupe");
    
    construire_message(&msg, ORDRE_INFO, "Serveur", liste);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
    
    printf("[SERVEUR] Liste envoyee (%d groupes)\n", nb);
}

void traiter_connexion_groupe(const char* nom_groupe, const char* login, const char* ip_client, int port_client) {
    int idx = trouver_groupe(groupes, nb_groupes, nom_groupe);
    struct struct_message msg;
    
    if (idx < 0) {
        construire_message(&msg, ORDRE_ERR, "Serveur", "Groupe introuvable");
        envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
        return;
    }
    
    /* Vérifier si banni */
    for (int i = 0; i < groupes[idx].nb_membres; i++) {
        if (strcmp(groupes[idx].membres[i].login, login) == 0 && groupes[idx].membres[i].banni) {
            construire_message(&msg, ORDRE_ERR, "Serveur", "Vous etes banni");
            envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
            return;
        }
    }
    
    char info[TAILLE_TEXTE];
    snprintf(info, TAILLE_TEXTE, "127.0.0.1:%d", groupes[idx].port);
    
    construire_message(&msg, ORDRE_OK, "Serveur", info);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
    
    printf("[SERVEUR] %s: Connexion au groupe '%s'\n", login, nom_groupe);
}

void fusionner_groupes(const char* groupe1, const char* groupe2, const char* demandeur) {
    int idx1 = trouver_groupe(groupes, nb_groupes, groupe1);
    int idx2 = trouver_groupe(groupes, nb_groupes, groupe2);
    
    if (idx1 < 0 || idx2 < 0) {
        printf("[SERVEUR] Groupes introuvables pour fusion\n");
        return;
    }
    
    /* Vérifier que le demandeur est modérateur des deux */
    if (strcmp(groupes[idx1].moderateur, demandeur) != 0 || 
        strcmp(groupes[idx2].moderateur, demandeur) != 0) {
        printf("[SERVEUR] %s n'est pas moderateur des deux groupes\n", demandeur);
        return;
    }
    
    /* Migrer les membres de groupe2 vers groupe1 */
    for (int i = 0; i < groupes[idx2].nb_membres; i++) {
        if (groupes[idx1].nb_membres < MAX_MEMBRES_PAR_GROUPE) {
            groupes[idx1].membres[groupes[idx1].nb_membres++] = groupes[idx2].membres[i];
        }
    }
    
    /* Supprimer groupe2 */
    if (groupes[idx2].pid_processus > 0) {
        kill(groupes[idx2].pid_processus, SIGTERM);
        waitpid(groupes[idx2].pid_processus, NULL, 0);
    }
    groupes[idx2].actif = 0;
    
    printf("[SERVEUR] Fusion: '%s' + '%s' -> '%s'\n", groupe1, groupe2, groupe1);
}

void boucle_serveur() {
    struct struct_message msg;
    char ip_client[TAILLE_IP];
    int port_client;
    
    printf("[SERVEUR] En attente...\n\n");
    
    while (continuer) {
        if (recevoir_message(sockfd_serveur, &msg, ip_client, &port_client) < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        
        if (strcmp(msg.Ordre, ORDRE_CRE) == 0) {
            int idx = creer_groupe(msg.Texte, msg.Emetteur);
            struct struct_message rep;
            if (idx >= 0) {
                construire_message(&rep, ORDRE_OK, "Serveur", "Groupe cree");
            } else {
                construire_message(&rep, ORDRE_ERR, "Serveur", "Erreur creation");
            }
            envoyer_message(sockfd_serveur, &rep, ip_client, port_client);
            
        } else if (strcmp(msg.Ordre, ORDRE_LST) == 0) {
            printf("[SERVEUR] %s: Liste groupes\n", msg.Emetteur);
            envoyer_liste_groupes(ip_client, port_client);
            
        } else if (strcmp(msg.Ordre, ORDRE_JOIN) == 0) {
            printf("[SERVEUR] %s: Join '%s'\n", msg.Emetteur, msg.Texte);
            traiter_connexion_groupe(msg.Texte, msg.Emetteur, ip_client, port_client);
            
        } else if (strcmp(msg.Ordre, ORDRE_FUS) == 0) {
            /* Format: groupe1:groupe2 */
            char g1[TAILLE_NOM_GROUPE], g2[TAILLE_NOM_GROUPE];
            if (sscanf(msg.Texte, "%[^:]:%s", g1, g2) == 2) {
                fusionner_groupes(g1, g2, msg.Emetteur);
            }
        }
    }
}

void terminer_tous_groupes() {
    printf("\n[SERVEUR] Terminaison groupes...\n");
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif && groupes[i].pid_processus > 0) {
            kill(groupes[i].pid_processus, SIGTERM);
            waitpid(groupes[i].pid_processus, NULL, 0);
        }
    }
}

int main() {
    printf("=== SERVEUR ISY ===\n\n");
    
    sockfd_serveur = creer_socket_udp();
    if (sockfd_serveur < 0 || bind_socket(sockfd_serveur, PORT_SERVEUR) < 0) {
        fprintf(stderr, "Erreur init serveur\n");
        return EXIT_FAILURE;
    }
    
    printf("[SERVEUR] Socket: 127.0.0.1:%d\n", PORT_SERVEUR);
    
    signal(SIGINT, gestionnaire_signal);
    
    for (int i = 0; i < MAX_GROUPES; i++) groupes[i].actif = 0;
    
    boucle_serveur();
    terminer_tous_groupes();
    close(sockfd_serveur);
    
    printf("[SERVEUR] Termine\n");
    return EXIT_SUCCESS;
}