/*
 * ServeurISY.c
 * Version corrigée pour WSL avec redirection automatique Windows
 */

#include "../include/Commun.h"
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define BROADCAST_PORT 9999

static Groupe groupes[MAX_GROUPES];
static int nb_groupes = 0;
static int sockfd_serveur;
static int sockfd_broadcast;
static int continuer = 1;

// IP Windows détectée (pour la redirection des clients)
static char IP_WINDOWS[TAILLE_IP] = "127.0.0.1";
// IP WSL locale (pour l'écoute serveur)
static char IP_WSL[TAILLE_IP] = "0.0.0.0";

/* ========================== AUTO-DÉTECTION IP ========================== */
void recuperer_ip_windows_automatique() {
    printf("[INIT] Détection automatique de l'IP Windows...\n");

    // Commande PowerShell pour récupérer l'IP physique Windows
    const char* cmd = "powershell.exe -NoProfile -Command \"(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.InterfaceAlias -notlike '*WSL*' -and $_.InterfaceAlias -notlike '*Loopback*' -and $_.InterfaceAlias -notlike '*vEthernet*' -and $_.PrefixOrigin -ne 'WellKnown' } | Select-Object -ExpandProperty IPAddress | Select-Object -First 1)\"";

    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "[ERREUR] Impossible d'exécuter PowerShell depuis WSL.\n");
        return;
    }

    char buffer[64];
    if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        // Nettoyage des espaces et sauts de ligne
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r' || buffer[len - 1] == ' ')) {
            buffer[len - 1] = '\0';
            len--;
        }

        if (strlen(buffer) > 0 && strlen(buffer) < TAILLE_IP) {
            strncpy(IP_WINDOWS, buffer, TAILLE_IP - 1);
            IP_WINDOWS[TAILLE_IP - 1] = '\0';
            printf("[SUCCÈS] IP Windows détectée : %s\n", IP_WINDOWS);
        }
    }
    pclose(pipe);
}

void recuperer_ip_wsl_locale() {
    printf("[INIT] Détection de l'IP WSL locale...\n");
    
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char host[TAILLE_IP];
            struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, host, sizeof(host));
            
            // On cherche l'interface eth0 (typique WSL)
            if (strcmp(ifa->ifa_name, "eth0") == 0) {
                strncpy(IP_WSL, host, TAILLE_IP - 1);
                IP_WSL[TAILLE_IP - 1] = '\0';
                printf("[SUCCÈS] IP WSL détectée : %s (interface %s)\n", IP_WSL, ifa->ifa_name);
                break;
            }
        }
    }
    
    freeifaddrs(ifaddr);
}

void configurer_port_forwarding_windows() {
    printf("\n[CONFIG] Configuration de la redirection de port Windows...\n");
    
    // Commande pour ajouter la règle de port forwarding
    char cmd[512];
    
    // Suppression des règles existantes (au cas où)
    snprintf(cmd, sizeof(cmd), 
        "powershell.exe -NoProfile -Command \"Remove-NetFirewallRule -DisplayName 'WSL ISY Server' -ErrorAction SilentlyContinue; "
        "netsh interface portproxy delete v4tov4 listenport=%d listenaddress=0.0.0.0\"", 
        PORT_SERVEUR);
    system(cmd);
    
    // Ajout de la nouvelle règle de redirection
    snprintf(cmd, sizeof(cmd),
        "powershell.exe -NoProfile -Command \"netsh interface portproxy add v4tov4 listenport=%d listenaddress=0.0.0.0 connectport=%d connectaddress=%s; "
        "New-NetFirewallRule -DisplayName 'WSL ISY Server' -Direction Inbound -LocalPort %d -Protocol UDP -Action Allow\"",
        PORT_SERVEUR, PORT_SERVEUR, IP_WSL, PORT_SERVEUR);
    
    int result = system(cmd);
    if (result == 0) {
        printf("[SUCCÈS] Redirection 0.0.0.0:%d -> %s:%d configurée\n", 
               PORT_SERVEUR, IP_WSL, PORT_SERVEUR);
        printf("[INFO] Les clients peuvent se connecter à %s:%d\n", IP_WINDOWS, PORT_SERVEUR);
    } else {
        printf("[AVERTISSEMENT] Erreur lors de la configuration (droits admin requis?)\n");
    }
    
    // Configuration similaire pour les ports de groupes
    for (int i = 0; i < 10; i++) {
        int port = PORT_GROUPE_BASE + i;
        snprintf(cmd, sizeof(cmd),
            "powershell.exe -NoProfile -Command \"netsh interface portproxy add v4tov4 listenport=%d listenaddress=0.0.0.0 connectport=%d connectaddress=%s 2>$null\"",
            port, port, IP_WSL);
        system(cmd);
    }
    
    printf("[INFO] Redirection configurée pour les ports %d-%d\n", 
           PORT_GROUPE_BASE, PORT_GROUPE_BASE + 9);
}

