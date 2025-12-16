# CLIM - Système de Chat Distribué en C avec UDP (Command Line Instant Messenger)

**CLIM** est un système de chat de groupe décentralisé écrit en C utilisant UDP pour la communication inter-processus. Le système permet aux utilisateurs de créer des groupes de discussion, d'envoyer des messages, de gérer les utilisateurs (ban, suppression) et de fusionner des groupes.

##  Table des matières

- [Architecture](#architecture)
- [Composants](#composants)
- [Installation](#installation)
- [Utilisation](#utilisation)
- [Commandes](#commandes)
- [Structure des données](#structure-des-données)
- [Fonctionnalités](#fonctionnalités)
- [Gestion de la persistence](#gestion-de-la-persistence)

##  Architecture

CLIM est composé d'une architecture client-serveur avec des processus distribués:

```

                                                            
  ServeurISY (Port 8000)                                   
  └─ Gère les groupes et les commandes des clients       
                                                            
  
  GroupeISY #1 (Port 8100)   GroupeISY #2 (Port 8101)   
  └─ Broadcasts messages     └─ Broadcasts messages     
     aux clients                aux clients            
                                                             
  ClientISY (CLI Interface)                                
  ├─ Menu interactif                                      
  ├─ Envoie des commandes au serveur                     
  ├─ Crée un processus AffichageISY par groupe          
  └─ Affiche les messages reçus                         
                                                             
  AffichageISY (Display Process)                          
  └─ Écoute les messages du groupe                      
     et les affiche en temps réel                        
                                                             

```

### Communication

- **UDP Sockets**: Tous les échanges utilisent UDP sur localhost ou le réseau
- **ISYMessage**: Structure commune de message (164 bytes)
  - `ordre[4]`: Type de message (CMD, RPL, CON, MES, MGR)
  - `emetteur[20]`: Nom d'utilisateur
  - `emoji[8]`: Emoji Unicode généré automatiquement par IP
  - `groupe[32]`: Nom du groupe
  - `texte[100]`: Contenu ou commande

##  Composants

### 1. **ServeurISY** (Serveur principal)
- **Port**: 8000
- **Rôle**: Gère les groupes et traite les commandes
- **Fonctionnalités**:
  - Création et suppression de groupes
  - Gestion des utilisateurs (JOIN, LIST, DELETE)
  - Banning d'adresses IP
  - Fusion de groupes
  - Lancement des processus `GroupeISY`

### 2. **GroupeISY** (Processus groupe)
- **Port**: 8100 + numéro du groupe
- **Rôle**: Gère les messages et membres d'un groupe spécifique
- **Fonctionnalités**:
  - Enregistrement des clients (ORDRE_CON)
  - Broadcast des messages à tous les membres
  - Gestion locale du ban
  - Persistence des membres dans `infoGroup/*.txt`
  - Chargement des anciens membres au démarrage

### 3. **ClientISY** (Interface client)
- **Type**: CLI interactive
- **Rôle**: Interface utilisateur
- **Fonctionnalités**:
  - Menu de sélection des commandes
  - Communication avec le serveur
  - Lancement du processus `AffichageISY`
  - Monitoring de l'état de connexion

### 4. **AffichageISY** (Processus d'affichage)
- **Rôle**: Reçoit et affiche les messages
- **Fonctionnalités**:
  - Écoute sur le port assigné par ClientISY
  - Affichage formaté des messages
  - Détection du bannissement (VOUS_ETES_BANNI)
  - Notifications visuelles

##  Installation

### Prérequis

- kitty
- librairie ffmpeg (jouer un son)

### Compilation

```bash
cd /path/to/Projet1.0
make clean
make
```

Cela génère les binaires dans le dossier `bin/`:
- `bin/ServeurISY`
- `bin/GroupeISY`
- `bin/ClientISY`
- `bin/AffichageISY`

##  Utilisation

### Démarrage du serveur

```bash
./bin/ServeurISY
```

Le serveur affichera:
```
ServeurISY en écoute sur port 8000
[SERVER] Waiting for message on port 8000...
```

### Lancement d'un client

```bash
./bin/ClientISY
```

Cela ouvre un menu interactif:
```
[CLIENT] Bienvenue dans CLIM!
Configuré avec: username=jan, server_ip=10.148.111.54, display_port=9002

1. CREATE <group_name> - Créer un groupe
2. JOIN <group_name>   - Rejoindre un groupe
3. LIST                - Lister les groupes
4. MERGE <g1> <g2>     - Fusionner deux groupes
5. DELETE <group_name> - Supprimer un groupe
6. EXIT                - Quitter

Entrez votre commande: 
```

##  Commandes

### Commandes serveur (depuis ClientISY)

- list     : permet au modérateur de lister les membres de la discussion
- ban <IP> : permet au modérateur de bannir une membres de la discussion avec IP
- quit     : permet de quitter la discussion et de revenir au menu principal
### Commandes dans un groupe (après JOIN)

Une fois dans un groupe (via AffichageISY), vous pouvez:
- Taper des messages et appuyer sur Entrée pour envoyer
- Les messages s'affichent au format: `[groupe] 😀 username : message`

##  Structure des données

### Configuration

- **`config/client_template.conf`**: Configuration client
  ```
  username=jan
  server_ip=10.148.111.54
  display_port=9002
  ```

### Persistence

- **`infoGroup/<nom>.txt`**: Liste des membres
  ```
  alice:127.0.0.1:😀
  bob:127.0.0.2:😁
  charlie:127.0.0.3:😂
  ```

- **`infoGroup/<nom>_banned.txt`**: Liste des IPs bannies
  ```
  192.168.1.100
  10.0.0.5
  ```

- **`group_members.txt`**: Registre global des groupes
  ```
  GROUP:GroupA
  GROUP:GroupB
  GROUP:GroupC
  ```


##  Fonctionnalités

### Gestion des groupes

 **Création** - Créer un nouveau groupe (modérateur = créateur)
 **Suppression** - Supprimer un groupe (modérateur requis)
 **Fusion** - Fusionner deux groupes sans perte de données
 **Listing** - Afficher tous les groupes avec leurs ports

### Gestion des utilisateurs

 **Adhésion** - Rejoindre un groupe automatiquement
 **Listing** - Voir les membres d'un groupe avec emojis
 **Depart** - Quitter un groupe automatiquement

### Sécurité

 **Bannissement** - Bannir une IP (multi-niveaux: serveur, groupe, client)
 **Détection ban** - Notification immédiate au client
 **Force exit** - Sortie forcée du groupe si banni

### Emoji

 **Génération automatique** - Un emoji déterministe par IP
 **Plage Unicode** - U+1F600 à U+1F64F (48 emojis)
 **Consistance** - Même IP = toujours le même emoji

### Persistence

 **Sauvegarde des membres** - Liste dans `infoGroup/*.txt`
 **Chargement au démarrage** - Récupère les anciens membres
 **Fusion sans perte** - Préserve tous les membres après merge
 **Pas de doublons** - Même IP ne s'ajoute qu'une fois

##  Gestion de la persistence

### Après une fusion (MERGE GroupA GroupB)

1. **Lecture des fichiers**:
   - Lit `infoGroup/GroupB.txt` (groupe destination)
   - Lit `infoGroup/GroupA.txt` (groupe source)

2. **Fusion intelligente**:
   - Conserve tous les membres de GroupB
   - Ajoute les membres de GroupA qui n'existent pas (vérifié par IP)
   - Élimine les doublons d'IP

3. **Écriture**:
   - Écrit le résultat dans `infoGroup/GroupB.txt`
   - Supprime `infoGroup/GroupA.txt`

4. **Chargement**:
   - Quand GroupeISY redémarre, `load_group_file_into_memory()` recharge les membres
   - Aucune perte de données même après redémarrage

### Éviter les doublons

 **Déduplication par IP**: Même IP ne peut qu'une fois dans `clients[]`
 **Vérification avant ajout**: `add_client()` vérifie si l'IP existe déjà
 **Mise à jour du profil**: Si re-connexion, met à jour nom/port uniquement


##  Exemple de session

```bash
# Terminal 1: Serveur
$ ./bin/ServeurISY
ServeurISY en écoute sur port 8000

# Terminal 2: Client Alice
$ ./bin/ClientISY
> CREATE GroupA
[SERVER] Groupe GroupA cree sur port 8100
> JOIN GroupA
[SERVER] OK 8100
[AffichageISY] En écoute sur port 9002

# Terminal 3: Client Bob
$ ./bin/ClientISY
> JOIN GroupA
[SERVER] OK 8100
[AffichageISY] En écoute sur port 9003

# Terminal 2: Alice tape dans GroupA
Coucou tout le monde!
[GroupA] 😀 alice : Coucou tout le monde!
[GroupA] 😁 bob : Salut alice!

# Terminal 2: Alice crée GroupB
> CREATE GroupB
> JOIN GroupB

# Terminal 2: Alice fusionne les groupes
> MERGE GroupA GroupB
[SERVER] Groupe GroupA fusionne dans GroupB (port 8101). Tous les membres sont maintenant dans GroupB.

# Terminal 2: Alice rejoint GroupB et vérifie les membres
> JOIN GroupB
> LISTMEMBER GroupB
alice:127.0.0.1:😀
bob:127.0.0.1:😁  <- Bob transféré automatiquement!
```

##  Gestion des bugs connus

### Doublons d'adhésion (RÉSOLU)
**Problème**: Rejoindre plusieurs fois ajoutait plusieurs entrées
**Solution**: Vérification d'IP avant ajout dans `add_client()`

### Perte de données après fusion (RÉSOLU)
**Problème**: Après MERGE, rejoindre écrasait les anciens membres
**Solution**: `load_group_file_into_memory()` au démarrage de GroupeISY


##  Licences et crédits

Développé comme projet ISEN - Alternance Systèmes Explicables

---

**Dernière mise à jour**: Décembre 2025
**Version**: 1.0
**Stabilité**: Production (avec déduplication et fusion corrigées)

