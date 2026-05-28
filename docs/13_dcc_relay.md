# Phase 13 — Le Relais DCC (DCC Relay)

> **Pré-requis** : avoir lu [10_messagerie.md](10_messagerie.md) et [03_boucle_evenementielle.md](03_boucle_evenementielle.md)  
> **Fichiers concernés** : [includes/DccRelay.hpp](../includes/DccRelay.hpp), [srcs/DccRelay.cpp](../srcs/DccRelay.cpp), [srcs/commands/cmd_privmsg.cpp](../srcs/commands/cmd_privmsg.cpp)

---

## 1. Concept de base — Qu'est-ce que DCC ?

**DCC** (Direct Client-to-Client) est un sous-protocole IRC qui permet à deux utilisateurs d'établir une **connexion directe** pour transférer des fichiers, sans passer par le serveur.

Sans DCC :
```
alice ──PRIVMSG──► Serveur ──► bob
(toujours via le serveur)
```

Avec DCC SEND :
```
alice ──── connexion TCP directe ────► bob
(peer-to-peer, le serveur n'est qu'intermédiaire pour l'établissement)
```

**Le problème** : dans un réseau privé (NAT), l'adresse IP d'alice est peut-être `192.168.1.5` — une adresse **non routable** depuis l'extérieur. Bob ne peut pas se connecter directement à cette IP.

**La solution** : le serveur intercepte le message DCC SEND, remplace l'IP d'alice par sa **propre IP publique**, et **relaie** les données entre alice et bob.

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **DCC** | Direct Client-to-Client — protocole pour connexions directes entre clients IRC. |
| **DCC SEND** | Sous-commande DCC pour initier un transfert de fichier. |
| **CTCP** | Client-To-Client Protocol — messages spéciaux encapsulés dans PRIVMSG avec `\x01` comme délimiteur. |
| **NAT** | Network Address Translation — mécanisme réseau qui masque les adresses IP privées. |
| **Relay / Relais** | Le serveur se positionne comme intermédiaire pour retransmettre les données byte par byte. |
| **`DccSession`** | Structure représentant une session de relai en cours (avec ses fds et son état). |
| **`listen_fd`** | Socket d'écoute créé par le relai — c'est là que le destinataire se connecte. |
| **`recv_fd`** | Socket de connexion avec le destinataire (bob). |
| **`send_fd`** | Socket de connexion avec l'expéditeur (alice). |
| **Phase** | État de la session : WAITING_RECEIVER → CONNECTING_SENDER → RELAYING → DONE |
| **`intercept()`** | Méthode qui intercepte un DCC SEND et retourne un message modifié. |
| **`handleFd()`** | Méthode appelée par `poll()` quand un fd de relai a de l'activité. |

---

## 3. Le format d'un message DCC SEND

Un DCC SEND est un PRIVMSG avec un contenu CTCP spécial :

```
PRIVMSG bob :\x01DCC SEND monimage.jpg 3232235778 5001 204800\x01
                │                       │           │    │
                │                       │           │    └── taille en octets
                │                       │           └─────── port d'écoute
                │                       └─────────────────── IP en entier (host byte order)
                └─────────────────────────────────────────── "DCC SEND "
```

L'IP `3232235778` en décimal équivaut à `192.168.1.2` en notation point.

La **conversion** : 192×16777216 + 168×65536 + 1×256 + 2 = 3232235778.

---

## 4. Objectif attendu dans ce projet

Le `DccRelay` doit :
1. Détecter un message DCC SEND dans `cmd_privmsg()`.
2. Créer un socket d'écoute sur un port libre.
3. Remplacer l'IP/port original par l'IP/port du relai dans le message envoyé à bob.
4. Quand bob se connecte au relai, se connecter à l'IP/port original d'alice.
5. Relayer les données dans les deux sens (handshake + transfert).

---

## 5. Pourquoi ces objectifs ?

### Pourquoi relayer plutôt que laisser la connexion directe ?

