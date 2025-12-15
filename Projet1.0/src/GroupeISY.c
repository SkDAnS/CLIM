#include "../include/Commun.h"
#include <strings.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Helper function to ensure infoGroup directory exists */
static void ensure_infogroup_dir(void)
{
    mkdir("infoGroup", 0755);  /* Create if doesn't exist, ignore if already exists */
}

/* Helper function to build the path to the group info file */
static void build_group_file_path(const char *group_name, char *path, size_t path_size)
{
    snprintf(path, path_size, "infoGroup/%s.txt", group_name);
}

/* Helper function to check if a user is already in the group file */
static int is_user_already_in_file(const char *group_name, const char *username)
{
    ensure_infogroup_dir();
    char filepath[256];
    build_group_file_path(group_name, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;  /* File doesn't exist, so user is not in it */
    
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char stored_user[MAX_USERNAME];
        if (sscanf(line, "%19s:", stored_user) == 1) {
            if (strcmp(stored_user, username) == 0) {
                found = 1;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

/* Add user info to the group file (name:ip:emoji) */
static void add_user_to_group_file(const char *group_name, const char *username, 
                                   const char *ip, const char *emoji)
{
    ensure_infogroup_dir();
    char filepath[256];
    build_group_file_path(group_name, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "a");
    if (f) {
        fprintf(f, "%s:%s:%s\n", username, ip, emoji);
        fclose(f);
    }
}

/* Build path to the banned IPs file for a group */
static void build_banned_file_path(const char *group_name, char *path, size_t path_size)
{
    snprintf(path, path_size, "infoGroup/%s_banned.txt", group_name);
}

/* Check if an IP is banned from a group */
static int is_ip_banned(const char *group_name, const char *ip)
{
    char filepath[256];
    build_banned_file_path(group_name, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;  /* No ban file means no bans */
    
    char line[64];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Remove newline if present */
        char *p = strchr(line, '\n');
        if (p) *p = '\0';
        if (strcmp(line, ip) == 0) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* Add an IP to the banned list of a group */
static void ban_ip_from_group(const char *group_name, const char *ip)
{
    ensure_infogroup_dir();
    char filepath[256];
    build_banned_file_path(group_name, filepath, sizeof(filepath));
    
    FILE *f = fopen(filepath, "a");
    if (f) {
        fprintf(f, "%s\n", ip);
        fclose(f);
    }
}

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
/* current group name stored for use in helper functions */
static char g_group_name[MAX_GROUP_NAME];

/* Rebuild the group file from current active clients */
static void rebuild_group_file(const char *group_name)
{
    ensure_infogroup_dir();
    char filepath[256];
    build_group_file_path(group_name, filepath, sizeof(filepath));
    
    /* Delete old file */
    unlink(filepath);
    
    /* Recreate with current active clients */
    FILE *f = fopen(filepath, "w");
    if (f) {
        for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
            if (clients[i].actif) {
                char ip_str[64];
                inet_ntop(AF_INET, &clients[i].addr_cli.sin_addr, ip_str, sizeof(ip_str));
                fprintf(f, "%s:%s:%s\n", clients[i].nom, ip_str, clients[i].emoji);
            }
        }
        fclose(f);
    }
}

void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

/* Add a client to the group and return status. Returns 0 if added, 1 if banned, 2 if no space */
static int add_client(const char *name,
                       struct sockaddr_in *addr, int display_port,
                       const char *emoji)
{
    /* Extract IP address first to check if banned */
    char ip_str[64];
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
    
    /* Check if this IP is banned from the group */
    if (is_ip_banned(g_group_name, ip_str)) {
        printf("Client %s (%s) rejected: IP is banned from group %s\n", 
               name, ip_str, g_group_name);
        return 1;  /* Indicate ban */
    }
    
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

            /* Rebuild the group file with all current clients */
            rebuild_group_file(g_group_name);
            
            return 0;  /* Success */
        }
    }
    printf("Plus de place pour de nouveaux clients dans ce groupe\n");
    return 2;  /* No space */
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

    /* store group name globally for helpers that need it */
    snprintf(g_group_name, sizeof(g_group_name), "%s", nom_groupe);

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

        /* DEBUG: afficher l'origine du paquet reçu pour diagnostiquer doublons */
        addrlen = sizeof(addr_src);               /* assure addrlen correct */
        {
            char ip_src[64];
            inet_ntop(AF_INET, &addr_src.sin_addr, ip_src, sizeof(ip_src));
            printf("[DEBUG GROUPE] paquet reçu ordre='%s' emetteur='%s' texte='%s' depuis %s:%d\n",
                   msg.ordre, msg.emetteur, msg.texte, ip_src, ntohs(addr_src.sin_port));
            fflush(stdout);
        }

        if (strncmp(msg.ordre, ORDRE_CON, 3) == 0) {
            /* msg.texte contient le port d'affichage du client */
            int display_port = atoi(msg.texte);
            int status = add_client(msg.emetteur, &addr_src, display_port, msg.emoji);
            
            /* If client was banned, send an error message */
            if (status == 1) {
                ISYMessage error_msg;
                memset(&error_msg, 0, sizeof(error_msg));
                strcpy(error_msg.ordre, ORDRE_MSG);
                snprintf(error_msg.emetteur, MAX_USERNAME, "SERVER");
                choose_emoji_from_username("SERVER", error_msg.emoji);
                strncpy(error_msg.groupe, nom_groupe, MAX_GROUP_NAME - 1);
                error_msg.groupe[MAX_GROUP_NAME - 1] = '\0';
                snprintf(error_msg.texte, sizeof(error_msg.texte), "VOUS_ETES_BANNI");
                
                /* Send error message to the banned client's display port */
                struct sockaddr_in addr_display;
                memcpy(&addr_display, &addr_src, sizeof(addr_src));
                addr_display.sin_port = htons(display_port);
                ssize_t s = sendto(sock_grp, &error_msg, sizeof(error_msg), 0,
                                   (struct sockaddr *)&addr_display, sizeof(addr_display));
                if (s < 0) perror("sendto ban error");
            }
        }
        else if (strncmp(msg.ordre, ORDRE_MSG, 3) == 0) {
            if (stats) stats->nb_messages++;
            snprintf(msg.groupe, MAX_GROUP_NAME, "%s", nom_groupe);

            /* If moderator requests list of members by sending "list" in chat,
               respond privately to the moderator's display with the member list. */
            if (strcasecmp(msg.texte, "list") == 0) {
                if (strcmp(msg.emetteur, moderateur) == 0) {
                    /* Read list from file */
                    char buf[MAX_TEXT]; buf[0] = '\0';
                    char filepath[256];
                    build_group_file_path(nom_groupe, filepath, sizeof(filepath));
                    
                    FILE *f = fopen(filepath, "r");
                    if (f) {
                        char line[256];
                        int first = 1;
                        while (fgets(line, sizeof(line), f)) {
                            char username[MAX_USERNAME];
                            char ip_str[64];
                            char emoji[MAX_EMOJI];
                            if (sscanf(line, "%19[^:]:%63[^:]:%s", username, ip_str, emoji) == 3) {
                                if (!first) {
                                    strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
                                }
                                char member_line[128];
                                snprintf(member_line, sizeof(member_line), "%s %s (%s)", 
                                        emoji, ip_str, username);
                                strncat(buf, member_line, sizeof(buf) - strlen(buf) - 1);
                                first = 0;
                            }
                        }
                        fclose(f);
                    }
                    if (buf[0] == '\0') snprintf(buf, sizeof(buf), "Aucun membre\n");

                    ISYMessage resp;
                    memset(&resp, 0, sizeof(resp));
                    strcpy(resp.ordre, ORDRE_MSG);
                    strncpy(resp.emetteur, "SERVER", MAX_USERNAME - 1);
                    resp.emetteur[MAX_USERNAME - 1] = '\0';
                    choose_emoji_from_username("SERVER", resp.emoji);
                    strncpy(resp.groupe, nom_groupe, MAX_GROUP_NAME - 1);
                    resp.groupe[MAX_GROUP_NAME - 1] = '\0';
                    snprintf(resp.texte, sizeof(resp.texte), "%s", buf);

                    /* find moderator's display address in clients[] */
                    int found = 0;
                    struct sockaddr_in target;
                    memset(&target, 0, sizeof(target));
                    for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
                        if (clients[i].actif && strcmp(clients[i].nom, msg.emetteur) == 0) {
                            target = clients[i].addr_cli;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        /* fallback: reply to sender address (addr_src) */
                        target = addr_src;
                    }
                    ssize_t s = sendto(sock_grp, &resp, sizeof(resp), 0, (struct sockaddr *)&target, sizeof(target));
                    if (s < 0) perror("sendto list reply");
                } else {
                    /* notify sender that only moderator can list */
                    ISYMessage deny;
                    memset(&deny,0,sizeof(deny));
                    strcpy(deny.ordre, ORDRE_MSG);
                    strncpy(deny.emetteur, "SERVER", MAX_USERNAME-1);
                    deny.emetteur[MAX_USERNAME-1] = '\0';
                    choose_emoji_from_username("SERVER", deny.emoji);
                    snprintf(deny.texte, sizeof(deny.texte), "Permission refusee: seul le moderateur peut lister les membres");
                    ssize_t s = sendto(sock_grp, &deny, sizeof(deny), 0, (struct sockaddr *)&addr_src, sizeof(addr_src));
                    if (s < 0) perror("sendto deny");
                }
            } else if (strncmp(msg.texte, "ban ", 4) == 0) {
                /* Ban command: "ban <ip>" - only moderator can use this */
                if (strcmp(msg.emetteur, moderateur) == 0) {
                    char ban_ip[64] = {0};
                    if (sscanf(msg.texte, "ban %63s", ban_ip) == 1) {
                        /* Find and disconnect the client with this IP */
                        int found_client = -1;
                        for (int i = 0; i < MAX_CLIENTS_GROUP; ++i) {
                            if (clients[i].actif) {
                                char client_ip[64];
                                inet_ntop(AF_INET, &clients[i].addr_cli.sin_addr, client_ip, sizeof(client_ip));
                                if (strcmp(client_ip, ban_ip) == 0) {
                                    found_client = i;
                                    break;
                                }
                            }
                        }
                        
                        if (found_client != -1) {
                            /* Ban the IP from the group */
                            ban_ip_from_group(nom_groupe, ban_ip);
                            
                            /* Remove the client from active clients */
                            char banned_username[MAX_USERNAME];
                            snprintf(banned_username, sizeof(banned_username), "%s", clients[found_client].nom);
                            clients[found_client].actif = 0;
                            if (stats) stats->nb_clients--;
                            
                            /* Send ban message directly to the banned client to force them out */
                            ISYMessage ban_msg;
                            memset(&ban_msg, 0, sizeof(ban_msg));
                            strcpy(ban_msg.ordre, ORDRE_MSG);
                            snprintf(ban_msg.emetteur, MAX_USERNAME, "SERVER");
                            choose_emoji_from_username("SERVER", ban_msg.emoji);
                            strncpy(ban_msg.groupe, nom_groupe, MAX_GROUP_NAME - 1);
                            snprintf(ban_msg.texte, sizeof(ban_msg.texte), "VOUS_ETES_BANNI");
                            
                            struct sockaddr_in addr_banned;
                            memset(&addr_banned, 0, sizeof(addr_banned));
                            addr_banned.sin_family = AF_INET;
                            addr_banned.sin_addr = clients[found_client].addr_cli.sin_addr;
                            addr_banned.sin_port = clients[found_client].addr_cli.sin_port;
                            
                            ssize_t s = sendto(sock_grp, &ban_msg, sizeof(ban_msg), 0,
                                               (struct sockaddr *)&addr_banned, sizeof(addr_banned));
                            if (s < 0) perror("sendto force ban message");
                            
                            /* Notify all clients that someone was banned */
                            ISYMessage ban_notice;
                            memset(&ban_notice, 0, sizeof(ban_notice));
                            strcpy(ban_notice.ordre, ORDRE_MSG);
                            snprintf(ban_notice.emetteur, MAX_USERNAME, "SERVER");
                            choose_emoji_from_username("SERVER", ban_notice.emoji);
                            strncpy(ban_notice.groupe, nom_groupe, MAX_GROUP_NAME - 1);
                            ban_notice.groupe[MAX_GROUP_NAME - 1] = '\0';
                            snprintf(ban_notice.texte, sizeof(ban_notice.texte), "%s a ete banni du groupe (%s)", 
                                    banned_username, ban_ip);
                            broadcast_message(&ban_notice);
                            
                            /* Rebuild the group file with remaining clients */
                            rebuild_group_file(nom_groupe);
                            
                            printf("Client %s (%s) a ete banni du groupe %s\n", 
                                   banned_username, ban_ip, nom_groupe);
                        } else {
                            /* IP not found in current clients */
                            ISYMessage error;
                            memset(&error, 0, sizeof(error));
                            strcpy(error.ordre, ORDRE_MSG);
                            snprintf(error.emetteur, MAX_USERNAME, "SERVER");
                            choose_emoji_from_username("SERVER", error.emoji);
                            snprintf(error.texte, sizeof(error.texte), "IP %s non trouvee dans le groupe", ban_ip);
                            ssize_t s = sendto(sock_grp, &error, sizeof(error), 0, (struct sockaddr *)&addr_src, sizeof(addr_src));
                            if (s < 0) perror("sendto ban error");
                        }
                    }
                } else {
                    /* Only moderator can ban */
                    ISYMessage deny;
                    memset(&deny, 0, sizeof(deny));
                    strcpy(deny.ordre, ORDRE_MSG);
                    snprintf(deny.emetteur, MAX_USERNAME, "SERVER");
                    choose_emoji_from_username("SERVER", deny.emoji);
                    snprintf(deny.texte, sizeof(deny.texte), "Permission refusee: seul le moderateur peut bannir");
                    ssize_t s = sendto(sock_grp, &deny, sizeof(deny), 0, (struct sockaddr *)&addr_src, sizeof(addr_src));
                    if (s < 0) perror("sendto deny ban");
                }
            } else {
                broadcast_message(&msg);
            }
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
