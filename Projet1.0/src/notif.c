#include "../include/notif.h"

int listerSons(char nomsSons[][MAX_NOM])
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    dir = opendir("sons");
    if (!dir) {
        perror("Impossible d'ouvrir le dossier 'sons'");
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        /* On ne garde que les fichiers .wav */
        if (strstr(entry->d_name, ".wav")) {
            snprintf(nomsSons[count], MAX_NOM, "%s", entry->d_name);
            count++;
            if (count >= MAX_SONS) break; /* Limite de sons */
        }
    }

    closedir(dir);
    return count;
}

void jouerSon(const char *nomFichier)
{
    char commande[512];
#ifdef _WIN32
    snprintf(commande, sizeof(commande), "start sons/%s", nomFichier);
#else
    snprintf(commande, sizeof(commande), "ffplay -nodisp -autoexit \"sons/%s\" 2>/dev/null &", nomFichier);
#endif
    /* Launch in background (& suffix on Linux) to avoid blocking */
    system(commande);
}
