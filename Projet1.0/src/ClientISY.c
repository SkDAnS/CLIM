#include "../include/Commun.h"
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/wait.h>

/* Port de découverte automatique du serveur (doit être le même que côté ServeurISY) */
#define DISCOVERY_PORT 8005

typedef struct {
    char username[MAX_USERNAME];
    char server_ip[64];
    int  display_port;   /* port local pour AffichageISY */
} ClientConfig;

static ClientConfig cfg;
static int sock_cli = -1;              /* socket UDP principal */
static int shm_id   = -1;
static ClientDisplayShm *shm_cli = NULL;
static pid_t pid_affichage = -1;

/* =======================================================================
 *  Chargement de la configuration client
 * ======================================================================= */
static void load_config(const char *path)
{
    FILE *f = fopen(path, "r");
    check_fatal(!f, "fopen config client");

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[128];
        if (sscanf(line, "%63[^=]=%127s", key, val) == 2) {
            if (strcmp(key, "username") == 0) {
                strncpy(cfg.username, val, MAX_USERNAME - 1);
                cfg.username[MAX_USERNAME - 1] = '\0';
            } else if (strcmp(key, "server_ip") == 0) {
                strncpy(cfg.server_ip, val, sizeof(cfg.server_ip) - 1);
                cfg.server_ip[sizeof(cfg.server_ip) - 1] = '\0';
            } else if (strcmp(key, "display_port") == 0) {
                cfg.display_port = atoi(val);
            }
        }
    }
    fclose(f);

    if (cfg.username[0] == '\0') {
        fprintf(stderr, "Config client: username manquant\n");
        exit(EXIT_FAILURE);
    }
    if (cfg.server_ip[0] == '\0') {
        fprintf(stderr, "Config client: server_ip manquant (sera peut-être remplacé par l’auto-discovery)\n");
    }
    if (cfg.display_port <= 0) {
        fprintf(stderr, "Config client: display_port invalide\n");
        exit(EXIT_FAILURE);
    }
}

/* =======================================================================
 *  Découverte automatique du serveur via broadcast
 *  - Envoi:  "WHO_IS_SERVER?"
 *  - Réponse attendue: "SERVER_HERE:<ip>"
 *  Retourne 1 si trouvé, 0 sinon.
 * ======================================================================= */
static int discover_server_ip(char *out_ip, size_t out_sz)
{
    int sock = create_udp_socket();
    int opt = 1;

    /* Autoriser la réutilisation et le broadcast */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    struct sockaddr_in addr_bc;
    memset(&addr_bc, 0, sizeof(addr_bc));
    addr_bc.sin_family = AF_INET;
    addr_bc.sin_port   = htons(DISCOVERY_PORT);
    addr_bc.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const char *msg = "WHO_IS_SERVER?";
    ssize_t n = sendto(sock, msg, strlen(msg), 0,
                       (struct sockaddr *)&addr_bc, sizeof(addr_bc));
    if (n < 0) {
        perror("sendto broadcast");
        close(sock);
        return 0;
    }

    /* Timeout sur la réception */
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buffer[128];
    struct sockaddr_in addr_from;
    socklen_t len = sizeof(addr_from);

    n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                 (struct sockaddr *)&addr_from, &len);
    if (n <= 0) {
        /* aucun serveur n’a répondu */
        close(sock);
        return 0;
    }

    buffer[n] = '\0';

    if (strncmp(buffer, "SERVER_HERE:", 12) == 0) {
        strncpy(out_ip, buffer + 12, out_sz - 1);
        out_ip[out_sz - 1] = '\0';
        close(sock);
        return 1;
    }

    close(sock);
    return 0;
}

/* =======================================================================
 *  Gestion SHM & AffichageISY
 * ======================================================================= */
static void init_shm_client(void)
{
    shm_id = shmget(SHM_CLIENT_KEY, sizeof(ClientDisplayShm), IPC_CREAT | 0666);
    check_fatal(shm_id < 0, "shmget client");

    shm_cli = (ClientDisplayShm *)shmat(shm_id, NULL, 0);
    check_fatal(shm_cli == (void *)-1, "shmat client");

    shm_cli->running = 1;
}

static void detach_shm_client(void)
{
    if (shm_cli && shm_cli != (void *)-1) {
        shmdt(shm_cli);
        shm_cli = NULL;
    }
}

