#include "../include/Commun.h"
#include "../include/notif.h"

/* Global: list of available sounds */
static char sonsList[MAX_SONS][MAX_NOM];
static int nbSons = 0;

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <port_affichage> <nom_utilisateur>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    const char *username = argv[2];

    /* Attache SHM client-affichage */
    int shm_id = shmget(SHM_CLIENT_KEY, sizeof(ClientDisplayShm),
                        IPC_CREAT | 0666);
    check_fatal(shm_id < 0, "shmget client");
    ClientDisplayShm *shm =
        (ClientDisplayShm *)shmat(shm_id, NULL, 0);
    check_fatal(shm == (void *)-1, "shmat client");

    shm->running = 1;
    shm->notify_flag = 0;
    shm->notify[0] = '\0';

    int sock = create_udp_socket();
    struct sockaddr_in addr_local, addr_src;
    socklen_t addrlen = sizeof(addr_src);

    fill_sockaddr(&addr_local, NULL, port);
    check_fatal(bind(sock, (struct sockaddr *)&addr_local,
                     sizeof(addr_local)) < 0, "bind affichage");

    printf("AffichageISY (%s) écoute sur port %d\n",
           username, port);

    /* Load available sounds from 'sons' folder */
    nbSons = listerSons(sonsList);
    if (nbSons > 0) {
        printf("Sons disponibles: ");
        for (int i = 0; i < nbSons; i++) {
            printf("%s ", sonsList[i]);
        }
        printf("\n");
    }

    ISYMessage msg;

    while (shm->running) {
        ssize_t n = recvfrom(sock, &msg, sizeof(msg), 0,
                             (struct sockaddr *)&addr_src, &addrlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom affichage");
            break;
        }



        if (strncmp(msg.ordre, ORDRE_MSG, 3) == 0) {
            /* Check if this is a ban message */
            if (strcmp(msg.texte, "VOUS_ETES_BANNI") == 0) {
                printf("\n🚫 VOUS AVEZ ÉTÉ BANNI DE CE GROUPE!\n\n");
                fflush(stdout);
                /* Signal client to return to menu */
                shm->running = 0;
                break;
            }
            
            printf("[%s] %s %s : %s\n",
                   msg.groupe,
                   msg.emoji,
                   msg.emetteur,
                   msg.texte);
            fflush(stdout);

            /* Play notification sound from SHM selection */
            if (shm->sound_name[0] != '\0') {
                jouerSon(shm->sound_name);
            } else if (nbSons > 0) {
                /* Fallback: use first available sound if none selected */
                jouerSon(sonsList[0]);
            }

            /* Auto-join capability: if the server broadcasts a machine-parsable 'MIGRATE name port' text, store notify */
            if (strncmp(msg.texte, "MIGRATE ", 7) == 0) {
                /* copy into SHM for the client */
                snprintf(shm->notify, MAX_TEXT, "%s", msg.texte);
                shm->notify_flag = 1;
            }
        }
    }

    close(sock);
    shmdt(shm);  /* On laisse éventuellement la SHM exister pour
                    d’autres clients */

    printf("AffichageISY termine\n");
    return 0;
}
