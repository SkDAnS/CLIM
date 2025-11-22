#include "Commun.h"

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

    int sock = create_udp_socket();
    struct sockaddr_in addr_local, addr_src;
    socklen_t addrlen = sizeof(addr_src);

    fill_sockaddr(&addr_local, NULL, port);
    check_fatal(bind(sock, (struct sockaddr *)&addr_local,
                     sizeof(addr_local)) < 0, "bind affichage");

    printf("AffichageISY (%s) écoute sur port %d\n",
           username, port);

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
            printf("[%s] %s : %s\n",
                   msg.groupe,
                   msg.emetteur,
                   msg.texte);
            fflush(stdout);
        }
    }

    close(sock);
    shmdt(shm);  /* On laisse éventuellement la SHM exister pour
                    d’autres clients */

    printf("AffichageISY termine\n");
    return 0;
}
