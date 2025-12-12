#include "../include/Commun.h"
#include <fcntl.h>

/* Liste des clients d'un groupe */
typedef struct {
    int actif;
    struct sockaddr_in addr_cli;  /* IP + port d'affichage côté client */
    char nom[MAX_USERNAME];
    char emoji[MAX_EMOJI];        /* emoji assigné au client */
} ClientInfo;

static ClientInfo clients[MAX_CLIENTS_GROUP];
static int sock_grp;
static int running = 1;
static GroupStats *stats = NULL;

void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

static void add_client(const char *name,
                       struct sockaddr_in *addr, int display_port,
                       const char *emoji)
{
    for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
        if (!clients[i].actif) {
            clients[i].actif = 1;
            snprintf(clients[i].nom, MAX_USERNAME, "%s", name);
            clients[i].addr_cli = *addr;
            clients[i].addr_cli.sin_port = htons(display_port);
            snprintf(clients[i].emoji, MAX_EMOJI, "%s", emoji);
            if (stats) stats->nb_clients++;
            printf("Client %s ajouté (port %d)\n",
                   name, display_port);
            return;
        }
    }
    printf("Plus de place pour de nouveaux clients dans ce groupe\n");
}

/* Add a client by explicit IP and port (used to transfer clients from another group) */
static void add_client_direct(const char *name, const char *ip, int display_port, const char *emoji)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    /* Avoid duplicates: check if client with same IP:port already exists */
    for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
        if (clients[i].actif) {
            char existing_ip[64];
            inet_ntop(AF_INET, &clients[i].addr_cli.sin_addr, existing_ip, sizeof(existing_ip));
            if (strcmp(existing_ip, ip) == 0 && ntohs(clients[i].addr_cli.sin_port) == display_port) {
                /* Update name/emoji if needed */
                snprintf(clients[i].nom, MAX_USERNAME, "%s", name);
                snprintf(clients[i].emoji, MAX_EMOJI, "%s", emoji);
                return;
            }
        }
    }
    add_client(name, &addr, display_port, emoji);
}

