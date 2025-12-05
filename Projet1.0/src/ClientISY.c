#include "../include/Commun.h"
#include "../include/notif.h"
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>


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
static char selected_sound[256] = "notif.wav";  /* son par défaut */

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
                snprintf(cfg.username, MAX_USERNAME, "%s", val);
            } else if (strcmp(key, "server_ip") == 0) {
                snprintf(cfg.server_ip, sizeof(cfg.server_ip), "%s", val);
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
/* Discovery (broadcast) removed. The server IP must be provided in the config file.
 * This project explicitly disables broadcast-based discovery per request.
 */

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
    shm_cli->notify_flag = 0;
    shm_cli->notify[0] = '\0';
    snprintf(shm_cli->sound_name, sizeof(shm_cli->sound_name), "%s", selected_sound);
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
    snprintf(msg.emetteur, MAX_USERNAME, "%s", cfg.username);
    /* Assign a deterministic emoji for this user */
    choose_emoji_from_username(cfg.username, msg.emoji);
    msg.emetteur[MAX_USERNAME - 1] = '\0';
    snprintf(msg.texte, MAX_TEXT, "%s", cmd);

    printf("[CLIENT] Sending to server %s: %s\n", cfg.server_ip, cmd);
    fflush(stdout);
    ssize_t n = sendto(sock_cli, &msg, sizeof(msg), 0,
                       (struct sockaddr *)&addr_srv, sizeof(addr_srv));
    check_fatal(n < 0, "sendto serveur");

    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    ISYMessage reply;
    /* Add a timeout to avoid blocking indefinitely when server is not reachable */
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock_cli, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    n = recvfrom(sock_cli, &reply, sizeof(reply), 0,
                 (struct sockaddr *)&from, &len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            snprintf(reply_buf, reply_sz, "Aucun reponse du serveur (timeout)");
            if (port_groupe_opt) *port_groupe_opt = -1;
            if (group_name_opt) snprintf(group_name_opt, MAX_GROUP_NAME, "");
            return;
        }
        check_fatal(n < 0, "recvfrom serveur");
    }
    printf("[CLIENT] Received reply: %s\n", reply.texte);
    fflush(stdout);

    /* Copie la réponse texte pour affichage */
    snprintf(reply_buf, reply_sz, "%s", reply.texte);

    if (port_groupe_opt) {
        int port = -1;
        if (sscanf(reply.texte, "OK %d", &port) == 1)
            *port_groupe_opt = port;
        else
            *port_groupe_opt = -1;
    }

    if (group_name_opt) {
        snprintf(group_name_opt, MAX_GROUP_NAME, "%s", reply.groupe);
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
    snprintf(msg.emetteur, MAX_USERNAME, "%s", cfg.username);
    choose_emoji_from_username(cfg.username, msg.emoji);
    snprintf(msg.groupe, MAX_GROUP_NAME, "%s", group_name);
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
    snprintf(msg.emetteur, MAX_USERNAME, "%s", cfg.username);
    choose_emoji_from_username(cfg.username, msg.emoji);
    snprintf(msg.groupe, MAX_GROUP_NAME, "%s", group_name);
    snprintf(msg.texte, MAX_TEXT, "%s", texte);

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

    /* No broadcast discovery in this project; server_ip must be in config */
    printf("Serveur utilisé (config): %s\n", cfg.server_ip);

    printf("ClientISY – utilisateur=%s, serveur=%s, port_affichage=%d\n",
           cfg.username, cfg.server_ip, cfg.display_port);

    sock_cli = create_udp_socket();
    /* avoid child processes inheriting client socket */
    int flags_cli = fcntl(sock_cli, F_GETFD);
    if (flags_cli != -1) fcntl(sock_cli, F_SETFD, flags_cli | FD_CLOEXEC);

    int running = 1;
    while (running) {
        /* Check for migration notifications from Affichage via SHM */
        if (shm_cli && shm_cli->notify_flag) {
            char notif[MAX_TEXT];
            snprintf(notif, sizeof(notif), "%s", shm_cli->notify);
            shm_cli->notify_flag = 0;
            shm_cli->notify[0] = '\0';
            char newname[MAX_GROUP_NAME]; int newport;
            if (sscanf(notif, "MIGRATE %31s %d", newname, &newport) == 2) {
                printf("[AUTOJOIN] Migration notice: %s -> %d\n", newname, newport);
                fflush(stdout);
                /* try to JOIN via server to get current port, prefer server reply */
                char joincmd[128];
                snprintf(joincmd, sizeof(joincmd), "JOIN %s", newname);
                char reply[256]; int port_g = -1;
                send_command_to_server(joincmd, reply, sizeof(reply), NULL, &port_g);
                if (port_g > 0) {
                    if (pid_affichage <= 0) pid_affichage = start_affichage();
                    connect_to_group(newname, port_g);
                    printf("[AUTOJOIN] Rejoint le groupe %s (port %d) via server reply\n", newname, port_g);
                } else if (newport > 0) {
                    if (pid_affichage <= 0) pid_affichage = start_affichage();
                    connect_to_group(newname, newport);
                    printf("[AUTOJOIN] Rejoint le groupe %s (port %d) via MIGRATE port\n", newname, newport);
                }
            }
        }
        printf("\n=== MENU CLIENT ISY ===\n");
        printf("1) Rejoindre un groupe\n");
        printf("2) Créer un groupe\n");
        printf("3) Liste des groupes\n");
        printf("4) Fusionner deux groupes\n");
        printf("5) Choisir le son de notification\n");
        printf("0) Quitter\n");
        printf("Choix : ");

        char buffer[256];
        if (!fgets(buffer, sizeof(buffer), stdin))
            break;

        int choice = atoi(buffer);

        if (choice == 0) {
            running = 0;
            exit(0);
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
        else if (choice == 4) {
            char g1[MAX_GROUP_NAME];
            char g2[MAX_GROUP_NAME];
            char newname[MAX_GROUP_NAME];
            printf("Nom du premier groupe : ");
            if (!fgets(g1, sizeof(g1), stdin)) continue;
            g1[strcspn(g1, "\n")] = '\0';
            printf("Nom du second groupe : ");
            if (!fgets(g2, sizeof(g2), stdin)) continue;
            g2[strcspn(g2, "\n")] = '\0';
            printf("Nom du nouveau groupe : ");
            if (!fgets(newname, sizeof(newname), stdin)) continue;
            newname[strcspn(newname, "\n")] = '\0';

            /* Stop AffichageISY before merging so we don't receive messages from old groups */
            stop_affichage();

            char cmd[256];
            snprintf(cmd, sizeof(cmd), "MERGE %s %s %s", g1, g2, newname);
            char reply[256];
            send_command_to_server(cmd, reply, sizeof(reply), NULL, NULL);
            printf("Réponse serveur : %s\n", reply);
        }
        else if (choice == 5) {
            /* Menu pour choisir le son de notification */
            char nomsSons[MAX_SONS][MAX_NOM];
            int nbSons = listerSons(nomsSons);
            
            if (nbSons == 0) {
                printf("Aucun fichier .wav trouvé dans le dossier 'sons'.\n");
            } else {
                printf("\n=== SONS DISPONIBLES ===\n");
                for (int i = 0; i < nbSons; i++) {
                    printf("%d) %s\n", i + 1, nomsSons[i]);
                }
                printf("Choix du son (1-%d) : ", nbSons);
                
                if (fgets(buffer, sizeof(buffer), stdin)) {
                    int choice_son = atoi(buffer);
                    if (choice_son >= 1 && choice_son <= nbSons) {
                        snprintf(selected_sound, sizeof(selected_sound), "%s", nomsSons[choice_son - 1]);
                        /* Mettre à jour le SHM pour que AffichageISY utilise ce son */
                        if (shm_cli) {
                            snprintf(shm_cli->sound_name, sizeof(shm_cli->sound_name), "%s", selected_sound);
                        }
                        printf("Son sélectionné : %s\n", selected_sound);
                    } else {
                        printf("Choix invalide.\n");
                    }
                }
            }
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