/* Lance AffichageISY dans un processus fils */
static pid_t start_affichage(void)
{
    if (!shm_cli)
        init_shm_client();

    pid_t pid = fork();
    check_fatal(pid < 0, "fork affichage");

    if (pid == 0) {
        /* Fils → exécuter AffichageISY */
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", cfg.display_port);

        execl("./bin/AffichageISY", "./bin/AffichageISY",
              port_str, cfg.username, (char *)NULL);
        perror("execl AffichageISY");
        _exit(EXIT_FAILURE);
    }

    return pid;
}

/* Demande l’arrêt d’AffichageISY via la SHM et attend le fils */
static void stop_affichage(void)
{
    if (shm_cli) {
        shm_cli->running = 0;
    }
    if (pid_affichage > 0) {
        int status;
        waitpid(pid_affichage, &status, 0);
        pid_affichage = -1;
    }
}

/* =======================================================================
 *  Envoi de commande au serveur (JOIN, CREATE, LIST, ...)
 *  - cmd : ex "JOIN isen"
 *  - reply_buf : buffer texte lisible ("OK ...", "ERR ...")
 *  - port_groupe_opt : si non NULL et que la réponse est "OK <port>",
 *                      renseigne le port du groupe.
 * ======================================================================= */
static void send_command_to_server(const char *cmd,
                                   char *reply_buf, size_t reply_sz,
                                   char *group_name_opt,
                                   int *port_groupe_opt)
{
    struct sockaddr_in addr_srv;
    fill_sockaddr(&addr_srv, cfg.server_ip, SERVER_PORT);

    ISYMessage msg;
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.ordre, ORDRE_CMD);
    strncpy(msg.emetteur, cfg.username, MAX_USERNAME - 1);
    msg.emetteur[MAX_USERNAME - 1] = '\0';
    strncpy(msg.texte, cmd, MAX_TEXT - 1);
    msg.texte[MAX_TEXT - 1] = '\0';

    ssize_t n = sendto(sock_cli, &msg, sizeof(msg), 0,
                       (struct sockaddr *)&addr_srv, sizeof(addr_srv));
    check_fatal(n < 0, "sendto serveur");

    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    ISYMessage reply;
    n = recvfrom(sock_cli, &reply, sizeof(reply), 0,
                 (struct sockaddr *)&from, &len);
    check_fatal(n < 0, "recvfrom serveur");

    /* Copie la réponse texte pour affichage */
    strncpy(reply_buf, reply.texte, reply_sz - 1);
    reply_buf[reply_sz - 1] = '\0';

    if (port_groupe_opt) {
        int port = -1;
        if (sscanf(reply.texte, "OK %d", &port) == 1)
            *port_groupe_opt = port;
        else
            *port_groupe_opt = -1;
    }

    if (group_name_opt) {
        strncpy(group_name_opt, reply.groupe, MAX_GROUP_NAME - 1);
        group_name_opt[MAX_GROUP_NAME - 1] = '\0';
    }
}

/* =======================================================================
 *  Envoi du CON au GroupeISY pour s’enregistrer comme membre
 * ======================================================================= */
static void connect_to_group(const char *group_name, int port_groupe)
{
    struct sockaddr_in addr_grp;
    /* Les processus GroupeISY tournent sur la même machine que le serveur */
    fill_sockaddr(&addr_grp, cfg.server_ip, port_groupe);

    ISYMessage msg;
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.ordre, ORDRE_CON);
    strncpy(msg.emetteur, cfg.username, MAX_USERNAME - 1);
    msg.emetteur[MAX_USERNAME - 1] = '\0';
    strncpy(msg.groupe, group_name, MAX_GROUP_NAME - 1);
    msg.groupe[MAX_GROUP_NAME - 1] = '\0';
    snprintf(msg.texte, sizeof(msg.texte), "%d", cfg.display_port);

    ssize_t n = sendto(sock_cli, &msg, sizeof(msg), 0,
                       (struct sockaddr *)&addr_grp, sizeof(addr_grp));
    check_fatal(n < 0, "sendto groupe CON");
}

/* =======================================================================
 *  Envoi d’un message MES au GroupeISY
 * ======================================================================= */
