//test
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "../include/Commun.h"
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
/* Use nanosleep instead of usleep for portability */
static void msleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms/1000;
    ts.tv_nsec = (ms%1000) * 1000000L;
    nanosleep(&ts, NULL);
}
/* Some toolchains (or non-POSIX targets) may not declare kill; declare it explicitly
 * to avoid implicit declaration warnings on all environments. The prototype uses
 * pid_t which is available through <sys/types.h>. */
extern int kill(pid_t pid, int sig);

static int sock_srv;
static GroupeInfo groupes[MAX_GROUPS];
static int running = 1;
//les deux prochains pour la recherche du serveur sur le réseaux 
/* Broadcast discovery removed - code cleaned up / disabled */

/* Helper function to clean up all group info files */
static void cleanup_infogroup_files(void)
{
    DIR *dir = opendir("infoGroup");
    if (!dir) return;  /* Directory doesn't exist */
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  /* Regular file */
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "infoGroup/%s", entry->d_name);
            unlink(filepath);  /* Delete the file (both group info and banned files) */
        }
    }
    closedir(dir);
}

/* Nettoyage sur CTRL-C */
void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
    cleanup_infogroup_files();  /* Clean up all group info files */
    exit(0);
}

/* Merge member lists from two groups into target group, avoiding duplicate IPs */
static void merge_group_member_files(const char *src1, const char *src2, const char *target)
{
    /* Read all members from both source files, store unique IPs in a map */
    #define MAX_MERGED_MEMBERS 256
    typedef struct {
        char username[MAX_USERNAME];
        char ip[64];
        char emoji[MAX_EMOJI];
    } MergedMember;
    
    MergedMember members[MAX_MERGED_MEMBERS];
    int member_count = 0;
    
    /* Helper: check if IP already exists */
    int ip_exists(const char *ip) {
        for (int i = 0; i < member_count; ++i) {
            if (strcmp(members[i].ip, ip) == 0) {
                return 1;
            }
        }
        return 0;
    }
    
    /* Helper: add member if not duplicate */
    void add_member(const char *username, const char *ip, const char *emoji) {
        if (!ip_exists(ip) && member_count < MAX_MERGED_MEMBERS) {
            snprintf(members[member_count].username, MAX_USERNAME, "%s", username);
            snprintf(members[member_count].ip, 64, "%s", ip);
            snprintf(members[member_count].emoji, MAX_EMOJI, "%s", emoji);
            member_count++;
        }
    }
    
    /* Read from src1 */
    {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", src1);
        FILE *f = fopen(filepath, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char username[MAX_USERNAME];
                char ip[64];
                char emoji[MAX_EMOJI];
                if (sscanf(line, "%19[^:]:%63[^:]:%7s", username, ip, emoji) == 3) {
                    if (!ip_exists(ip) && member_count < MAX_MERGED_MEMBERS) {
                        snprintf(members[member_count].username, MAX_USERNAME, "%s", username);
                        snprintf(members[member_count].ip, 64, "%s", ip);
                        snprintf(members[member_count].emoji, MAX_EMOJI, "%s", emoji);
                        member_count++;
                    }
                }
            }
            fclose(f);
        }
    }
    
    /* Read from src2 */
    {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", src2);
        FILE *f = fopen(filepath, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char username[MAX_USERNAME];
                char ip[64];
                char emoji[MAX_EMOJI];
                if (sscanf(line, "%19[^:]:%63[^:]:%7s", username, ip, emoji) == 3) {
                    if (!ip_exists(ip) && member_count < MAX_MERGED_MEMBERS) {
                        snprintf(members[member_count].username, MAX_USERNAME, "%s", username);
                        snprintf(members[member_count].ip, 64, "%s", ip);
                        snprintf(members[member_count].emoji, MAX_EMOJI, "%s", emoji);
                        member_count++;
                    }
                }
            }
            fclose(f);
        }
    }
    
    /* Write merged list to target file */
    {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", target);
        FILE *f = fopen(filepath, "w");
        if (f) {
            for (int i = 0; i < member_count; ++i) {
                fprintf(f, "%s:%s:%s\n", members[i].username, members[i].ip, members[i].emoji);
            }
            fclose(f);
        }
    }
    
    #undef MAX_MERGED_MEMBERS
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

    /* Père : continue - enregistrer PID pour gestion ultérieure (merge/stop) */
    groupes[index].pid = pid;
    return 0;
}

