//test
#define _POSIX_C_SOURCE 200809L
#include "../include/Commun.h"
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
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
    snprintf(reply.emetteur, MAX_USERNAME, "%s", "SERVER");
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

        snprintf(reply.texte, MAX_TEXT, "%s", buffer);
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
                snprintf(groupes[slot].nom, MAX_GROUP_NAME, "%s", arg1);
                snprintf(groupes[slot].moderateur, MAX_USERNAME, "%s", msg->emetteur);
                groupes[slot].port_groupe = GROUP_PORT_BASE + slot;

                /* Optionnel : SHM stats */
                key_t key = SHM_GROUP_KEY_BASE + slot;
                int shm_id = shmget(key, sizeof(GroupStats),
                                    IPC_CREAT | 0666);
                check_fatal(shm_id < 0, "shmget group");
                groupes[slot].shm_key = key;
                groupes[slot].shm_id  = shm_id;

                    create_group_process(slot);
                /* Give child a moment to start and fail if exec failed */
                msleep_ms(100); /* 100ms */
                if (groupes[slot].pid > 0) {
                    int status;
                    pid_t r = waitpid(groupes[slot].pid, &status, WNOHANG);
                    if (r == groupes[slot].pid) {
                        /* Child terminated immediately -> creation failed */
                        snprintf(reply.texte, MAX_TEXT, "Erreur: echec demarrage GroupeISY");
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
            snprintf(reply.groupe, MAX_GROUP_NAME, "%s", groupes[idx].nom);
        }
    }
    else if (strcmp(cmd, "MERGE") == 0) {
        /* Expected: MERGE <g1> <g2> <newname> */
        char g1[64] = {0};
        char g2[64] = {0};
        char newname[64] = {0};
        sscanf(msg->texte, "%15s %63s %63s %63s", cmd, g1, g2, newname);

        if (g1[0] == '\0' || g2[0] == '\0') {
            strcpy(reply.texte, "Usage: MERGE <g1> <g2> <newname>");
        } else {
            int idx1 = find_group(g1);
            int idx2 = find_group(g2);

            if (idx1 < 0 || idx2 < 0) {
                snprintf(reply.texte, MAX_TEXT,
                         "Un ou plusieurs groupes introuvables (%s, %s)", g1, g2);
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

                if (newname[0] != '\0' && find_group(newname) != -1 && strcmp(newname, g1) != 0 && strcmp(newname, g2) != 0) {
                    snprintf(reply.texte, MAX_TEXT, "Nom de nouveau groupe deja existant: %s", newname);
                } else if (idx1 == idx2) {
                    snprintf(reply.texte, MAX_TEXT,
                             "Les deux groupes doivent etre distincts: %s", g1);
                } else {
                    /* Allocate a slot for the new group */
                    int slot = -1;
                    for (int i = 0; i < MAX_GROUPS; ++i) {
                        if (!groupes[i].actif) { slot = i; break; }
                    }
                    if (slot == -1) {
                        strcpy(reply.texte, "Plus de place pour créer le groupe fusionne");
                    } else {
                        /* If target name exists, do merge into existing group instead of creating new one */
                        int target_idx = -1;
                        if (newname[0] != '\0') target_idx = find_group(newname);

                        if (target_idx != -1) {
                            /* Merge into existing target group */
                            int target_port = groupes[target_idx].port_groupe;
                            /* Send MIGRATEEXIST to both groups to have them add their clients to target */
                            ISYMessage migr_msg;
                            memset(&migr_msg, 0, sizeof(migr_msg));
                            strcpy(migr_msg.ordre, ORDRE_MGR);
                            snprintf(migr_msg.emetteur, MAX_USERNAME, "%s", "SERVER");
                            strcpy(migr_msg.emoji, reply.emoji);
                            snprintf(migr_msg.texte, sizeof(migr_msg.texte), "MIGRATEEXIST %s %d", groupes[target_idx].nom, target_port);

                            struct sockaddr_in addrOld1, addrOld2;
                            fill_sockaddr(&addrOld1, "127.0.0.1", groupes[idx1].port_groupe);
                            fill_sockaddr(&addrOld2, "127.0.0.1", groupes[idx2].port_groupe);
                            if (idx1 != target_idx) {
                                ssize_t r1 = sendto(sock_srv, &migr_msg, sizeof(migr_msg), 0, (struct sockaddr*)&addrOld1, sizeof(addrOld1));
                                if (r1 < 0) perror("sendto migr_msg idx1");
                            }
                            if (idx2 != target_idx) {
                                ssize_t r2 = sendto(sock_srv, &migr_msg, sizeof(migr_msg), 0, (struct sockaddr*)&addrOld2, sizeof(addrOld2));
                                if (r2 < 0) perror("sendto migr_msg idx2");
                            }

                            /* Notify clients of the two old groups via normal MIGRATE message */
                            ISYMessage notify;
                            memset(&notify, 0, sizeof(notify));
                            strcpy(notify.ordre, ORDRE_MGR);
                            snprintf(notify.emetteur, MAX_USERNAME, "%s", "SERVER");
                            strcpy(notify.emoji, reply.emoji);
                            snprintf(notify.texte, sizeof(notify.texte), "MIGRATE %s %d", groupes[target_idx].nom, target_port);
                            if (idx1 != target_idx) sendto(sock_srv, &notify, sizeof(notify), 0, (struct sockaddr*)&addrOld1, sizeof(addrOld1));
                            if (idx2 != target_idx) sendto(sock_srv, &notify, sizeof(notify), 0, (struct sockaddr*)&addrOld2, sizeof(addrOld2));

                            /* Kill and cleanup old groups except the target group */
                            if (groupes[idx1].pid > 0 && idx1 != target_idx) {
                                pid_t pid1 = groupes[idx1].pid;
                                kill(pid1, SIGTERM);
                                int gaveup = 0;
                                for (int t=0; t<30; ++t) { pid_t r = waitpid(pid1, NULL, WNOHANG); if (r == pid1) break; msleep_ms(100); if (t==29) gaveup = 1; }
                                if (gaveup) { kill(pid1, SIGKILL); waitpid(pid1, NULL, 0); }
                                groupes[idx1].pid = 0;
                                if (groupes[idx1].shm_id > 0) { shmctl(groupes[idx1].shm_id, IPC_RMID, NULL); groupes[idx1].shm_id = 0; }
                                groupes[idx1].shm_key = 0;
                                groupes[idx1].actif = 0;
                            }
                            if (groupes[idx2].pid > 0 && idx2 != target_idx) {
                                pid_t pid2 = groupes[idx2].pid;
                                kill(pid2, SIGTERM);
                                int gaveup2 = 0;
                                for (int t=0; t<30; ++t) { pid_t r = waitpid(pid2, NULL, WNOHANG); if (r==pid2) break; msleep_ms(100); if (t==29) gaveup2 = 1; }
                                if (gaveup2) { kill(pid2, SIGKILL); waitpid(pid2, NULL, 0); }
                                groupes[idx2].pid = 0;
                                if (groupes[idx2].shm_id > 0) { shmctl(groupes[idx2].shm_id, IPC_RMID, NULL); groupes[idx2].shm_id = 0; }
                                groupes[idx2].shm_key = 0;
                                groupes[idx2].actif = 0;
                            }

                            snprintf(reply.texte, MAX_TEXT, "Groupes %s + %s fusionnes en %s (port %d)", g1, g2, groupes[target_idx].nom, groupes[target_idx].port_groupe);
                        } else {
                            /* Create new group slot as before */
                            groupes[slot].actif = 1;
                            if (newname[0] != '\0')
                                snprintf(groupes[slot].nom, MAX_GROUP_NAME, "%s", newname);
                            else
                                snprintf(groupes[slot].nom, MAX_GROUP_NAME, "%s_%s", g1, g2);
                            snprintf(groupes[slot].moderateur, MAX_USERNAME, "%s", msg->emetteur);
                            groupes[slot].port_groupe = GROUP_PORT_BASE + slot;

                            /* Optionnel : SHM stats */
                            key_t key = SHM_GROUP_KEY_BASE + slot;
                            int shm_id = shmget(key, sizeof(GroupStats), IPC_CREAT | 0666);
                            check_fatal(shm_id < 0, "shmget group");
                            groupes[slot].shm_key = key;
                            groupes[slot].shm_id  = shm_id;

                            create_group_process(slot);
                            printf("[SERVER] Groupe %s cree (slot %d, port %d, pid %d)\n",
                                groupes[slot].nom, slot, groupes[slot].port_groupe, (int)groupes[slot].pid);
                            fflush(stdout);

                            /* Notify old groups to inform their clients */
                            ISYMessage migr_msg;
                            memset(&migr_msg, 0, sizeof(migr_msg));
                            strcpy(migr_msg.ordre, ORDRE_MGR);
                            snprintf(migr_msg.emetteur, MAX_USERNAME, "%s", "SERVER");
                            strcpy(migr_msg.emoji, reply.emoji);
                            snprintf(migr_msg.texte, sizeof(migr_msg.texte), "MIGRATE %s %d", groupes[slot].nom, groupes[slot].port_groupe);
                            printf("[SERVER] Merge request: %s + %s -> %s (port %d)\n", g1, g2,
                                groupes[slot].nom, groupes[slot].port_groupe);
                            fflush(stdout);

                            struct sockaddr_in addr1, addr2;
                            fill_sockaddr(&addr1, "127.0.0.1", groupes[idx1].port_groupe);
                            fill_sockaddr(&addr2, "127.0.0.1", groupes[idx2].port_groupe);
                            ssize_t r1 = sendto(sock_srv, &migr_msg, sizeof(migr_msg), 0,
                                (struct sockaddr *)&addr1, sizeof(addr1));
                            if (r1 < 0) perror("sendto migr_msg addr1");
                            ssize_t r2 = sendto(sock_srv, &migr_msg, sizeof(migr_msg), 0,
                                (struct sockaddr *)&addr2, sizeof(addr2));
                            if (r2 < 0) perror("sendto migr_msg addr2");

                            /* Close old groups and terminate their processes/shm */
                            groupes[idx1].actif = 0;
                            groupes[idx2].actif = 0;
                            if (groupes[idx1].pid > 0) {
                                pid_t pid1 = groupes[idx1].pid;
                                kill(pid1, SIGTERM);
                                int gaveup = 0;
                                for (int t=0; t<30; ++t) { pid_t r = waitpid(pid1, NULL, WNOHANG); if (r == pid1) break; msleep_ms(100); if (t==29) gaveup = 1; }
                                if (gaveup) { kill(pid1, SIGKILL); waitpid(pid1, NULL, 0); }
                                groupes[idx1].pid = 0;
                                if (groupes[idx1].shm_id > 0) shmctl(groupes[idx1].shm_id, IPC_RMID, NULL);
                                groupes[idx1].shm_id = 0;
                                groupes[idx1].shm_key = 0;
                            }
                            if (groupes[idx2].pid > 0) {
                                pid_t pid2 = groupes[idx2].pid;
                                kill(pid2, SIGTERM);
                                int gaveup2 = 0;
                                for (int t=0; t<30; ++t) { pid_t r = waitpid(pid2, NULL, WNOHANG); if (r == pid2) break; msleep_ms(100); if (t==29) gaveup2 = 1; }
                                if (gaveup2) { kill(pid2, SIGKILL); waitpid(pid2, NULL, 0); }
                                groupes[idx2].pid = 0;
                                if (groupes[idx2].shm_id > 0) shmctl(groupes[idx2].shm_id, IPC_RMID, NULL);
                                groupes[idx2].shm_id = 0;
                                groupes[idx2].shm_key = 0;
                            }

                            snprintf(reply.texte, MAX_TEXT,
                                     "Groupes %s + %s fusionnes en %s (port %d)",
                                     groupes[idx1].nom, groupes[idx2].nom,
                                     groupes[slot].nom, groupes[slot].port_groupe);
                        }
                    }
                }
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
        }
    }
    else {
        snprintf(reply.texte, MAX_TEXT,
                 "Commande inconnue: %s", cmd);
    }

    ssize_t ret = sendto(sock_srv, &reply, sizeof(reply), 0,
           (struct sockaddr *)src, src_len);
    if (ret < 0) {
        perror("sendto reply");
    } else {
        printf("[SERVER] Sent reply back to %s:%d (%zd bytes) \n",
               inet_ntoa(src->sin_addr), ntohs(src->sin_port), ret);
        fflush(stdout);
    }
}

int main(void)
{
    struct sockaddr_in addr_srv, addr_cli;
    socklen_t addrlen = sizeof(addr_cli);
    ISYMessage msg;

    memset(groupes, 0, sizeof(groupes));

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
    printf("ServeurISY termine\n");
    return 0;
}