/* ========================== SIGNAL HANDLER ========================== */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n[SERVEUR] Arrêt demandé.\n");
        continuer = 0;
    }
}

/* ========================== BROADCAST ========================== */
void* thread_broadcast(void* arg) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[256];

    printf("[BROADCAST] Écoute active sur le port %d\n", BROADCAST_PORT);

    while (continuer) {
        int bytes = recvfrom(sockfd_broadcast, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr*)&addr, &addrlen);
        if (bytes <= 0) continue;

        buffer[bytes] = '\0';

        if (strcmp(buffer, "SERVER_DISCOVERY") == 0) {
            char ip_client[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, ip_client, sizeof(ip_client));

            printf("[BROADCAST] Découverte reçue de %s\n", ip_client);

            char reponse[256];
            // On renvoie l'IP Windows (accessible depuis le réseau)
            snprintf(reponse, sizeof(reponse), "SERVER_HERE:%s", IP_WINDOWS);

            sendto(sockfd_broadcast, reponse, strlen(reponse), 0,
                   (struct sockaddr*)&addr, sizeof(addr));

            printf("[BROADCAST] Réponse envoyée : %s\n", IP_WINDOWS);
        }
    }

    close(sockfd_broadcast);
    return NULL;
}

/* ========================== GROUPES ========================== */
int creer_groupe(const char* nom_groupe, const char* moderateur) {
    if (trouver_groupe(groupes, nb_groupes, nom_groupe) >= 0) return -1;
    if (nb_groupes >= MAX_GROUPES) return -1;

    int idx = nb_groupes;
    strncpy(groupes[idx].nom, nom_groupe, TAILLE_NOM_GROUPE - 1);
    strncpy(groupes[idx].moderateur, moderateur, TAILLE_LOGIN - 1);
    groupes[idx].port = PORT_GROUPE_BASE + idx;
    groupes[idx].nb_membres = 0;
    groupes[idx].actif = 1;

    printf("[SERVEUR] Création du groupe '%s' (Port: %d)\n", nom_groupe, groupes[idx].port);

    pid_t pid = fork();
    if (pid < 0) return -1;

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

/* ========================== LISTE GROUPES ========================== */
void envoyer_liste_groupes(const char* ip_client, int port_client) {
    struct struct_message msg;
    char liste[TAILLE_TEXTE] = "";

    int nb = 0;
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif) {
            if (nb > 0) strncat(liste, ", ", sizeof(liste) - strlen(liste) - 1);
            strncat(liste, groupes[i].nom, sizeof(liste) - strlen(liste) - 1);
            nb++;
        }
    }
    if (nb == 0) strcpy(liste, "Aucun groupe");

    construire_message(&msg, ORDRE_INFO, "Serveur", liste);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
}

/* ========================== REJOINDRE GROUPE ========================== */
void traiter_connexion_groupe(const char* nom_groupe, const char* login,
                              const char* ip_client, int port_client) {
    struct struct_message msg;
    int idx = trouver_groupe(groupes, nb_groupes, nom_groupe);

    if (idx < 0 || !groupes[idx].actif) {
        construire_message(&msg, ORDRE_ERR, "Serveur", "Groupe introuvable");
        envoyer_message(sockfd_serveur, &msg, ip_client, port_client);
        return;
    }

    // Construction de la réponse avec l'IP WINDOWS (accessible depuis le réseau)
    char info[TAILLE_TEXTE];
    snprintf(info, sizeof(info), "%s:%d", IP_WINDOWS, groupes[idx].port);
    
    construire_message(&msg, ORDRE_OK, "Serveur", info);
    envoyer_message(sockfd_serveur, &msg, ip_client, port_client);

    printf("[SERVEUR] %s -> '%s' (Redirection vers %s:%d)\n", 
           login, nom_groupe, IP_WINDOWS, groupes[idx].port);
}

/* ========================== FUSION GROUPES ========================== */
void fusionner_groupes(const char* data, const char* demandeur) {
    char g1[TAILLE_NOM_GROUPE], g2[TAILLE_NOM_GROUPE], nouveau_nom[TAILLE_NOM_GROUPE];
    if (sscanf(data, "%[^:]:%[^:]:%s", g1, g2, nouveau_nom) != 3) return;

    int idx1 = trouver_groupe(groupes, nb_groupes, g1);
    int idx2 = trouver_groupe(groupes, nb_groupes, g2);

    if (idx1 < 0 || idx2 < 0) return;

    printf("[SERVEUR] Fusion : %s + %s -> %s\n", g1, g2, nouveau_nom);

    if (groupes[idx2].pid_processus > 0) {
        kill(groupes[idx2].pid_processus, SIGTERM);
        waitpid(groupes[idx2].pid_processus, NULL, 0);
        groupes[idx2].actif = 0;
    }
    creer_groupe(nouveau_nom, demandeur);
}

