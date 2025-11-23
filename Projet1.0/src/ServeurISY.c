#include "Commun.h"

static int sock_srv;
static GroupeInfo groupes[MAX_GROUPS];
static int running = 1;

/* Nettoyage sur CTRL-C */
void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

/* Cherche un groupe par nom, renvoie son index ou -1 */
static int find_group(const char *name)
{
    for (int i = 0; i < MAX_GROUPS; ++i) {
        if (groupes[i].actif && strcmp(groupes[i].nom, name) == 0)
            return i;
    }
    return -1;
}

/* Crée un GroupeISY (processus) */
static int create_group_process(int index)
{
    pid_t pid = fork();
    check_fatal(pid < 0, "fork GroupeISY");

    if (pid == 0) {
        /* Processus fils : exécuter GroupeISY */
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", groupes[index].port_groupe);

        execl("bin/GroupeISY", "bin/GroupeISY",
              groupes[index].nom,
              groupes[index].moderateur,
              port_str,
              (char *)NULL);

        perror("execl GroupeISY");
        _exit(EXIT_FAILURE);
    }

    /* Père : continue */
    return 0;
}

/* Traite une commande CMD reçue du client */
static void handle_command(ISYMessage *msg,
                           struct sockaddr_in *src, socklen_t src_len)
{
    ISYMessage reply;
    memset(&reply, 0, sizeof(reply));
    strcpy(reply.ordre, ORDRE_RPL);
    strncpy(reply.emetteur, "SERVER", MAX_USERNAME - 1);

    char cmd[16] = {0};
    char arg1[64] = {0};

    sscanf(msg->texte, "%15s %63s", cmd, arg1);

    if (strcmp(cmd, "LIST") == 0) {
        /* Liste des groupes actifs */
        strcpy(reply.groupe, "");
        char buffer[ MAX_TEXT ];
        buffer[0] = '\0';

        for (int i = 0; i < MAX_GROUPS; ++i) {
            if (groupes[i].actif) {
                char line[64];
                snprintf(line, sizeof(line), "%s (port %d)\n",
                         groupes[i].nom, groupes[i].port_groupe);
                if (strlen(buffer) + strlen(line) < sizeof(buffer))
                    strcat(buffer, line);
            }
        }
        if (buffer[0] == '\0')
            strcpy(buffer, "Aucun groupe\n");

        strncpy(reply.texte, buffer, MAX_TEXT - 1);
    }
    else if (strcmp(cmd, "CREATE") == 0) {
        if (arg1[0] == '\0') {
            strcpy(reply.texte, "Nom de groupe manquant");
        } else if (find_group(arg1) != -1) {
            strcpy(reply.texte, "Groupe deja existant");
        } else {
            int slot = -1;
            for (int i = 0; i < MAX_GROUPS; ++i) {
                if (!groupes[i].actif) { slot = i; break; }
            }
            if (slot == -1) {
                strcpy(reply.texte, "Plus de place pour de nouveaux groupes");
            } else {
                groupes[slot].actif = 1;
                strncpy(groupes[slot].nom, arg1, MAX_GROUP_NAME - 1);
                strncpy(groupes[slot].moderateur,
                        msg->emetteur, MAX_USERNAME - 1);
                groupes[slot].port_groupe = GROUP_PORT_BASE + slot;

                /* Optionnel : SHM stats */
                key_t key = SHM_GROUP_KEY_BASE + slot;
                int shm_id = shmget(key, sizeof(GroupStats),
                                    IPC_CREAT | 0666);
                check_fatal(shm_id < 0, "shmget group");
                groupes[slot].shm_key = key;
                groupes[slot].shm_id  = shm_id;

                create_group_process(slot);

                snprintf(reply.texte, MAX_TEXT,
                         "Groupe %s cree sur port %d",
                         groupes[slot].nom,
                         groupes[slot].port_groupe);
            }
        }
    }
    else if (strcmp(cmd, "JOIN") == 0) {
        int idx = find_group(arg1);
        if (idx < 0) {
            snprintf(reply.texte, MAX_TEXT,
                     "Groupe %s introuvable", arg1);
        } else {
            snprintf(reply.texte, MAX_TEXT,
                     "OK %d", groupes[idx].port_groupe);
            strncpy(reply.groupe, groupes[idx].nom, MAX_GROUP_NAME - 1);
        }
    }
    else if (strcmp(cmd, "DELETE") == 0) {
        int idx = find_group(arg1);
        if (idx < 0) {
            snprintf(reply.texte, MAX_TEXT,
                     "Groupe %s introuvable", arg1);
        } else {
            groupes[idx].actif = 0;
            snprintf(reply.texte, MAX_TEXT,
                     "Groupe %s supprime", arg1);
            /* Ici on pourrait signaler le processus GroupeISY
               (SIGTERM + nettoyage SHM) */
        }
    }
    else {
        snprintf(reply.texte, MAX_TEXT,
                 "Commande inconnue: %s", cmd);
    }

    sendto(sock_srv, &reply, sizeof(reply), 0,
           (struct sockaddr *)src, src_len);
}

int main(void)
{
    struct sockaddr_in addr_srv, addr_cli;
    socklen_t addrlen = sizeof(addr_cli);
    ISYMessage msg;

    memset(groupes, 0, sizeof(groupes));

    signal(SIGINT, handle_sigint);

    sock_srv = create_udp_socket();
    fill_sockaddr(&addr_srv, NULL, SERVER_PORT);
    check_fatal(bind(sock_srv, (struct sockaddr *)&addr_srv,
                     sizeof(addr_srv)) < 0, "bind serveur");

    printf("ServeurISY en écoute sur port %d\n", SERVER_PORT);

    while (running) {
        ssize_t n = recvfrom(sock_srv, &msg, sizeof(msg), 0,
                             (struct sockaddr *)&addr_cli, &addrlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        if (strncmp(msg.ordre, ORDRE_CMD, 3) == 0) {
            handle_command(&msg, &addr_cli, addrlen);
        } else {
            /* Messages inattendus au serveur */
            fprintf(stderr, "Ordre inconnu recu par serveur: %s\n",
                    msg.ordre);
        }
    }

    close(sock_srv);
    printf("ServeurISY termine\n");
    return 0;
}