static void broadcast_message(ISYMessage *msg)
{
    for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
        if (clients[i].actif) {
            sendto(sock_grp, msg, sizeof(*msg), 0,
                   (struct sockaddr *)&clients[i].addr_cli,
                   sizeof(clients[i].addr_cli));
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <nom_groupe> <moderateur> <port>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *nom_groupe = argv[1];
    const char *moderateur = argv[2];
    int port = atoi(argv[3]);

    memset(clients, 0, sizeof(clients));

    /* Attache SHM pour statistiques (optionnel) */
    key_t key = SHM_GROUP_KEY_BASE + (port - GROUP_PORT_BASE);
    int shm_id = shmget(key, sizeof(GroupStats), 0666);
    if (shm_id >= 0) {
        stats = shmat(shm_id, NULL, 0);
        if (stats != (void *)-1) {
            stats->nb_clients = 0;
            stats->nb_messages = 0;
        } else {
            stats = NULL;
        }
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    sock_grp = create_udp_socket();
    /* Avoid child processes inheriting this socket */
    int flags_grp = fcntl(sock_grp, F_GETFD);
    if (flags_grp != -1) fcntl(sock_grp, F_SETFD, flags_grp | FD_CLOEXEC);
    struct sockaddr_in addr_grp, addr_src;
    socklen_t addrlen = sizeof(addr_src);

    fill_sockaddr(&addr_grp, NULL, port);
    check_fatal(bind(sock_grp, (struct sockaddr *)&addr_grp,
                     sizeof(addr_grp)) < 0, "bind groupe");
    printf("[GROUPE] Bind success on port %d\n", port);
    fflush(stdout);
    printf("GroupeISY '%s' lancé, moderateur=%s, port=%d\n",
           nom_groupe, moderateur, port);

    ISYMessage msg;

    while (running) {
        ssize_t n = recvfrom(sock_grp, &msg, sizeof(msg), 0, (struct sockaddr *)&addr_src, &addrlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom groupe");
            break;
        }

        /*//DEBUT DEBUG
        addrlen = sizeof(addr_src);               // <-- ajoute ça à chaque tour

        char ip_src[64];
        inet_ntop(AF_INET, &addr_src.sin_addr, ip_src, sizeof(ip_src));
        printf("[DEBUG GROUPE] paquet reçu ordre='%s' emetteur='%s' texte='%s' depuis %s:%d\n", msg.ordre, msg.emetteur, msg.texte, ip_src, ntohs(addr_src.sin_port));
        //FIN DEBUG*/

        if (strncmp(msg.ordre, ORDRE_CON, 3) == 0) {
            /* msg.texte contient le port d'affichage du client */
            int display_port = atoi(msg.texte);
            add_client(msg.emetteur, &addr_src, display_port, msg.emoji);
        }
        else if (strncmp(msg.ordre, ORDRE_MSG, 3) == 0) {
            if (stats) stats->nb_messages++;
            snprintf(msg.groupe, MAX_GROUP_NAME, "%s", nom_groupe);
            broadcast_message(&msg);
        }
        else if (strncmp(msg.ordre, ORDRE_MGR, 3) == 0) {
            /* ex: MGR text = "MIGRATE newname newport" */
            /* On convertit pour informer les clients et leur montrer où se connecter */
            ISYMessage notice;
            memset(&notice, 0, sizeof(notice));
            strcpy(notice.ordre, ORDRE_MSG);
            strncpy(notice.emetteur, "SERVER", MAX_USERNAME - 1);
            notice.emetteur[MAX_USERNAME - 1] = '\0';
            choose_emoji_from_username("SERVER", notice.emoji);
            strncpy(notice.groupe, nom_groupe, MAX_GROUP_NAME - 1);
            notice.groupe[MAX_GROUP_NAME - 1] = '\0';
            /* Parse MIGRATE <newname> <newport> */
            char newname[MAX_GROUP_NAME] = {0};
            int newport = -1;
            if (sscanf(msg.texte, "MIGRATE %31s %d", newname, &newport) == 2) {
                snprintf(notice.texte, sizeof(notice.texte), "Groupe fusionné → %s (port %d)", newname, newport);
                /* Also send a machine-parsable MIGRATE message so clients (Affichage) can auto-join */
                ISYMessage control;
                memset(&control, 0, sizeof(control));
                strcpy(control.ordre, ORDRE_MSG);
                strncpy(control.emetteur, "SERVER", MAX_USERNAME - 1);
                control.emetteur[MAX_USERNAME - 1] = '\0';
                snprintf(control.emoji, MAX_EMOJI, "%.*s", (int)(MAX_EMOJI - 1), notice.emoji);
                snprintf(control.texte, sizeof(control.texte), "MIGRATE %s %d", newname, newport);
                broadcast_message(&control);
            }
            else if (sscanf(msg.texte, "MIGRATEEXIST %31s %d", newname, &newport) == 2) {
                /* send ADDCLIENT for each local client entry to the target group's port */
                struct sockaddr_in addr_target;
                fill_sockaddr(&addr_target, "127.0.0.1", newport);
                for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
                    if (!clients[i].actif) continue;
                    char ipstr[64];
                    inet_ntop(AF_INET, &clients[i].addr_cli.sin_addr, ipstr, sizeof(ipstr));
                    ISYMessage addmsg;
                    memset(&addmsg, 0, sizeof(addmsg));
                    strcpy(addmsg.ordre, ORDRE_MGR);
                    strncpy(addmsg.emetteur, nom_groupe, MAX_USERNAME - 1);
                    addmsg.emetteur[MAX_USERNAME - 1] = '\0';
                    snprintf(addmsg.emoji, MAX_EMOJI, "%.*s", (int)(MAX_EMOJI - 1), clients[i].emoji);
                    snprintf(addmsg.texte, sizeof(addmsg.texte), "ADDCLIENT %s %s %d", clients[i].nom, ipstr, ntohs(clients[i].addr_cli.sin_port));
                    ssize_t r = sendto(sock_grp, &addmsg, sizeof(addmsg), 0, (struct sockaddr *)&addr_target, sizeof(addr_target));
                    if (r < 0) perror("sendto ADDCLIENT");
                }
                /* Also notify local clients */
                /* build notice text safely */
                {
                    const char prefix[] = "Groupe fusionné → ";
                    size_t avail = sizeof(notice.texte) - 1;
                    strncpy(notice.texte, prefix, avail);
                    notice.texte[avail] = '\0';
                    size_t used = strlen(notice.texte);
                    if (used < avail) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%s (port %d)", newname, newport);
                        strncat(notice.texte, buf, avail - used);
                    }
                }
                /* also broadcast the control MIGRATE message for clients to auto-join */
                ISYMessage control;
                memset(&control, 0, sizeof(control));
                strcpy(control.ordre, ORDRE_MSG);
                strncpy(control.emetteur, "SERVER", MAX_USERNAME - 1);
                control.emetteur[MAX_USERNAME - 1] = '\0';
                strncpy(control.emoji, notice.emoji, MAX_EMOJI - 1);
                control.emoji[MAX_EMOJI - 1] = '\0';
                snprintf(control.texte, sizeof(control.texte), "MIGRATE %s %d", newname, newport);
                broadcast_message(&control);
            }
            else if (strncmp(msg.texte, "ADDCLIENT", 9) == 0) {
                /* ADDCLIENT name ip port */
                char name[MAX_USERNAME] = {0};
                char ipstr[64] = {0};
                int port = 0;
                if (sscanf(msg.texte, "ADDCLIENT %19s %63s %d", name, ipstr, &port) >= 3) {
                    add_client_direct(name, ipstr, port, msg.emoji);
                }
                /* Do not broadcast add notice to local clients, continue to next message */
                continue;
            } else {
                /* build notice text safely */
                {
                    const char prefix[] = "Groupe fusionné → ";
                    size_t avail = sizeof(notice.texte) - 1;
                    strncpy(notice.texte, prefix, avail);
                    notice.texte[avail] = '\0';
                    size_t used = strlen(notice.texte);
                    if (used < avail) {
                        /* Append at most remaining space using snprintf to avoid warnings */
                        snprintf(notice.texte + used, avail - used + 1, "%.*s", (int)(avail - used), msg.texte);
                    }
                }
            }
            broadcast_message(&notice);
        }
    }

    close(sock_grp);
    if (stats && stats != (void *)-1)
        shmdt(stats);

    printf("GroupeISY '%s' termine\n", nom_groupe);
    return 0;
}