/* Traite une commande CMD reçue du client */
static void handle_command(ISYMessage *msg,
                           struct sockaddr_in *src, socklen_t src_len)
{
    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src->sin_addr, src_ip, sizeof(src_ip));
    int src_port = ntohs(src->sin_port);
    printf("[SERVER] Command from %s:%d -> %s\n", src_ip, src_port, msg->texte);
    fflush(stdout);
    ISYMessage reply;
    memset(&reply, 0, sizeof(reply));
    strcpy(reply.ordre, ORDRE_RPL);
    strncpy(reply.emetteur, "SERVER", MAX_USERNAME - 1);
    reply.emetteur[MAX_USERNAME - 1] = '\0';
    choose_emoji_from_username("SERVER", reply.emoji);

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
        reply.texte[MAX_TEXT - 1] = '\0';
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
                snprintf(groupes[slot].nom, MAX_GROUP_NAME, "%.*s", (int)(MAX_GROUP_NAME - 1), arg1);
                snprintf(groupes[slot].moderateur, MAX_USERNAME, "%.*s", (int)(MAX_USERNAME - 1), msg->emetteur);
                groupes[slot].port_groupe = GROUP_PORT_BASE + slot;

                /* Optionnel : SHM stats */
                key_t key = SHM_GROUP_KEY_BASE + slot;
                int shm_id = shmget(key, sizeof(GroupStats),
                                    IPC_CREAT | 0666);
                check_fatal(shm_id < 0, "shmget group");
                groupes[slot].shm_key = key;
                groupes[slot].shm_id  = shm_id;

                    create_group_process(slot);
                            /* Append new group to group_members.txt (avoid duplicates) */
                            {
                                FILE *f = fopen("group_members.txt", "r");
                                int seen = 0;
                                if (f) {
                                    char line[256];
                                    while (fgets(line, sizeof(line), f)) {
                                        if (strncmp(line, "GROUP:", 6) == 0) {
                                            char *gname = line + 6;
                                            char *p = strchr(gname, '\n'); if (p) *p = '\0';
                                            if (strcmp(gname, groupes[slot].nom) == 0) { seen = 1; break; }
                                        }
                                    }
                                    fclose(f);
                                }
                                if (!seen) {
                                    FILE *fa = fopen("group_members.txt", "a");
                                    if (fa) {
                                        fprintf(fa, "GROUP:%s\n", groupes[slot].nom);
                                        fclose(fa);
                                    }
                                }
                            }
                /* Give child a moment to start and fail if exec failed */
                msleep_ms(100); /* 100ms */
                if (groupes[slot].pid > 0) {
                    int status;
                    pid_t r = waitpid(groupes[slot].pid, &status, WNOHANG);
                    if (r == groupes[slot].pid) {
                        /* Child terminated immediately -> creation failed */
                            strncpy(reply.texte, "Erreur: echec demarrage GroupeISY", MAX_TEXT - 1);
                            reply.texte[MAX_TEXT - 1] = '\0';
                        /* cleanup */
                        groupes[slot].actif = 0;
                        if (groupes[slot].shm_id > 0) { shmctl(groupes[slot].shm_id, IPC_RMID, NULL); groupes[slot].shm_id = 0; }
                        groupes[slot].shm_key = 0;
                        groupes[slot].pid = 0;
                        sendto(sock_srv, &reply, sizeof(reply), 0,
                               (struct sockaddr *)src, src_len);
                        return;
                    }
                }
                snprintf(reply.texte, MAX_TEXT,
                         "Groupe %s cree sur port %d",
                         groupes[slot].nom,
                         groupes[slot].port_groupe);

                /* Append group name to group_members.txt (avoid duplicates) */
                {
                    FILE *f = fopen("group_members.txt", "r");
                    int seen = 0;
                    if (f) {
                        char line[256];
                        while (fgets(line, sizeof(line), f)) {
                            if (strncmp(line, "GROUP:", 6) == 0) {
                                char *gname = line + 6;
                                char *p = strchr(gname, '\n'); if (p) *p = '\0';
                                if (strcmp(gname, groupes[slot].nom) == 0) { seen = 1; break; }
                            }
                        }
                        fclose(f);
                    }
                    if (!seen) {
                        FILE *fa = fopen("group_members.txt", "a");
                        if (fa) {
                            fprintf(fa, "GROUP:%s\n", groupes[slot].nom);
                            fclose(fa);
                        }
                    }
                }
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
            reply.groupe[MAX_GROUP_NAME - 1] = '\0';
        }
    }
    else if (strcmp(cmd, "CHECKBAN") == 0) {
        /* Check if the client's IP (from source) is banned from a group */
        /* Expected format: CHECKBAN <group_name> */
        char group_name[64] = {0};
        sscanf(msg->texte, "%15s %63s", cmd, group_name);
        
        if (group_name[0] == '\0') {
            strcpy(reply.texte, "Usage: CHECKBAN <group_name>");
        } else {
            /* Get the client's IP from the source address */
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src->sin_addr, client_ip, sizeof(client_ip));
            
            /* Check if the banned IPs file exists and contains this IP */
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "infoGroup/%s_banned.txt", group_name);
            
            int is_banned = 0;
            FILE *f = fopen(filepath, "r");
            if (f) {
                char line[64];
                while (fgets(line, sizeof(line), f)) {
                    char *p = strchr(line, '\n');
                    if (p) *p = '\0';
                    if (strcmp(line, client_ip) == 0) {
                        is_banned = 1;
                        break;
                    }
                }
                fclose(f);
            }
            
            if (is_banned) {
                snprintf(reply.texte, MAX_TEXT, "BANNED");
            } else {
                snprintf(reply.texte, MAX_TEXT, "OK");
            }
        }
    }
    else if (strcmp(cmd, "MERGE") == 0) {
        /* Expected: MERGE <g1> <g2> - merge g1 members into g2, then delete g1 */
        char g1[64] = {0};
        char g2[64] = {0};
        sscanf(msg->texte, "%15s %63s %63s", cmd, g1, g2);

        if (g1[0] == '\0' || g2[0] == '\0') {
            strcpy(reply.texte, "Usage: MERGE <g1> <g2>");
        } else {
            int idx1 = find_group(g1);
            int idx2 = find_group(g2);

            if (idx1 < 0 || idx2 < 0) {
                snprintf(reply.texte, MAX_TEXT,
                         "Un ou plusieurs groupes introuvables (%s, %s)", g1, g2);
            } else if (idx1 == idx2) {
                snprintf(reply.texte, MAX_TEXT,
                         "Les deux groupes doivent etre distincts: %s", g1);
            } else {
                /* Permission: only the creator (moderateur) of BOTH groups may merge them */
                if (strcmp(msg->emetteur, groupes[idx1].moderateur) != 0 ||
                    strcmp(msg->emetteur, groupes[idx2].moderateur) != 0) {
                    snprintf(reply.texte, MAX_TEXT,
                             "Permission refusee: vous devez etre le createur (moderateur) des deux groupes pour fusionner");
                    /* send immediate reply and return */
                    ssize_t ret = sendto(sock_srv, &reply, sizeof(reply), 0,
                           (struct sockaddr *)src, src_len);
                    if (ret < 0) perror("sendto reply");
                    return;
                }
                
                /* Merge members of g1 into g2, avoiding duplicates by IP */
                /* First read both files before deleting them */
                typedef struct {
                    char username[MAX_USERNAME];
                    char ip[64];
                    char emoji[MAX_EMOJI];
                } MergedMember;
                
                MergedMember merged[512];
                int merged_count = 0;
                
                /* Read from g1 */
                {
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", g1);
                    FILE *f = fopen(filepath, "r");
                    if (f) {
                        char line[256];
                        while (fgets(line, sizeof(line), f) && merged_count < 512) {
                            char username[MAX_USERNAME];
                            char ip[64];
                            char emoji[MAX_EMOJI];
                            if (sscanf(line, "%19[^:]:%63[^:]:%7s", username, ip, emoji) == 3) {
                                /* Check if IP already exists */
                                int found = 0;
                                for (int i = 0; i < merged_count; ++i) {
                                    if (strcmp(merged[i].ip, ip) == 0) {
                                        found = 1;
                                        break;
                                    }
                                }
                                if (!found) {
                                    snprintf(merged[merged_count].username, MAX_USERNAME, "%s", username);
                                    snprintf(merged[merged_count].ip, 64, "%s", ip);
                                    snprintf(merged[merged_count].emoji, MAX_EMOJI, "%s", emoji);
                                    merged_count++;
                                }
                            }
                        }
                        fclose(f);
                    }
                }
                
                /* Read from g2 */
                {
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", g2);
                    FILE *f = fopen(filepath, "r");
                    if (f) {
                        char line[256];
                        while (fgets(line, sizeof(line), f) && merged_count < 512) {
                            char username[MAX_USERNAME];
                            char ip[64];
                            char emoji[MAX_EMOJI];
                            if (sscanf(line, "%19[^:]:%63[^:]:%7s", username, ip, emoji) == 3) {
                                /* Check if IP already exists */
                                int found = 0;
                                for (int i = 0; i < merged_count; ++i) {
                                    if (strcmp(merged[i].ip, ip) == 0) {
                                        found = 1;
                                        break;
                                    }
                                }
                                if (!found) {
                                    snprintf(merged[merged_count].username, MAX_USERNAME, "%s", username);
                                    snprintf(merged[merged_count].ip, 64, "%s", ip);
                                    snprintf(merged[merged_count].emoji, MAX_EMOJI, "%s", emoji);
                                    merged_count++;
                                }
                            }
                        }
                        fclose(f);
                    }
                }
                
                /* Delete both original files */
                {
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", g1);
                    unlink(filepath);
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", g2);
                    unlink(filepath);
                }
                
                /* Write merged result to g2 */
                {
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", g2);
                    FILE *f = fopen(filepath, "w");
                    if (f) {
                        for (int i = 0; i < merged_count; ++i) {
                            fprintf(f, "%s:%s:%s\n", merged[i].username, merged[i].ip, merged[i].emoji);
                        }
                        fclose(f);
                    }
                }
                
                /* Notify g1 clients to migrate to g2 */
                ISYMessage migr_msg;
                memset(&migr_msg, 0, sizeof(migr_msg));
                strcpy(migr_msg.ordre, ORDRE_MGR);
                strncpy(migr_msg.emetteur, "SERVER", MAX_USERNAME - 1);
                migr_msg.emetteur[MAX_USERNAME - 1] = '\0';
                choose_emoji_from_username("SERVER", migr_msg.emoji);
                snprintf(migr_msg.texte, sizeof(migr_msg.texte), "MIGRATE %s %d", g2, groupes[idx2].port_groupe);
                
                struct sockaddr_in addr1;
                fill_sockaddr(&addr1, "127.0.0.1", groupes[idx1].port_groupe);
                ssize_t r = sendto(sock_srv, &migr_msg, sizeof(migr_msg), 0,
                                   (struct sockaddr *)&addr1, sizeof(addr1));
                if (r < 0) perror("sendto migrate g1->g2");
                
                /* Kill and cleanup g1 */
                if (groupes[idx1].pid > 0) {
                    pid_t pid1 = groupes[idx1].pid;
                    kill(pid1, SIGTERM);
                    int gaveup = 0;
                    for (int t = 0; t < 30; ++t) {
                        pid_t r = waitpid(pid1, NULL, WNOHANG);
                        if (r == pid1) break;
                        msleep_ms(100);
                        if (t == 29) gaveup = 1;
                    }
                    if (gaveup) {
                        kill(pid1, SIGKILL);
                        waitpid(pid1, NULL, 0);
                    }
                    groupes[idx1].pid = 0;
                }
                
                if (groupes[idx1].shm_id > 0) {
                    shmctl(groupes[idx1].shm_id, IPC_RMID, NULL);
                    groupes[idx1].shm_id = 0;
                }
                
                groupes[idx1].shm_key = 0;
                groupes[idx1].actif = 0;
                
                /* Clean up g1 ban file (g1.txt already deleted during merge) */
                {
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s_banned.txt", g1);
                    unlink(filepath);
                }
                
                snprintf(reply.texte, MAX_TEXT, "Groupe %s fusionne dans %s (port %d). Tous les membres sont maintenant dans %s.",
                         g1, g2, groupes[idx2].port_groupe, g2);
                printf("[SERVER] Merge: %s -> %s\n", g1, g2);
                fflush(stdout);
            }
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
            /* Signaler le processus GroupeISY (SIGTERM + nettoyage SHM) */
                if (groupes[idx].pid > 0) {
                    kill(groupes[idx].pid, SIGTERM);
                    waitpid(groupes[idx].pid, NULL, 0);
                    groupes[idx].pid = 0;
                }
                if (groupes[idx].shm_id > 0) {
                    shmctl(groupes[idx].shm_id, IPC_RMID, NULL);
                    groupes[idx].shm_id = 0;
                    groupes[idx].shm_key = 0;
                }
                /* Clean up the group info file */
                {
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s.txt", arg1);
                    unlink(filepath);  /* Delete the file, ignore errors */
                    /* Also clean up the banned list file */
                    snprintf(filepath, sizeof(filepath), "infoGroup/%s_banned.txt", arg1);
                    unlink(filepath);
                }
        }
    }
    else {
        snprintf(reply.texte, MAX_TEXT,
                 "Commande inconnue: %s", cmd);
    }
    
    /* Send reply back to client */
    ssize_t ret = sendto(sock_srv, &reply, sizeof(reply), 0,
                         (struct sockaddr *)src, src_len);
    if (ret < 0) {
        perror("sendto reply");
    }
}

