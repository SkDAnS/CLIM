#ifndef NOTIF_H
#define NOTIF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define MAX_NOM 256
#define MAX_SONS 50

/* Liste les fichiers .wav du dossier 'sons' */
int listerSons(char nomsSons[][MAX_NOM]);

/* Joue un son de notification (lance aplay sur Linux, start sur Windows) */
void jouerSon(const char *nomFichier);

#endif /* NOTIF_H */