static void send_message_to_group(const char *group_name,
                                  int port_groupe,
                                  const char *texte)
{
    struct sockaddr_in addr_grp;
    fill_sockaddr(&addr_grp, cfg.server_ip, port_groupe);

    ISYMessage msg;
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.ordre, ORDRE_MSG);
    strncpy(msg.emetteur, cfg.username, MAX_USERNAME - 1);
    msg.emetteur[MAX_USERNAME - 1] = '\0';
    strncpy(msg.groupe, group_name, MAX_GROUP_NAME - 1);
    msg.groupe[MAX_GROUP_NAME - 1] = '\0';
    strncpy(msg.texte, texte, MAX_TEXT - 1);
    msg.texte[MAX_TEXT - 1] = '\0';

    ssize_t n = sendto(sock_cli, &msg, sizeof(msg), 0,
                       (struct sockaddr *)&addr_grp, sizeof(addr_grp));
    check_fatal(n < 0, "sendto groupe MES");
}

/* =======================================================================
 *  Programme principal
 * ======================================================================= */
int main(void)
{
    load_config("config/client_template.conf");

    /* Tentative de découverte automatique du serveur */
    char discovered_ip[64];
    if (discover_server_ip(discovered_ip, sizeof(discovered_ip))) {
        printf("[AUTO] Serveur détecté : %s\n", discovered_ip);
        strncpy(cfg.server_ip, discovered_ip, sizeof(cfg.server_ip) - 1);
        cfg.server_ip[sizeof(cfg.server_ip) - 1] = '\0';
    } else {
        printf("[AUTO] Aucun serveur détecté. Utilisation de l’IP du fichier conf : %s\n",
               cfg.server_ip);
    }

    printf("ClientISY – utilisateur=%s, serveur=%s, port_affichage=%d\n",
           cfg.username, cfg.server_ip, cfg.display_port);

    sock_cli = create_udp_socket();

    int running = 1;
    while (running) {
        printf("\n=== MENU CLIENT ISY ===\n");
        printf("1) Rejoindre un groupe\n");
        printf("2) Créer un groupe\n");
        printf("3) Liste des groupes\n");
        printf("0) Quitter\n");
        printf("Choix : ");

        char buffer[256];
        if (!fgets(buffer, sizeof(buffer), stdin))
            break;

        int choice = atoi(buffer);

        if (choice == 0) {
            running = 0;
        }
        else if (choice == 1) {
            char group_name[MAX_GROUP_NAME];
            printf("Nom du groupe : ");
            if (!fgets(group_name, sizeof(group_name), stdin))
                continue;
            group_name[strcspn(group_name, "\n")] = '\0';

            char cmd[128];
            snprintf(cmd, sizeof(cmd), "JOIN %s", group_name);

            char reply[256];
            int  port_groupe = -1;
            send_command_to_server(cmd, reply, sizeof(reply),
                                   NULL, &port_groupe);

            printf("Réponse serveur : %s\n", reply);

            if (port_groupe > 0) {
                if (pid_affichage <= 0) {
                    pid_affichage = start_affichage();
                }
                connect_to_group(group_name, port_groupe);

                /* Boucle de dialogue simple */
                printf("Entrez vos messages (\"quit\" pour revenir au menu) :\n");
                while (1) {
                    printf("> ");
                    if (!fgets(buffer, sizeof(buffer), stdin))
                        break;
                    buffer[strcspn(buffer, "\n")] = '\0';

                    if (strcmp(buffer, "quit") == 0)
                        break;

                    send_message_to_group(group_name, port_groupe, buffer);
                }
            }
        }
        else if (choice == 2) {
            char group_name[MAX_GROUP_NAME];
            printf("Nom du nouveau groupe : ");
            if (!fgets(group_name, sizeof(group_name), stdin))
                continue;
            group_name[strcspn(group_name, "\n")] = '\0';

            char cmd[128];
            snprintf(cmd, sizeof(cmd), "CREATE %s", group_name);

            char reply[256];
            int port_groupe = -1;
            send_command_to_server(cmd, reply, sizeof(reply),
                                   NULL, &port_groupe);
            printf("Réponse serveur : %s\n", reply);
        }
        else if (choice == 3) {
            char reply[512];
            send_command_to_server("LIST", reply, sizeof(reply),
                                   NULL, NULL);
            printf("Groupes disponibles :\n%s\n", reply);
        }
        else {
            printf("Choix invalide.\n");
        }
    }

    stop_affichage();
    detach_shm_client();
    if (sock_cli >= 0)
        close(sock_cli);

    return 0;
}