DCC SEND was designed for networks where clients have public IPs. Dans un environnement avec NAT (machines derrière une box internet, réseau étudiant 42, VMs), les clients n'ont que des IPs privées. Le relai est la **seule solution** pour que le transfert fonctionne.

### Pourquoi stocker `orig_ip` et `orig_port` ?

Quand bob se connecte au port du relai, le relai doit se connecter à **l'adresse originale d'alice** pour récupérer les données. Il doit donc mémoriser où était le serveur d'alice avant la modification.

### Pourquoi 4 phases ?

Le relai est **asynchrone** (non-bloquant). Les connexions ne se font pas instantanément. Les phases permettent au code de savoir où en est l'établissement de la session :
1. `WAITING_RECEIVER` : le socket d'écoute est ouvert, on attend que bob se connecte.
2. `CONNECTING_SENDER` : bob est connecté, on initie la connexion vers alice (peut prendre du temps).
3. `RELAYING` : les deux connexions sont établies, on transmet les données.
4. `DONE` : le transfert est terminé, on nettoie.

---

## 6. Implémentation — Code détaillé

### 6.1 — `parseDccSend()` et `buildDccSend()` : lire et construire le CTCP

Fichier : [srcs/DccRelay.cpp](../srcs/DccRelay.cpp)

```cpp
// Analyser : "\x01DCC SEND fichier ip port taille\x01"
bool parseDccSend(const std::string &ctcp,
    std::string &filename, uint32_t &ip, uint16_t &port, size_t &filesize)
{
    if (ctcp.size() < 2 || ctcp[0] != '\x01')
        return false;

    // Enlever les délimiteurs \x01
    std::string body = ctcp.substr(1);
    if (!body.empty() && body.back() == '\x01')
        body.erase(body.size() - 1);

    if (body.substr(0, 9) != "DCC SEND ")
        return false;
    body = body.substr(9); // "fichier.zip 3232235778 5001 204800"

    // Découper en tokens par espace
    std::vector<std::string> toks;
    // ... tokenisation ...
    // toks[0] = filename, toks[1] = ip, toks[2] = port, toks[3] = filesize

    filename = toks[0];
    ip       = static_cast<uint32_t>(std::strtoul(toks[1].c_str(), NULL, 10));
    port     = static_cast<uint16_t>(std::strtoul(toks[2].c_str(), NULL, 10));
    filesize = static_cast<size_t>(  std::strtoul(toks[3].c_str(), NULL, 10));
    return true;
}

// Reconstruire le message DCC SEND avec le nouvel IP/port (celui du relai)
std::string buildDccSend(const std::string &filename,
    uint32_t ip, uint16_t port, size_t filesize)
{
    std::ostringstream oss;
    oss << '\x01' << "DCC SEND " << filename << ' '
        << static_cast<unsigned long>(ip) << ' '
        << static_cast<unsigned>(port) << ' '
        << filesize << '\x01';
    return oss.str();
    // Ex: "\x01DCC SEND fichier.zip 2130706433 9001 204800\x01"
    //                                 │              │
    //                                 └─ 127.0.0.1   └─ port du relai
}
```

### 6.2 — `DccRelay::intercept()` : cœur du mécanisme d'interception

```cpp
std::string DccRelay::intercept(Client &/*sender*/, Client &/*target*/,
    const std::string &ctcpText)
{
    std::string filename;
    uint32_t orig_ip;
    uint16_t orig_port;
    size_t   filesize;

    // Analyser le DCC SEND original
    if (!parseDccSend(ctcpText, filename, orig_ip, orig_port, filesize))
        return "";  // pas un DCC SEND valide → laisser passer sans modification

    // Créer un socket d'écoute sur un port libre (port=0 → le noyau choisit)
    uint16_t relay_port = 0;
    int lfd = openRelayListen(relay_port);  // bind sur 0.0.0.0:0, listen
    if (lfd < 0)
        return "";

    // Créer la session et mémoriser l'adresse originale d'alice
    DccSession s;
    s.listen_fd = lfd;
    s.orig_ip   = orig_ip;    // IP d'alice (adresse originale)
    s.orig_port = orig_port;  // port d'alice (port original)
    _sessions.push_back(s);

    // Enregistrer listen_fd dans poll() pour détecter la connexion de bob
    _srv.addPolledFd(lfd, POLLIN);

    // L'IP à annoncer à bob : l'IP publique du serveur (ou 127.0.0.1 par défaut)
    uint32_t adv_ip = _server_ip ? _server_ip : ((127u << 24) | 1u);

    // Retourner le DCC SEND modifié : bob se connectera au serveur relai
    return buildDccSend(filename, adv_ip, relay_port, filesize);
    // Alice avait dit "connecte-toi à 192.168.1.2:5001"
    // On dit maintenant "connecte-toi à 127.0.0.1:9001 (notre relai)"
}
```

