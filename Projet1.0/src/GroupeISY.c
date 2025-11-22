#include "Commun.h"

/* Liste des clients d’un groupe */
typedef struct {
    int actif;
    struct sockaddr_in addr_cli;  /* IP + port d’affichage côté client */
    char nom[MAX_USERNAME];
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
                       struct sockaddr_in *addr, int display_port)
{
    for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
        if (!clients[i].actif) {
            clients[i].actif = 1;
            strncpy(clients[i].nom, name, MAX_USERNAME - 1);
            clients[i].addr_cli = *addr;
            clients[i].addr_cli.sin_port = htons(display_port);
            if (stats) stats->nb_clients++;
            printf("Client %s ajouté (port %d)\n",
                   name, display_port);
            return;
        }
    }
    printf("Plus de place pour de nouveaux clients dans ce groupe\n");
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

    sock_grp = create_udp_socket();
    struct sockaddr_in addr_grp, addr_src;
    socklen_t addrlen = sizeof(addr_src);

    fill_sockaddr(&addr_grp, NULL, port);
    check_fatal(bind(sock_grp, (struct sockaddr *)&addr_grp,
                     sizeof(addr_grp)) < 0, "bind groupe");

    printf("GroupeISY '%s' lancé, moderateur=%s, port=%d\n",
           nom_groupe, moderateur, port);

    ISYMessage msg;

    while (running) {
        ssize_t n = recvfrom(sock_grp, &msg, sizeof(msg), 0,
                             (struct sockaddr *)&addr_src, &addrlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom groupe");
            break;
        }

        if (strncmp(msg.ordre, ORDRE_CON, 3) == 0) {
            /* msg.texte contient le port d’affichage du client */
            int display_port = atoi(msg.texte);
            add_client(msg.emetteur, &addr_src, display_port);
        }
        else if (strncmp(msg.ordre, ORDRE_MSG, 3) == 0) {
            if (stats) stats->nb_messages++;
            strncpy(msg.groupe, nom_groupe, MAX_GROUP_NAME - 1);
            broadcast_message(&msg);
        }
    }

    close(sock_grp);
    if (stats && stats != (void *)-1)
        shmdt(stats);

    printf("GroupeISY '%s' termine\n", nom_groupe);
    return 0;
}
