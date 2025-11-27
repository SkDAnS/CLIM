#include "../include/Commun.h"
#include <arpa/inet.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

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
int bind_socket(int sockfd, const char* ip, int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (strcmp(ip, "0.0.0.0") == 0)
        addr.sin_addr.s_addr = INADDR_ANY;   // écoute sur toutes interfaces
    else
        inet_pton(AF_INET, ip, &addr.sin_addr);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }

    printf("[SOCKET] Bind OK sur %s:%d\n", ip, port);
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


/* ============================================================
 * Génère un avatar Unicode basé sur l'adresse IP
 * (Utilise la plage U+2600 → U+26FF : symboles divers)
 * ============================================================ */
const char* get_avatar_from_ip(const char* ip)
{
    static char buffer[8];

    setlocale(LC_CTYPE, "");

    unsigned long hash = 0;
    for (int i = 0; ip[i]; i++)
        hash = hash * 31 + (unsigned char)ip[i];

    unsigned long codepoint = 0x2600 + (hash % 256);

    snprintf(buffer, sizeof(buffer), "%lc", (wint_t)codepoint);
    return buffer;
}

/* ============================================================
 * Notification sonore (simple beep terminal)
 * ============================================================ */
void jouer_son_notification(void)
{
    printf("\a");     /* beep terminal */
    fflush(stdout);
}


/* ============================================================
   LECTURE CONFIG SERVEUR
   ============================================================ */

void load_server_config(char *ip_out, int *port_out)
{
    FILE *f = fopen("config/serveur.conf", "r");
    if (!f) {
        perror("serveur.conf");
        exit(1);
    }

    char key[64], val[128], line[256];
    while (fgets(line, sizeof(line), f)) {

        if (sscanf(line, "%63[^=]=%127s", key, val) == 2) {

            if (!strcmp(key, "IP"))
                strncpy(ip_out, val, TAILLE_IP - 1);

            else if (!strcmp(key, "PORT"))
                *port_out = atoi(val);
        }
    }

    fclose(f);
}