### 6.3 — Interception dans `cmd_privmsg()`

Fichier : [srcs/commands/cmd_privmsg.cpp](../srcs/commands/cmd_privmsg.cpp)

```cpp
// Dans la branche "user message" de cmd_privmsg()
if (isDccSend(text) && srv.getDccRelay()) {
    std::string relayed = srv.getDccRelay()->intercept(cli, *dest, text);
    if (!relayed.empty()) {
        // On envoie le DCC SEND MODIFIÉ (avec IP/port du relai)
        std::string fullMsg = ":" + cli.getPrefix() + " PRIVMSG "
                            + target + " :" + relayed + "\r\n";
        srv.sendTo(*dest, fullMsg);
        return;  // ne pas envoyer le message original
    }
}
// Si interception échouée, envoyer le message original (sans relai)
```

### 6.4 — Phase 1 : `WAITING_RECEIVER` → bob se connecte

```cpp
void DccRelay::handleListenFd(DccSession &s)
{
    struct sockaddr_in ca;
    socklen_t len = sizeof(ca);

    // Accepter la connexion de bob (le destinataire du fichier)
    int rfd = accept(s.listen_fd, reinterpret_cast<struct sockaddr *>(&ca), &len);
    if (rfd < 0)
        return;
    fcntl(rfd, F_SETFL, O_NONBLOCK);

    // Le socket d'écoute n'est plus utile : fermer et remplacer par recv_fd
    _srv.removePolledFd(s.listen_fd);
    close(s.listen_fd);
    s.listen_fd = -1;
    s.recv_fd   = rfd;
    s.phase     = DccSession::CONNECTING_SENDER;  // → phase 2
    _srv.addPolledFd(rfd, POLLIN);

    // Initier la connexion vers alice (l'adresse originale)
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sfd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in dest;
    dest.sin_family      = AF_INET;
    dest.sin_addr.s_addr = htonl(s.orig_ip);   // IP originale d'alice
    dest.sin_port        = htons(s.orig_port);  // port original d'alice

    // connect() non-bloquant : peut retourner EINPROGRESS
    int ret = connect(sfd, reinterpret_cast<struct sockaddr *>(&dest), sizeof(dest));
    if (ret < 0 && errno != EINPROGRESS) {
        close(sfd);
        removeSession(&s);
        return;
    }
    s.send_fd = sfd;
    _srv.addPolledFd(sfd, POLLIN | POLLOUT);
}
```

### 6.5 — Phase 2 : `CONNECTING_SENDER` → connexion vers alice établie

```cpp
void DccRelay::handleSendFd(DccSession &s, short revents)
{
    // Vérifier si la connexion asynchrone vers alice est établie
    if (s.phase == DccSession::CONNECTING_SENDER) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(s.send_fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            removeSession(&s);
            return;
        }
        s.phase = DccSession::RELAYING;  // → phase 3
    }
    // suite : phase RELAYING ...
```

### 6.6 — Phase 3 : `RELAYING` → transmission bidirectionnelle

