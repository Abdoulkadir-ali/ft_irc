# Phase 00 — Vue d'ensemble du projet ft_irc

> **Niveau supposé** : aucun pré-requis. On repart de zéro.

---

## 1. Concept de base — Qu'est-ce qu'IRC ?

**IRC** signifie *Internet Relay Chat*. C'est un protocole de messagerie en temps réel inventé en 1988, qui permet à des milliers de personnes de discuter simultanément en passant par un **serveur central**.

Imagine un walkie-talkie géant :
- Chaque personne connectée s'appelle un **client**.
- Le **serveur** reçoit les messages et les redistribue.
- Les discussions se font dans des **salons** appelés **channels** (ex : `#general`).

```
Client A ──┐
Client B ──┼──► SERVEUR IRC ──► redistribue les messages aux bons destinataires
Client C ──┘
```

Avant les applications modernes comme Discord ou Slack, IRC était **LE** moyen de communiquer en temps réel sur internet. Il est encore utilisé aujourd'hui par des communautés open-source (ex : les développeurs du noyau Linux utilisaient IRC).

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Protocole** | Un ensemble de règles que deux machines respectent pour se comprendre. IRC est un protocole texte : les messages sont des lignes de texte. |
| **Client** | Un programme (ou personne) qui se connecte au serveur IRC (ex : irssi, WeeChat, HexChat). |
| **Serveur** | Le programme central qui gère toutes les connexions et redistribue les messages. C'est ce que vous avez codé. |
| **Channel** | Un salon de discussion (commence par `#` ou `&`, ex : `#general`). |
| **Nick** | Le pseudonyme d'un utilisateur (ex : `alice`, `Bob42`). |
| **Opérateur (op)** | Un utilisateur privilégié dans un channel, capable de gérer les membres. |
| **RFC 2812** | Le document officiel qui décrit précisément comment fonctionne le protocole IRC. Votre serveur doit s'y conformer. |
| **Socket** | Le "câble virtuel" entre le client et le serveur sur le réseau. |
| **Port** | Un numéro (1024–65535) qui identifie quelle application reçoit les connexions réseau. |
| **File descriptor (fd)** | Un numéro entier que le système d'exploitation donne à chaque fichier/socket ouvert. C'est comme un ticket de vestiaire. |

---

## 3. Objectif attendu du projet

Écrire en **C++98** un serveur IRC fonctionnel capable de :

1. Accepter plusieurs connexions simultanées de clients IRC.
2. Gérer l'authentification (`PASS`, `NICK`, `USER`).
3. Gérer des channels avec toutes leurs règles (modes, opérateurs, invitations...).
4. Transmettre les messages privés et les messages de canaux.
5. Répondre aux commandes standard IRC : `JOIN`, `PART`, `KICK`, `TOPIC`, `MODE`, `PRIVMSG`, etc.
6. Être compatible avec un vrai client IRC (comme **irssi**).

**Bonus** : Un bot intégré et un relais de transfert de fichiers DCC.

---

## 4. Pourquoi cet objectif ?

### Pourquoi IRC et pas HTTP ?

IRC est plus simple à implémenter qu'HTTP, mais il couvre tous les concepts fondamentaux des réseaux :
- Connexions persistantes (contrairement au HTTP classique qui ferme après chaque réponse)
- Multiplexage de nombreux clients simultanés
- Protocole texte lisible et débogable facilement

### Pourquoi C++98 sans threads ?

La contrainte de **ne pas utiliser de threads** est volontaire : elle force à comprendre comment un seul thread peut gérer des milliers de connexions simultanées via l'**I/O non-bloquant** (voir Phase 03). C'est la technique utilisée par des serveurs comme Nginx ou Node.js.

### Pourquoi est-ce formateur ?

Ce projet enseigne :
- La programmation réseau bas-niveau (sockets POSIX)
- La gestion mémoire manuelle (C++)
- L'architecture d'un serveur event-driven
- Le respect d'un protocole industriel (RFC 2812)

---

## 5. Architecture générale du projet

```
ft_irc/
├── srcs/main.cpp              ← Point d'entrée : parse les args, lance le serveur
├── srcs/Server.cpp            ← Le cœur : boucle événementielle, gestion I/O
├── srcs/Client.cpp            ← Représente une connexion utilisateur
├── srcs/Channel.cpp           ← Représente un salon de discussion
├── srcs/Parser.cpp            ← Décode les messages IRC bruts
├── srcs/CommandDispatcher.cpp ← Route les commandes vers les bonnes fonctions
├── srcs/Reply.cpp             ← Génère les réponses numériques IRC (001, 433, etc.)
├── srcs/Utils.cpp             ← Fonctions utilitaires (toLower, isValidNick, etc.)
├── srcs/Bot.cpp               ← [BONUS] Bot IRC intégré
├── srcs/DccRelay.cpp          ← [BONUS] Relais transfert de fichiers
└── srcs/commands/
    ├── cmd_pass.cpp           ← Commande PASS
    ├── cmd_nick.cpp           ← Commande NICK
    ├── cmd_user.cpp           ← Commande USER
    ├── cmd_join.cpp           ← Commande JOIN
    ├── cmd_part.cpp           ← Commande PART
    ├── cmd_mode.cpp           ← Commande MODE
    ├── cmd_kick.cpp           ← Commande KICK
    ├── cmd_topic.cpp          ← Commande TOPIC
    ├── cmd_invite.cpp         ← Commande INVITE
    ├── cmd_privmsg.cpp        ← Commandes PRIVMSG + NOTICE
    ├── cmd_ping.cpp           ← Commande PING
    ├── cmd_quit.cpp           ← Commande QUIT
    ├── cmd_whois.cpp          ← Commande WHOIS
    └── cmd_cap.cpp            ← Commande CAP (négociation IRCv3)
```

