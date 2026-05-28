# Phase 03 — La Boucle Événementielle

> **Pré-requis** : avoir lu [02_demarrage_serveur.md](02_demarrage_serveur.md)  
> **Fichiers concernés** : [srcs/Server.cpp](../srcs/Server.cpp) méthodes `run()`, `handleRead()`, `handleWrite()`, `consumeCommands()`, `disconnectClient()`, `reapDisconnected()`

---

## 1. Concept de base — Le problème des connexions multiples

Imaginez un standardiste téléphonique. Comment gérer 100 appels simultanément avec **une seule personne** ?

**Approche naïve** (bloquante) : écouter Alice jusqu'à ce qu'elle raccroche, puis passer à Bob. Résultat : Bob attend des heures.

**Bonne approche** (événementielle) : regarder régulièrement si quelqu'un a quelque chose à dire, traiter ce qui est prêt, revenir à la surveillance. C'est exactement ce que fait `poll()`.

### Qu'est-ce qu'une boucle événementielle ?

C'est une boucle infinie qui :
1. **Attend** qu'un événement se produise (quelqu'un envoie des données, quelqu'un se connecte...)
2. **Identifie** quel(s) socket(s) sont concernés
3. **Traite** chaque événement
4. **Recommence**

```
loop:
  ATTENDRE qu'un événement survienne  ← poll() bloque ici
  POUR CHAQUE socket qui a un événement:
    SI c'est une nouvelle connexion → accepter
    SI c'est une donnée à lire      → lire et traiter
    SI c'est prêt à écrire          → envoyer le buffer
  FIN POUR
  NETTOYER les déconnectés
  goto loop
```

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **poll()** | Appel système qui surveille un tableau de file descriptors et retourne quand l'un d'eux a une activité. Le serveur passe tout son temps dans cet appel. |
| **struct pollfd** | Structure C décrivant un fd à surveiller : `{ int fd; short events; short revents; }` |
| **events** | Masque de bits des événements à surveiller : `POLLIN` (données à lire), `POLLOUT` (prêt à écrire) |
| **revents** | Masque de bits des événements qui se sont **réellement** produits (rempli par poll()) |
| **POLLIN** | "Il y a des données à lire sur ce fd" — ou une nouvelle connexion sur le fd d'écoute |
| **POLLOUT** | "Ce fd est prêt à recevoir des données" — le buffer d'envoi du noyau a de la place |
| **POLLERR** | Une erreur s'est produite sur ce fd |
| **POLLHUP** | La connexion a été fermée par l'autre bout |
| **POLLNVAL** | Le fd n'est pas valide |
| **Event-driven** | Architecture où le code réagit à des événements plutôt que d'interroger en boucle (polling actif = gaspillage CPU) |
| **`_dead_fds`** | Ensemble de fds marqués pour déconnexion. On les supprime APRÈS la boucle de poll() pour ne pas invalider les itérateurs. |
| **`_fd_to_pfd_idx`** | Map `fd → index dans _pfds`. Permet de trouver et modifier l'entrée d'un client dans le tableau poll en O(1). |

---

## 3. Objectif attendu dans ce projet