/* ========================== BOUCLE PRINCIPALE ========================== */
void boucle_serveur() {
    struct struct_message msg;
    char ip_client[TAILLE_IP];
    int port_client;

    printf("\n[SERVEUR] Prêt. En attente de clients sur 0.0.0.0:%d\n", PORT_SERVEUR);
    printf("[INFO] Les clients doivent se connecter à %s:%d\n", IP_WINDOWS, PORT_SERVEUR);

    while (continuer) {
        if (recevoir_message(sockfd_serveur, &msg, ip_client, &port_client) < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        if (strcmp(msg.Ordre, ORDRE_CRE) == 0) {
            int idx = creer_groupe(msg.Texte, msg.Emetteur);
            struct struct_message rep;
            if (idx >= 0)
                construire_message(&rep, ORDRE_OK, "Serveur", "Groupe créé");
            else
                construire_message(&rep, ORDRE_ERR, "Serveur", "Erreur création");
            envoyer_message(sockfd_serveur, &rep, ip_client, port_client);

        } else if (strcmp(msg.Ordre, ORDRE_LST) == 0) {
            envoyer_liste_groupes(ip_client, port_client);

        } else if (strcmp(msg.Ordre, ORDRE_JOIN) == 0) {
            traiter_connexion_groupe(msg.Texte, msg.Emetteur, ip_client, port_client);

        } else if (strcmp(msg.Ordre, ORDRE_FUS) == 0) {
            fusionner_groupes(msg.Texte, msg.Emetteur);
        }
    }
}

/* ========================== MAIN ========================== */
int main() {
    printf("=== SERVEUR ISY (Mode WSL avec Redirection Windows) ===\n");

    // 1. Détection des IPs
    recuperer_ip_windows_automatique();
    recuperer_ip_wsl_locale();

    if (strcmp(IP_WINDOWS, "127.0.0.1") == 0) {
        printf("⚠️  Attention : IP Windows non trouvée, utilisation de localhost.\n");
    }

    // 2. Configuration de la redirection Windows -> WSL
    printf("\n");
    configurer_port_forwarding_windows();

    // 3. Démarrage sockets (écoute sur toutes les interfaces)
    sockfd_serveur = creer_socket_udp();
    if (sockfd_serveur < 0) {
        fprintf(stderr, "Erreur création socket UDP\n");
        return EXIT_FAILURE;
    }

    // IMPORTANT : Bind sur 0.0.0.0 pour écouter sur TOUTES les interfaces
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0
    addr.sin_port = htons(PORT_SERVEUR);

    if (bind(sockfd_serveur, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[SERVEUR] Erreur bind");
        return EXIT_FAILURE;
    }

    printf("[SERVEUR] Écoute sur 0.0.0.0:%d (toutes interfaces)\n", PORT_SERVEUR);

    // 4. Socket broadcast
    sockfd_broadcast = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr_b;
    memset(&addr_b, 0, sizeof(addr_b));
    addr_b.sin_family = AF_INET;
    addr_b.sin_port = htons(BROADCAST_PORT);
    addr_b.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd_broadcast, (struct sockaddr*)&addr_b, sizeof(addr_b)) < 0) {
        perror("[BROADCAST] Erreur bind");
        return EXIT_FAILURE;
    }

    pthread_t tid_broadcast;
    pthread_create(&tid_broadcast, NULL, thread_broadcast, NULL);

    signal(SIGINT, gestionnaire_signal);

    // 5. Boucle principale
    boucle_serveur();

    // 6. Nettoyage
    printf("\n[SERVEUR] Arrêt des processus groupes...\n");
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif && groupes[i].pid_processus > 0) {
            kill(groupes[i].pid_processus, SIGTERM);
        }
    }

    close(sockfd_serveur);
    close(sockfd_broadcast);
    
    // Nettoyage des règles de redirection
    printf("[CLEANUP] Suppression des règles de redirection...\n");
    system("powershell.exe -NoProfile -Command \"netsh interface portproxy delete v4tov4 listenport=8000 listenaddress=0.0.0.0 2>$null\"");
    
    printf("[SERVEUR] Terminé.\n");
    return EXIT_SUCCESS;
}