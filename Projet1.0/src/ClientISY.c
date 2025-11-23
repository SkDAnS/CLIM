#include "Commun.h"

typedef struct {
    char username[MAX_USERNAME];
    char server_ip[64];
    int  display_port;   /* port local pour AffichageISY */
} ClientConfig;

static ClientConfig cfg;
static int sock_cli;
static struct sockaddr_in addr_srv;
static ClientDisplayShm *shm = NULL;






//Port de dévouverte pour la recherche du serveur
#define DISCOVERY_PORT 8005

//Fonction pour la découverte du serveur
static int discover_server_ip(char *out_ip)
{
    int sock = create_udp_socket();

    /* Autoriser broadcast */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    /* Adresse broadcast */
    struct sockaddr_in addr_bc;
    fill_sockaddr(&addr_bc, "255.255.255.255", DISCOVERY_PORT);

    const char *msg = "WHO_IS_SERVER?";
    sendto(sock, msg, strlen(msg), 0,
           (struct sockaddr*)&addr_bc, sizeof(addr_bc));

    /* Attend une réponse */
    struct sockaddr_in addr_from;
    socklen_t len = sizeof(addr_from);
    char buffer[128];

    struct timeval tv = {1, 0}; /* timeout 1s */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = recvfrom(sock, buffer, sizeof(buffer)-1, 0,
                         (struct sockaddr*)&addr_from, &len);

    close(sock);

    if (n <= 0) {
        return 0; /* pas trouvé */
    }

    buffer[n] = '\0';

    if (strncmp(buffer, "SERVER_HERE:", 12) == 0) {
        strcpy(out_ip, buffer + 12);
        return 1;
    }

    return 0;
}





/* Lecture simple d’un fichier de config :
 * username=...
 * server_ip=...
 * display_port=... */
static void load_config(const char *path)
{
    FILE *f = fopen(path, "r");
    check_fatal(!f, "fopen client conf");

    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (strcmp(key, "username") == 0)
                strncpy(cfg.username, val, MAX_USERNAME - 1);
            else if (strcmp(key, "server_ip") == 0)
                strncpy(cfg.server_ip, val, sizeof(cfg.server_ip) - 1);
            else if (strcmp(key, "display_port") == 0)
                cfg.display_port = atoi(val);
        }
    }
    fclose(f);
}

/* Envoie une commande simple au serveur et reçoit la réponse */
static void send_command_to_server(const char *cmd, char *buffer,
                                   size_t bufsize,
                                   char *groupe_name_opt,
                                   int *port_groupe_opt)
{
    ISYMessage msg, reply;
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.ordre, ORDRE_CMD);
    strncpy(msg.emetteur, cfg.username, MAX_USERNAME - 1);
    strncpy(msg.texte, cmd, MAX_TEXT - 1);

    sendto(sock_cli, &msg, sizeof(msg), 0,
           (struct sockaddr *)&addr_srv, sizeof(addr_srv));

    struct sockaddr_in addr_from;
    socklen_t fromlen = sizeof(addr_from);
    ssize_t n = recvfrom(sock_cli, &reply, sizeof(reply), 0,
                         (struct sockaddr *)&addr_from, &fromlen);
    check_fatal(n < 0, "recvfrom serveur");

    if (buffer && bufsize > 0) {
        strncpy(buffer, reply.texte, bufsize - 1);
        buffer[bufsize - 1] = '\0';
    }

    if (groupe_name_opt)
        strncpy(groupe_name_opt, reply.groupe,
                MAX_GROUP_NAME - 1);

    if (port_groupe_opt) {
        int port = -1;
        if (sscanf(reply.texte, "OK %d", &port) == 1)
            *port_groupe_opt = port;
        else
            *port_groupe_opt = -1;
    }
}

/* Lance AffichageISY (fork + execl) */
static pid_t start_affichage(void)
{
    pid_t pid = fork();
    check_fatal(pid < 0, "fork affichage");

    if (pid == 0) {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", cfg.display_port);
        execl("bin/AffichageISY", "bin/AffichageISY",
              port_str, cfg.username, (char *)NULL);
        perror("execl AffichageISY");
        _exit(EXIT_FAILURE);
    }
    return pid;
}

/* Connexion au groupe (protocole : message CON au GroupeISY)
 * text = display_port */
static void connect_to_group(const char *group_name, int port_groupe)
{
    struct sockaddr_in addr_grp;
    //fill_sockaddr(&addr_grp, cfg.server_ip, port_groupe);
    fill_sockaddr(&addr_grp,"192.168.0.17", port_groupe);


    ISYMessage msg;
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.ordre, ORDRE_CON);
    strncpy(msg.emetteur, cfg.username, MAX_USERNAME - 1);
    strncpy(msg.groupe, group_name, MAX_GROUP_NAME - 1);
    snprintf(msg.texte, sizeof(msg.texte), "%d", cfg.display_port);

    /*//DEBUT DEBUG
    char ip_grp[64];
    inet_ntop(AF_INET, &addr_grp.sin_addr, ip_grp, sizeof(ip_grp));
    printf("[DEBUG CLIENT] Envoi CON vers %s:%d (groupe=%s, user=%s, display_port=%d)\n", ip_grp, ntohs(addr_grp.sin_port), group_name, cfg.username, cfg.display_port);
    //FIN DEBUG*/

    sendto(sock_cli, &msg, sizeof(msg), 0, (struct sockaddr *)&addr_grp, sizeof(addr_grp));
}