int main(void)
{
    struct sockaddr_in addr_srv, addr_cli;
    socklen_t addrlen = sizeof(addr_cli);
    ISYMessage msg;

    memset(groupes, 0, sizeof(groupes));

    /* Reset the persistent group/member file on server start */
    {
        FILE *f = fopen("group_members.txt", "w");
        if (f) fclose(f);
    }

    signal(SIGINT, handle_sigint);

    sock_srv = create_udp_socket();
    /* Avoid child processes inheriting the server socket (use CLOEXEC) */
    int flags = fcntl(sock_srv, F_GETFD);
    if (flags != -1) fcntl(sock_srv, F_SETFD, flags | FD_CLOEXEC);
    fill_sockaddr(&addr_srv, NULL, SERVER_PORT);
    check_fatal(bind(sock_srv, (struct sockaddr *)&addr_srv, sizeof(addr_srv)) < 0, "bind serveur");

    /* === SOCKET POUR LA DÉCOUVERTE AUTOMATIQUE === */
    /* Broadcast discovery removed; server doesn't use broadcast any more. */
    /* === FIN SOCKET POUR LA DÉCOUVERTE AUTOMATIQUE === */


    printf("ServeurISY en écoute sur port %d\n", SERVER_PORT);

    while (running) {
        /* Broadcast discovery removed; nothing to do here */
        printf("[SERVER] Waiting for message on port %d...\n", SERVER_PORT);
        fflush(stdout);

        ssize_t n = recvfrom(sock_srv, &msg, sizeof(msg), 0,
                             (struct sockaddr *)&addr_cli, &addrlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        printf("[SERVER] recvfrom returned %zd bytes from %s:%d\n",
               n, inet_ntoa(addr_cli.sin_addr), ntohs(addr_cli.sin_port));
        fflush(stdout);

        if (strncmp(msg.ordre, ORDRE_CMD, 3) == 0) {
            handle_command(&msg, &addr_cli, addrlen);
        } else {
            /* Messages inattendus au serveur */
            fprintf(stderr, "Ordre inconnu recu par serveur: %s\n",
                    msg.ordre);
        }
    }

    close(sock_srv);
    cleanup_infogroup_files();  /* Clean up all group info files before shutting down */
    printf("ServeurISY termine\n");
    return 0;
}