```cpp
    // ── Données du recv_buf (venu de bob) vers alice ─────────────────────
    if ((revents & POLLOUT) && !s.recv_buf.empty()) {
        ssize_t n = send(s.send_fd, s.recv_buf.c_str(), s.recv_buf.size(),
                         MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0)
            s.recv_buf.erase(0, static_cast<size_t>(n));
        // recv_buf stocke les acquittements DCC envoyés par bob → alice
    }

    // ── Lire les données venant d'alice (le fichier) ─────────────────────
    if (revents & POLLIN) {
        char buf[8192];
        ssize_t n = recv(s.send_fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            removeSession(&s);
            return;
        }
        s.send_buf.append(buf, static_cast<size_t>(n));
        // Ces données seront écrites vers bob par handleRecvFd()
    }
}

void DccRelay::handleRecvFd(DccSession &s, short revents)
{
    // ── Lire les acquittements envoyés par bob ───────────────────────────
    if (revents & POLLIN) {
        char buf[8192];
        ssize_t n = recv(s.recv_fd, buf, sizeof(buf), 0);
        if (n <= 0) { removeSession(&s); return; }
        s.recv_buf.append(buf, static_cast<size_t>(n));
        if (s.send_fd >= 0)
            _srv.addPolledFd(s.send_fd, POLLIN | POLLOUT);  // prêt à écrire vers alice
    }
    // ── Écrire les données du fichier vers bob ───────────────────────────
    if ((revents & POLLOUT) && !s.send_buf.empty()) {
        ssize_t n = send(s.recv_fd, s.send_buf.c_str(), s.send_buf.size(),
                         MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0)
            s.send_buf.erase(0, static_cast<size_t>(n));
    }
}
```

---

## 7. Schéma complet du flux DCC Relay

```
alice tape : /dcc send bob fichier.zip

ÉTAPE 1 : alice envoie le DCC SEND
──────────────────────────────────
alice → Serveur TCP : 
  "PRIVMSG bob :\x01DCC SEND fichier.zip 3232235778 5001 204800\x01\r\n"
                                         │           │
                                         └─ alice_IP └─ alice écoute sur ce port

ÉTAPE 2 : interception et modification
──────────────────────────────────────
cmd_privmsg() → isDccSend() → true
               → srv.getDccRelay()->intercept(alice, bob, ctcpText)
               → parseDccSend() : orig_ip=192.168.1.2, orig_port=5001
               → openRelayListen(relay_port) : bind sur 0.0.0.0:9001
               → DccSession créée, listen_fd dans poll()
               → buildDccSend("fichier.zip", 127.0.0.1, 9001, 204800)

Serveur → bob TCP :
  "PRIVMSG bob :\x01DCC SEND fichier.zip 2130706433 9001 204800\x01\r\n"
                                         │           │
                                         └─ serveur  └─ port du relai

ÉTAPE 3 : bob accepte et se connecte au relai
─────────────────────────────────────────────
bob (irssi) → connexion TCP sur le serveur port 9001
  → handleListenFd() : accept() → recv_fd = bob_fd
  → close listen_fd
  → connect() vers 192.168.1.2:5001 (alice originale) → send_fd

ÉTAPE 4 : alice accepte la connexion du relai
─────────────────────────────────────────────
alice (irssi) côté : accept() d'une connexion venant du serveur relai
  (alice pense que c'est bob qui se connecte directement)

ÉTAPE 5 : relai bidirectionnel
──────────────────────────────
alice → send_fd (relai) → send_buf → recv_fd → bob
  (le fichier)

bob → recv_fd (relai) → recv_buf → send_fd → alice
  (acquittements DCC)

poll() surveille : recv_fd (POLLIN|POLLOUT) + send_fd (POLLIN|POLLOUT)
handleRecvFd() et handleSendFd() shuttlent les données

ÉTAPE 6 : fin de transfert
───────────────────────────
alice ferme la connexion → recv() retourne 0 → removeSession()
Tous les fds fermés, session supprimée.
```

---

## 8. Récapitulatif

| Phase | Déclencheur | Action |
|-------|-------------|--------|
| `WAITING_RECEIVER` | alice envoie DCC SEND | Créer listen_fd, modifier IP/port, attendre bob |
| `CONNECTING_SENDER` | bob se connecte au relai | Accept bob, se connecter vers alice |
| `RELAYING` | connexion vers alice établie | Transfert bidirectionnel de données |
| `DONE` | déconnexion de l'un ou l'autre | Fermer les fds, supprimer la session |