/* Envoi d’un message MES au groupe */
static void send_message_to_group(const char *group_name,
                                  int port_groupe)
{
    struct sockaddr_in addr_grp;
    fill_sockaddr(&addr_grp, cfg.server_ip, port_groupe);

    char line[256];
    while (1) {
        printf("Message (quit pour revenir au menu) : ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            break;

        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "quit") == 0)
            break;

        ISYMessage msg;
        memset(&msg, 0, sizeof(msg));
        strcpy(msg.ordre, ORDRE_MSG);
        strncpy(msg.emetteur, cfg.username, MAX_USERNAME - 1);
        strncpy(msg.groupe, group_name, MAX_GROUP_NAME - 1);
        strncpy(msg.texte, line, MAX_TEXT - 1);

        sendto(sock_cli, &msg, sizeof(msg), 0,
               (struct sockaddr *)&addr_grp, sizeof(addr_grp));
    }
}

int main(void)
{
    load_config("config/client_template.conf");

    /* === Tentative de découverte automatique du serveur === */
    char discovered_ip[64];
    if (discover_server_ip(discovered_ip)) {
        printf("[AUTO] Serveur détecté : %s\n", discovered_ip);
        strcpy(cfg.server_ip, discovered_ip);
    } else {
        printf("[AUTO] Aucun serveur détecté. Utilisation de l’IP du fichier conf : %s\n",
            cfg.server_ip);
    }
    
    printf("ClientISY – utilisateur %s, serveur=%s, port_affichage=%d\n",
        cfg.username, cfg.server_ip, cfg.display_port);
    /* === FIN Tentative de découverte automatique du serveur === */

    /* SHM client-affichage */
    int shm_id = shmget(SHM_CLIENT_KEY, sizeof(ClientDisplayShm),
                        IPC_CREAT | 0666);
    check_fatal(shm_id < 0, "shmget client");
    shm = (ClientDisplayShm *)shmat(shm_id, NULL, 0);
    check_fatal(shm == (void *)-1, "shmat client");
    shm->running = 1;

    /* Socket UDP client */
    sock_cli = create_udp_socket();
    fill_sockaddr(&addr_srv, cfg.server_ip, SERVER_PORT);

    int choice;
    char buffer[256];
    char group_name[MAX_GROUP_NAME];
    int port_groupe = -1;
    pid_t pid_affichage = -1;

    while (1) {
        printf("\nChoix des commandes :\n");
        printf("0 - Creation de groupe\n");
        printf("1 - Rejoindre un groupe\n");
        printf("2 - Lister les groupes\n");
        printf("3 - Dialoguer sur un groupe\n");
        printf("4 - Quitter\n");
        printf("Choix : ");
        fflush(stdout);

        if (scanf("%d", &choice) != 1) {
            printf("Entrée invalide\n");
            break;
        }
        /* consommer le \n restant */
        fgets(buffer, sizeof(buffer), stdin);

        if (choice == 0) {
            printf("Nom du groupe : ");
            fgets(group_name, sizeof(group_name), stdin);
            group_name[strcspn(group_name, "\n")] = '\0';

            char cmd[128];
            snprintf(cmd, sizeof(cmd),
                     "CREATE %s", group_name);
            send_command_to_server(cmd, buffer,
                                   sizeof(buffer), NULL, NULL);
            printf("Reponse serveur : %s\n", buffer);
        }
        else if (choice == 1) {
            printf("Nom du groupe : ");
            fgets(group_name, sizeof(group_name), stdin);
            group_name[strcspn(group_name, "\n")] = '\0';

            char cmd[128];
            snprintf(cmd, sizeof(cmd),
                     "JOIN %s", group_name);
            send_command_to_server(cmd, buffer,
                                   sizeof(buffer),
                                   group_name, &port_groupe);
            printf("Reponse serveur : %s\n", buffer);

            if (port_groupe > 0) {
                /* Lancer affichage si pas déjà lancé */
                if (pid_affichage <= 0) {
                    pid_affichage = start_affichage();
                }
                connect_to_group(group_name, port_groupe);
            }
        }
        else if (choice == 2) {
            send_command_to_server("LIST", buffer,
                                   sizeof(buffer), NULL, NULL);
            printf("Groupes:\n%s\n", buffer);
        }
        else if (choice == 3) {
            if (port_groupe <= 0) {
                printf("Vous n'êtes connecté à aucun groupe\n");
            } else {
                send_message_to_group(group_name, port_groupe);
            }
        }
        else if (choice == 4) {
            printf("Demande de deconnexion\n");
            break;
        }
        else {
            printf("Choix inconnu\n");
        }
    }

    shm->running = 0; /* Demande arrêt affichage */
    if (pid_affichage > 0) {
        waitpid(pid_affichage, NULL, 0);
    }

    close(sock_cli);
    shmdt(shm);

    printf("ClientISY termine\n");
    return 0;
}