La boucle événementielle doit :
1. Surveiller **tous les sockets** simultanément (clients, fd d'écoute, bot, DCC).
2. Réagir aux **nouvelles connexions** (`accept()`).
3. Réagir aux **données entrantes** (`recv()` + parsing + dispatch).
4. Réagir aux **opportunités d'envoi** (`send()` depuis le write_buf).
5. Gérer les **déconnexions** proprement sans crasher en milieu de boucle.

---

## 4. Pourquoi ces objectifs ?

### Pourquoi `poll()` plutôt que `select()` ou `epoll()` ?

- `select()` : limite de 1024 fds par défaut, interface moins pratique.
- `epoll()` : plus efficace pour des milliers de connexions, mais Linux uniquement et plus complexe.
- `poll()` : pas de limite arbitraire de fds, interface claire, portable, suffisant pour ce projet.

### Pourquoi activer POLLOUT seulement quand nécessaire ?

- **POLLIN** = attendre qu'on sonne à ta porte. Tu dors jusqu'à ce que ça sonne → `poll()` met le processus en sommeil → **0% CPU**.
- **POLLOUT** = vérifier si tu peux parler. La réponse est presque toujours "oui" → `poll()` retourne immédiatement → boucle infinie → **100% CPU**.

On active donc POLLOUT **uniquement quand le `write_buf` contient des données à envoyer**, et on le désactive dès que le buffer est vide.

### Pourquoi la liste `_dead_fds` et `reapDisconnected()` ?

Si on supprime un client **pendant** la boucle `for (i = 0; i < _pfds.size(); ++i)`, deux problèmes :
1. On invalide le tableau `_pfds` qu'on est en train d'itérer.
2. On peut avoir deux événements pour le même fd dans la même passe.

La solution : **marquer** pour suppression pendant la boucle, **supprimer réellement** après.

---

## 5. Implémentation — Code détaillé

### 5.1 — Structure `pollfd` et le tableau `_pfds`

Déclaration dans [includes/Server.hpp](../includes/Server.hpp) :

```cpp
std::vector<pollfd>   _pfds;           // tableau à passer à poll()
std::map<int, size_t> _fd_to_pfd_idx; // fd → index dans _pfds
```

Chaque entrée du tableau ressemble à :

```cpp
struct pollfd {
    int   fd;       // le file descriptor à surveiller
    short events;   // ce qu'on veut surveiller (POLLIN, POLLOUT...)
    short revents;  // ce qui s'est passé (rempli par poll())
};
```

Contenu typique de `_pfds` pendant l'exécution :

```
Index 0 : { fd:3, events:POLLIN }              ← socket d'écoute
Index 1 : { fd:4, events:POLLIN }              ← côté serveur du bot
Index 2 : { fd:5, events:POLLIN }              ← côté bot de la socketpair
Index 3 : { fd:6, events:POLLIN }              ← client Alice (rien à envoyer)
Index 4 : { fd:7, events:POLLIN|POLLOUT }      ← client Bob (a un message à envoyer)
```

### 5.2 — La boucle `run()`

Fichier : [srcs/Server.cpp](../srcs/Server.cpp), méthode `run()`

```cpp
void Server::run()
{
    while (!g_stop) {   // g_stop = 1 quand Ctrl+C est pressé
        if (_pfds.empty())
            break;

        // ── Appel poll() ────────────────────────────────────────────────
        // poll() BLOQUE jusqu'à ce qu'au moins un événement se produise
        // -1 = pas de timeout : attend indéfiniment
        // Retourne le nombre de fds avec des événements, ou -1 si erreur
        int n = poll(&_pfds[0], static_cast<nfds_t>(_pfds.size()), -1);

        if (n < 0) {
            // EINTR = interruption par un signal (ex: SIGINT pendant le poll)
            // Ce n'est pas une vraie erreur, on recommence
            if (errno == EINTR) { if (g_stop) break; continue; }
            break; // autre erreur : on sort
        }

        // ── Traitement de chaque fd actif ────────────────────────────────
        for (size_t i = 0; i < _pfds.size(); ++i) {
            if (_pfds[i].revents == 0)
                continue; // aucun événement sur ce fd → passer au suivant

            int fd = _pfds[i].fd;

            try {
                // ── Cas 1 : Nouvelle connexion ───────────────────────────
                if (fd == _listen_fd) {
                    if (_pfds[i].revents & POLLIN)
                        acceptClient(); // crée un nouveau Client
                    continue;
                }

                // ── Cas 2 : Le bot a des données à lire ─────────────────
                if (_bot && fd == _bot_pipe_fd) {
                    if (_pfds[i].revents & POLLIN)
                        _bot->onRead(); // le bot traite les réponses du serveur
                    continue;
                }

                // ── Cas 3 : Événement DCC relay ──────────────────────────
                if (_dcc_relay && _dcc_relay->hasFd(fd)) {
                    _dcc_relay->handleFd(fd, _pfds[i].revents);
                    continue;
                }

                // ── Ignore les fds déjà marqués morts ────────────────────
                if (_dead_fds.find(fd) != _dead_fds.end())
                    continue;

                // ── Cas 4 : Client IRC normal ────────────────────────────
                std::map<int, Client *>::iterator cit = _clients.find(fd);
                if (cit == _clients.end()) continue;
                Client &c = *cit->second;

                // Erreur ou déconnexion brutale
                if (_pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    _dead_fds.insert(fd);
                    continue;
                }

                // Données à lire
                if (_pfds[i].revents & POLLIN) {
                    handleRead(c);
                    // handleRead() peut avoir marqué le client comme mort
                    if (_dead_fds.find(fd) != _dead_fds.end()) continue;
                }

                // Prêt à envoyer
                if (_pfds[i].revents & POLLOUT)
                    handleWrite(c);

            } catch (const std::exception &e) {
                // Isoler les exceptions par client : une erreur ne tue pas le serveur
                _dead_fds.insert(fd);
            }
        }

        // ── Après la boucle : supprimer les clients morts ────────────────
        reapDisconnected();
    }
    // Fin de run() → le destructeur fait le ménage
}
```

### 5.3 — `handleRead()` : lire les données

```cpp
void Server::handleRead(Client &client)
{
    char buf[4096];
    // recv() lit jusqu'à 4096 octets depuis le socket
    ssize_t n = recv(client.getFd(), buf, sizeof(buf), 0);

    if (n <= 0) {
        // n == 0 : le client a fermé proprement (FIN TCP)
        // n < 0  : erreur réseau
        _dead_fds.insert(client.getFd());
        return;
    }

    // Accumuler dans le buffer de lecture du client
    client.readBuf().append(buf, static_cast<size_t>(n));

    // Sécurité : si le buffer grossit trop, déconnecter
    // (protège contre les attaques par remplissage de buffer)
    if (client.readBuf().size() > 65536) {
        _dead_fds.insert(client.getFd());
        return;
    }

    // Tenter d'extraire et traiter des commandes complètes
    consumeCommands(client);
}
```

### 5.4 — `handleWrite()` : envoyer les données en attente

```cpp
void Server::handleWrite(Client &client)
{
    if (client.writeBuf().empty()) return;

    // Envoyer le plus possible depuis le write_buf
    ssize_t n = send(client.getFd(),
                     client.writeBuf().c_str(),
                     client.writeBuf().size(),
                     0);

    if (n <= 0) {
        // Erreur d'envoi → déconnecter
        _dead_fds.insert(client.getFd());
        return;
    }

    // Supprimer les n octets envoyés du buffer
    client.writeBuf().erase(0, static_cast<size_t>(n));

    // Si le buffer est vide, désactiver POLLOUT
    // (inutile de surveiller "prêt à envoyer" si on n'a rien à envoyer)
    if (client.writeBuf().empty()) {
        std::map<int, size_t>::iterator it = _fd_to_pfd_idx.find(client.getFd());
        if (it != _fd_to_pfd_idx.end())
            _pfds[it->second].events = POLLIN; // retirer POLLOUT
    }
}
```

### 5.5 — `consumeCommands()` : extraire et dispatcher les commandes

```cpp
void Server::consumeCommands(Client &client)
{
    std::string line;

    // Boucle : extraire une ligne complète (\r\n ou \n) du buffer
    while (Parser::extractLine(client.readBuf(), line)) {
        if (line.empty()) continue;
        if (line.size() > 512) line.resize(512); // RFC 2812 : max 512 chars

        // Transformer la ligne texte en structure Message
        Message msg = Parser::tokenize(line);
        if (msg.command.empty()) continue;

        // Vérification d'authentification :
        // Un client non enregistré ne peut utiliser que ces commandes
        if (client.getState() != Client::STATE_REGISTERED) {
            if (msg.command != "PASS" && msg.command != "NICK"
             && msg.command != "USER" && msg.command != "CAP"
             && msg.command != "QUIT" && msg.command != "PING"
             && msg.command != "PONG") {
                sendTo(client, Reply::err_notregistered(_server_name, client));
                continue;
            }
        }

        // Appeler le handler de la commande
        _dispatcher.dispatch(*this, client, msg);

        // Si le handler a marqué le client mort, arrêter
        if (client.getState() == Client::STATE_DEAD) break;
    }
}
```

### 5.6 — `reapDisconnected()` + `disconnectClient()` : nettoyage propre

```cpp
void Server::reapDisconnected()
{
    // Copier _dead_fds pour éviter la modification pendant l'itération
    std::set<int> dead(_dead_fds);
    _dead_fds.clear();
    for (std::set<int>::iterator it = dead.begin(); it != dead.end(); ++it)
        disconnectClient(*it);
}

void Server::disconnectClient(int fd)
{
    std::map<int, Client *>::iterator it = _clients.find(fd);
    if (it == _clients.end()) return;

    Client *client = it->second;

    // 1. Envoyer ce qui reste dans le write_buf (messages en attente)
    if (!client->writeBuf().empty())
        send(fd, client->writeBuf().c_str(), client->writeBuf().size(),
             MSG_NOSIGNAL | MSG_DONTWAIT);

    // 2. Retirer du nick_index
    if (client->hasNick())
        _nick_index.erase(Utils::toLower(client->getNick()));

    // 3. Retirer de tous les channels (et supprimer les channels vides)
    removeClientFromAllChannels(*client);

    // 4. Fermer le socket
    close(fd);

    // 5. Retirer de _pfds via "swap with last" pour éviter les trous
    //    (O(1) grâce à _fd_to_pfd_idx)
    std::map<int, size_t>::iterator idx_it = _fd_to_pfd_idx.find(fd);
    if (idx_it != _fd_to_pfd_idx.end()) {
        size_t idx  = idx_it->second;
        size_t last = _pfds.size() - 1;
        if (idx != last) {
            // Déplacer le dernier élément à la place du supprimé
            _pfds[idx] = _pfds[last];
            _fd_to_pfd_idx[_pfds[idx].fd] = idx;
        }
        _pfds.pop_back();
        _fd_to_pfd_idx.erase(idx_it);
    }

    // 6. Libérer la mémoire
    delete client;
    _clients.erase(it);
}
```

> **Astuce "swap with last"** : pour supprimer un élément d'un `vector` sans décaler tous les éléments suivants (O(n)), on échange avec le dernier élément et on `pop_back()` (O(1)). L'ordre dans `_pfds` n'a pas d'importance.

### 5.7 — `sendTo()` et `enableWrite()` : envoyer un message à un client

```cpp
// Utilisé par tous les handlers de commandes pour envoyer une réponse
void Server::sendTo(Client &client, const std::string &message)
{
    client.appendToWrite(message); // ajouter au write_buf
    enableWrite(client);           // activer POLLOUT pour envoyer au prochain tour
}

void Server::enableWrite(Client &client)
{
    std::map<int, size_t>::iterator it = _fd_to_pfd_idx.find(client.getFd());
    if (it != _fd_to_pfd_idx.end())
        _pfds[it->second].events |= POLLOUT; // ajouter POLLOUT au masque
}
```

---

## 6. Timeline d'un événement complet

```
Alice (irssi) tape : "/join #general"
irssi envoie sur le réseau : "JOIN #general\r\n"

┌─ poll() retourne ──────────────────────────────────────────────────────┐
│  _pfds[3] : { fd:6 (Alice), revents: POLLIN }                         │
└────────────────────────────────────────────────────────────────────────┘
                │
                ▼
handleRead(alice)
  recv() → lit "JOIN #general\r\n" dans buf
  alice.readBuf() = "JOIN #general\r\n"
  consumeCommands(alice)
    extractLine() → line = "JOIN #general"
    tokenize()    → Message { command:"JOIN", params:["#general"] }
    dispatcher.dispatch() → cmd_join(server, alice, msg)
      cmd_join :
        - trouve ou crée #general
        - alice.addChannel("#general")
        - envoie JOIN à tous les membres
        - server.sendTo(alice, "... 353 alice = #general :alice\r\n")
        - server.enableWriteAll(#general)

┌─ poll() retourne ──────────────────────────────────────────────────────┐
│  _pfds[3] : { fd:6 (Alice), revents: POLLOUT }                        │
└────────────────────────────────────────────────────────────────────────┘
                │
                ▼
handleWrite(alice)
  send() → envoie le contenu du write_buf d'alice
  writeBuf vide → désactive POLLOUT

Alice voit dans irssi : "Now talking in #general"
```

---

## 7. Récapitulatif

| Méthode | Rôle |
|---------|------|
| `run()` | Boucle infinie : `poll()` → dispatch par type d'événement → `reapDisconnected()` |
| `handleRead()` | `recv()` → accumule dans `readBuf` → `consumeCommands()` |
| `handleWrite()` | `send()` depuis `writeBuf` → désactive `POLLOUT` si buffer vide |
| `consumeCommands()` | Extrait des lignes, parse, vérifie l'état, dispatche |
| `reapDisconnected()` | Supprime les clients marqués `_dead_fds` après la boucle |
| `disconnectClient()` | Ferme le socket, nettoie nick_index, channels, poll array, libère la mémoire |
| `sendTo()` | Ajoute au write_buf et active POLLOUT |

---

## Moyen de vérifier que ça marche

### Vérification 1 — Plusieurs connexions simultanées

```bash
./ircserv 6667 secret &
# Ouvrir 3 terminaux et se connecter depuis chacun :
nc 127.0.0.1 6667   # terminal A
nc 127.0.0.1 6667   # terminal B
nc 127.0.0.1 6667   # terminal C
```

Le serveur doit accepter les 3 sans bloquer. Chacun doit avoir son propre fd dans les logs `(fd 4)`, `(fd 5)`, `(fd 6)`.

### Vérification 2 — La boucle ne consomme pas 100% CPU

```bash
./ircserv 6667 secret &
# Regarder la consommation CPU sans clients connectés :
top -p $!
# Attendu : ~0% CPU (poll() bloque en attendant des événements)
```

### Vérification 3 — La déconnexion brutale est gérée

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
# Fermer nc avec Ctrl+C (déconnexion brutale)
# Le serveur ne doit pas crasher
# Alice doit être retirée des channels et de _clients
```

### Vérification 4 — POLLOUT est bien activé/désactivé

```bash
# Avec strace, vérifier que poll() n'inclut POLLOUT que quand nécessaire :
strace -e poll ./ircserv 6667 secret 2>&1 | grep POLLOUT
# Avant d'envoyer une réponse : events=POLLIN|POLLOUT
# Après envoi : events=POLLIN seulement
```

**Prochain chapitre** : [04_gestion_clients.md](04_gestion_clients.md) — la classe `Client`, ses états et ses buffers.
