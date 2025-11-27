#include "../include/Commun.h"
#include <arpa/inet.h>
#include <string.h>

/* ---------------------------------------------------------
   Socket UDP
--------------------------------------------------------- */
int creer_socket_udp()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) perror("socket");
    return sock;
}

/* ---------------------------------------------------------
   Bind standardisé
--------------------------------------------------------- */
int bind_socket(int sockfd, const char *ip, int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;

    if (!ip || strlen(ip)==0 || strcmp(ip, "0.0.0.0")==0)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else
        inet_pton(AF_INET, ip, &addr.sin_addr);

    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }

    printf("[SOCKET] Bind sur %s:%d\n",
           (ip ? ip : "0.0.0.0"), port);

    return 0;
}

/* ---------------------------------------------------------
   ENVOI MESSAGE
--------------------------------------------------------- */
int envoyer_message(int sockfd, const struct_message *msg,
                    const char *ip, int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    addr.sin_port = htons(port);

    return sendto(sockfd, msg, sizeof(struct_message), 0,
                  (struct sockaddr*)&addr, sizeof(addr));
}

/* ---------------------------------------------------------
   RECEPTION MESSAGE
--------------------------------------------------------- */
int recevoir_message(int sockfd, struct_message *msg,
                     char *ip_out, int *port_out)
{
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);

    int n = recvfrom(sockfd, msg, sizeof(struct_message), 0,
                     (struct sockaddr*)&src, &slen);

    if (n <= 0) return -1;

    inet_ntop(AF_INET, &src.sin_addr, ip_out, TAILLE_IP);
    *port_out = ntohs(src.sin_port);

    return n;
}

/* ---------------------------------------------------------
   CONSTRUIRE MESSAGE
--------------------------------------------------------- */
void construire_message(struct_message *msg,
                        const char *ordre,
                        const char *emetteur,
                        const char *texte)
{
    strncpy(msg->Ordre, ordre, sizeof(msg->Ordre)-1);
    strncpy(msg->Emetteur, emetteur, TAILLE_LOGIN-1);
    strncpy(msg->Texte, texte, TAILLE_TEXTE-1);
}

/* ---------------------------------------------------------
   UTILS
--------------------------------------------------------- */

void nettoyer_chaine(char *s)
{
    size_t n = strlen(s);
    while (n>0 && (s[n-1]=='\n' || s[n-1]=='\r'))
        s[--n] = '\0';
}

int trouver_groupe(Groupe *groupes, int nb, const char *nom)
{
    for (int i = 0; i < nb; i++)
        if (groupes[i].actif && strcmp(groupes[i].nom, nom)==0)
            return i;
    return -1;
}