- `intercept()` **substitue** l'IP/port d'alice par celui du relai dans le message envoyé à bob.
- Le relai maintient **deux connexions TCP** : une vers bob (`recv_fd`) et une vers alice (`send_fd`).
- Tout est **non-bloquant** et intégré dans la boucle `poll()` principale du serveur.
- `recv_buf` et `send_buf` permettent de gérer les **décalages de débit** entre les deux côtés.

---

## Conclusion du manuel

Vous avez maintenant parcouru l'intégralité du projet `ft_irc` :

| Fichier | Contenu |
|---------|---------|
| [00_vue_ensemble.md](00_vue_ensemble.md) | Architecture globale, fichiers, cycle de vie |
| [01_reseau_et_sockets.md](01_reseau_et_sockets.md) | TCP/IP, socket, fd, bind/listen/accept |
| [02_demarrage_serveur.md](02_demarrage_serveur.md) | main.cpp, Server::init(), signaux |
| [03_boucle_evenementielle.md](03_boucle_evenementielle.md) | poll(), POLLIN/POLLOUT, run() |
| [04_gestion_clients.md](04_gestion_clients.md) | Client, états, buffers |
| [05_parsing_messages.md](05_parsing_messages.md) | extractLine(), tokenize(), Message |
| [06_dispatch_commandes.md](06_dispatch_commandes.md) | CommandDispatcher, fonction pointeurs |
| [07_registration.md](07_registration.md) | PASS/NICK/USER/CAP, tryRegister() |
| [08_canaux.md](08_canaux.md) | Channel, JOIN/PART/TOPIC/KICK/INVITE |
| [09_modes_irc.md](09_modes_irc.md) | MODE, +i +t +k +l +o |
| [10_messagerie.md](10_messagerie.md) | PRIVMSG, NOTICE, broadcast |
| [11_reponses_numeriques.md](11_reponses_numeriques.md) | Reply.hpp, tous les codes numériques |
| [12_bot.md](12_bot.md) | Bot, socketpair, !commandes |
| [13_dcc_relay.md](13_dcc_relay.md) | DCC SEND, relai 4 phases |

---

## Moyen de vérifier que ça marche

> Le DCC relay est difficile à tester sans vrai client. Voici des approches progressives.

### Vérification 1 — Le relai ne casse pas le serveur au démarrage

```bash
./ircserv 6667 secret
# Le serveur démarre sans erreur même avec le DCC relay initialisé
# (pas de crash, pas de message d'erreur DCC au démarrage)
```

### Vérification 2 — Tester l'interception avec netcat manuellement

```bash
# Simuler un DCC SEND brut (le \x01 est le délimiteur CTCP)
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
JOIN #test

# Depuis une autre session (bob) :
PASS secret
NICK bob
USER bob 0 * :Bob
JOIN #test

# Alice envoie un faux DCC SEND à bob :
PRIVMSG bob :\x01DCC SEND test.txt 2130706433 5001 1024\x01
# 2130706433 = 127.0.0.1 en décimal

# Bob devrait recevoir un message DCC SEND avec l'IP/port du relai
# (pas l'IP originale 127.0.0.1:5001)
```

### Vérification 3 — Vérifier les ports ouverts pendant le relai

```bash
# Pendant qu'une session DCC est en cours :
ss -tlnp | grep ircserv
# On doit voir un port supplémentaire ouvert (le port du relai)
# En plus du 6667, un port dynamique genre 9001
```

### Vérification 4 — Test complet avec irssi (DCC send réel)

```bash
# Terminal 1 — alice :
irssi -c 127.0.0.1 -p 6667 -w secret -n alice

# Terminal 2 — bob :
irssi -c 127.0.0.1 -p 6667 -w secret -n bob

# Dans irssi alice :
/join #test
/dcc send bob /etc/hostname   # envoyer un fichier à bob

# Dans irssi bob :
/dcc get alice   # accepter le fichier
# Le fichier doit arriver complet via le relai du serveur
```