### Relations entre les classes principales

```
Server (propriétaire central)
├── map<fd, Client*>     ← tous les utilisateurs connectés
├── map<nom, Channel*>   ← tous les salons actifs
├── CommandDispatcher    ← registre des commandes IRC
├── Bot*                 ← [BONUS] le bot intégré
└── DccRelay*            ← [BONUS] le relais de fichiers

Client
├── int _fd              ← son socket réseau
├── State _state         ← phase d'authentification
└── set<string> _channels← canaux rejoints

Channel
├── map<fd, Client*> _members  ← membres présents
├── set<int> _operators        ← qui est opérateur
└── bool/string _modes         ← modes actifs (+i, +t, +k, etc.)
```

---

## 6. Cycle de vie d'un message IRC

Voici ce qui se passe quand `alice` tape `!ping` dans irssi :

```
1. irssi envoie sur le réseau : "PRIVMSG #general :!ping\r\n"
2. Le noyau Linux dépose ces octets dans le buffer du socket d'alice
3. poll() détecte que le socket d'alice est lisible (POLLIN)
4. handleRead() lit les octets dans _read_buf d'alice
5. Parser::extractLine() extrait la ligne complète
6. Parser::tokenize() la transforme en Message { command: "PRIVMSG", params: ["#general", "!ping"] }
7. CommandDispatcher::dispatch() appelle cmd_privmsg()
8. cmd_privmsg() envoie le message à tous les membres de #general
9. Le Bot reçoit aussi le message, détecte "!ping", répond "PONG!"
10. irssi de bob reçoit la réponse et l'affiche
```

---

## 7. Plan de lecture du manuel

| Phase | Fichier | Ce que vous apprendrez |
|-------|---------|------------------------|
| 01 | [01_reseau_et_sockets.md](01_reseau_et_sockets.md) | TCP, sockets, file descriptors |
| 02 | [02_demarrage_serveur.md](02_demarrage_serveur.md) | Comment le serveur démarre et écoute |
| 03 | [03_boucle_evenementielle.md](03_boucle_evenementielle.md) | `poll()`, I/O non-bloquant |
| 04 | [04_gestion_clients.md](04_gestion_clients.md) | La classe `Client`, états, buffers |
| 05 | [05_parsing_messages.md](05_parsing_messages.md) | Protocole IRC, `Parser`, `struct Message` |
| 06 | [06_dispatch_commandes.md](06_dispatch_commandes.md) | `CommandDispatcher`, comment les commandes sont routées |
| 07 | [07_registration.md](07_registration.md) | `PASS`/`NICK`/`USER`, la séquence d'authentification |
| 08 | [08_canaux.md](08_canaux.md) | `Channel`, `JOIN`/`PART`/`TOPIC`/`KICK`/`INVITE` |
| 09 | [09_modes_irc.md](09_modes_irc.md) | Les modes `+i` `+t` `+k` `+l` `+o` |
| 10 | [10_messagerie.md](10_messagerie.md) | `PRIVMSG`/`NOTICE`, broadcast |
| 11 | [11_reponses_numeriques.md](11_reponses_numeriques.md) | Codes numériques RFC 2812 |
| 12 | [12_bot.md](12_bot.md) | Le bot bonus, `socketpair` |
| 13 | [13_dcc_relay.md](13_dcc_relay.md) | Le relais DCC, transfert de fichiers |

---

## 8. Récapitulatif

- **IRC** = protocole texte de messagerie en temps réel, fondé sur un serveur central.
- Ce projet implémente un serveur IRC **sans threads**, en **C++98**, avec `poll()` pour gérer de nombreuses connexions.
- L'architecture est centralisée : `Server` possède tout, les `Client` représentent les connexions, les `Channel` représentent les salons.
- Commencez par la Phase 01 pour comprendre les fondations réseau avant d'attaquer le code.

---

## 9. Moyen de vérifier que ça marche

> **Outil principal** : `nc` (netcat) — envoie du texte brut sur un socket TCP, exactement comme un vrai client IRC mais sans interface graphique.

### Vérification 1 — Le serveur compile et démarre

```bash
make
./ircserv 6667 secret
# Attendu : [ircserv] listening on port 6667
```

### Vérification 2 — Un client peut se connecter

```bash
# Dans un second terminal
nc 127.0.0.1 6667
```

Si la connexion s'établit sans erreur → la phase 01 (socket, bind, listen, accept) fonctionne.

### Vérification 3 — S'authentifier et recevoir le message de bienvenue

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
```

Réponse attendue :
```
:ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1
:ircserv 002 alice :Your host is ircserv, running version 1.0
:ircserv 003 alice :This server was created ...
:ircserv 004 alice :ircserv 1.0 o itkol
```

### Vérification 4 — Deux clients voient les messages du channel

Terminal 1 :
```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
JOIN #test
```

Terminal 2 :
```bash
nc 127.0.0.1 6667
PASS secret
NICK bob
USER bob 0 * :Bob
JOIN #test
PRIVMSG #test :Bonjour !
```

Terminal 1 doit recevoir `:bob!bob@127.0.0.1 PRIVMSG #test :Bonjour !`

### Astuce : envoyer plusieurs commandes d'un coup

```bash
printf 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\nJOIN #test\r\n' | nc 127.0.0.1 6667
```

Utile pour tester rapidement sans taper à la main.
