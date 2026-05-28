# Phase 01 — Le Réseau et les Sockets

> **Pré-requis** : avoir lu [00_vue_ensemble.md](00_vue_ensemble.md)  
> **Fichiers concernés** : [srcs/Server.cpp](../srcs/Server.cpp) lignes 34–100, [includes/Server.hpp](../includes/Server.hpp)

---

## 1. Concept de base — Comment deux ordinateurs communiquent-ils ?

Quand vous envoyez un message à quelqu'un, il doit traverser le réseau. Pour que ça marche, deux ordinateurs respectent un ensemble de règles qu'on appelle des **protocoles**.

### La pile TCP/IP

Internet fonctionne avec plusieurs couches de protocoles empilées :

```
Application (IRC, HTTP, FTP...)
    ↕  "Je veux envoyer 'PRIVMSG alice :Salut'"
Transport (TCP)
    ↕  "Je découpe en paquets, je garantis la livraison et l'ordre"
Internet (IP)
    ↕  "Je sais router les paquets vers la bonne machine"
Réseau (Ethernet, WiFi...)
    ↕  "Je gère les câbles et les ondes"
```

Pour IRC, on utilise **TCP** (Transmission Control Protocol) parce que :
- TCP **garantit que les données arrivent dans l'ordre** — essentiel pour IRC (les messages doivent arriver dans le bon ordre)
- TCP **garantit que rien ne se perd** — si un paquet disparaît, TCP le renvoie automatiquement
- TCP est une connexion **persistante** — contrairement à UDP qui est "tire et oublie"

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Socket** | Un point de communication réseau. C'est l'équivalent d'une prise électrique : côté serveur on met la prise au mur (`bind`+`listen`), côté client on y branche son câble (`connect`). |
| **File descriptor (fd)** | Un numéro entier donné par l'OS pour identifier une ressource ouverte (fichier, socket, pipe...). Si vous ouvrez un socket, l'OS vous donne le numéro `4`. Tout ce qui suit utilise ce `4` pour parler à ce socket. |
| **IP** | Adresse d'une machine sur le réseau (ex : `192.168.1.10`). `0.0.0.0` signifie "toutes les interfaces de cette machine". `127.0.0.1` est la machine locale (loopback). |
| **Port** | Numéro de 0 à 65535 qui identifie l'application destinataire sur une machine. Le serveur IRC écoute sur le port que vous lui donnez (ex : `6667`). |
| **bind()** | "Réserve" un port sur la machine : "le socket numéro 4 reçoit tout ce qui arrive sur le port 6667". |
| **listen()** | Met le socket en mode "attente de connexions". Analogie : ouvrir son magasin. |
| **accept()** | Accepte une connexion entrante et crée un **nouveau** socket rien que pour ce client. |
| **recv() / send()** | Lire depuis / écrire vers un socket. |
| **Non-bloquant** | Un appel qui retourne immédiatement même s'il n'y a pas de données disponibles (retourne `-1` avec `errno = EAGAIN`). Essentiel pour gérer plusieurs clients dans un seul thread. |
| **O_NONBLOCK** | Flag POSIX à passer à `fcntl()` pour rendre un fd non-bloquant. |
| **SO_REUSEADDR** | Option socket : autoriser la réutilisation du port immédiatement après la fermeture du serveur (sans attendre le délai TCP TIME_WAIT). |

---

## 3. Objectif attendu dans ce projet

Le serveur doit :
1. Créer un **socket TCP** qui écoute sur le port donné en argument.
2. Accepter **plusieurs connexions simultanées** de clients IRC.
3. Lire et écrire sur chaque socket de façon **non-bloquante** pour ne jamais bloquer.
4. Gérer tous les sockets dans **un seul thread** via `poll()`.

---

## 4. Pourquoi ces objectifs ?

### Pourquoi non-bloquant ?

Imaginez un serveur **bloquant** avec 100 clients :

```
Thread 1 → attend les données d'Alice   (BLOQUÉ pendant 30 secondes)
Thread 2 → attend les données de Bob    (BLOQUÉ pendant 10 secondes)
...
Thread 100 → attend les données de Charlie
```

Il faudrait 100 threads. Les threads consomment de la mémoire (~8 Mo chacun) et créer/détruire des threads est coûteux. Pour 10 000 clients, c'est impossible.

Avec l'**I/O non-bloquant + `poll()`**, un seul thread suffit :

```
Thread unique :
  "Qui a des données disponibles maintenant ?"
  Alice : oui → lis Alice
  Bob   : non → passe
  Charlie: oui → lis Charlie
  → Répète toutes les quelques millisecondes
```

### Pourquoi TCP et pas UDP ?

IRC transmet des **messages texte ordonnés**. Si Bob reçoit les messages dans le désordre, la conversation n'a plus de sens. TCP garantit l'ordre et la fiabilité.

### Pourquoi `SO_REUSEADDR` ?

Quand vous arrêtez le serveur et le relancez aussitôt, le port reste en état `TIME_WAIT` quelques secondes selon TCP. Sans `SO_REUSEADDR`, `bind()` échouerait avec "Address already in use". C'est juste de la commodité pour le développement.

---

## 5. Implémentation — Comment c'est fait dans le code

### 5.1 — Création du socket d'écoute

Fichier : [srcs/Server.cpp](../srcs/Server.cpp), lignes ~34–100

```cpp
// Server::init() dans srcs/Server.cpp

// Étape 1 : Créer un socket TCP IPv4
// AF_INET    = famille d'adresses IPv4
// SOCK_STREAM = type TCP (fiable, ordonné)
// 0          = protocole par défaut (TCP pour SOCK_STREAM)
_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
if (_listen_fd < 0)
    throw std::runtime_error("socket() failed");
// Ici _listen_fd vaut par exemple 3.
// L'OS nous a donné le numéro 3 pour ce socket.
```

### 5.2 — Options du socket

```cpp
// Étape 2 : SO_REUSEADDR — permet de relancer le serveur immédiatement
int opt = 1;
setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

// Étape 3 : O_NONBLOCK — le socket ne bloquera jamais
// F_SETFL = "Set File flags"
// O_NONBLOCK = toutes les opérations retournent immédiatement
fcntl(_listen_fd, F_SETFL, O_NONBLOCK);
```

> **Pourquoi `fcntl` plutôt que directement dans `socket()` ?**
> C'est la façon POSIX standard de modifier les flags d'un fd existant. `fcntl` = "File CoNTroL".

### 5.3 — Binding et Listen

```cpp
// Étape 4 : Préparer l'adresse d'écoute
struct sockaddr_in addr;
std::memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;           // IPv4
addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0 = toutes interfaces
addr.sin_port        = htons(static_cast<unsigned short>(_port));
//                      ^^ htons = "host to network short"
//                         Convertit l'endianness (ordre des octets)
//                         Le réseau utilise big-endian

// Étape 5 : Lier le socket au port
bind(_listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));

// Étape 6 : Mettre en écoute
// SOMAXCONN = taille maximale de la file d'attente de connexions pendantes
listen(_listen_fd, SOMAXCONN);
```

> **Que fait `htons()` ?**
> Les processeurs x86 stockent les nombres en "little-endian" (octet de poids faible en premier).
> Le réseau TCP/IP utilise "big-endian" (octet de poids fort en premier).
> `htons` = "Host TO Network Short" — convertit un entier 16 bits dans le bon ordre.
> `htonl` = même chose pour 32 bits.

### 5.4 — Récupération de l'adresse IP locale

```cpp
// Étape 7 : Récupérer l'IP réelle du serveur (utile pour le DCC)
struct sockaddr_in bound;
socklen_t len = sizeof(bound);
getsockname(_listen_fd, reinterpret_cast<struct sockaddr *>(&bound), &len);
_server_ip = ntohl(bound.sin_addr.s_addr); // ntohl = Network TO Host Long
if (_server_ip == 0)
    _server_ip = (127u << 24) | 1u; // fallback : 127.0.0.1
```

### 5.5 — Accepter une connexion client

Fichier : [srcs/Server.cpp](../srcs/Server.cpp), méthode `acceptClient()`

```cpp
void Server::acceptClient()
{
    struct sockaddr_in caddr;
    socklen_t len = sizeof(caddr);

    // accept() crée un NOUVEAU socket rien que pour ce client
    // _listen_fd reste ouvert pour accepter d'autres connexions
    int client_fd = accept(_listen_fd,
                           reinterpret_cast<struct sockaddr *>(&caddr),
                           &len);
    if (client_fd < 0)
        return; // EAGAIN : pas de connexion en attente (non-bloquant)

    // Rendre le socket client aussi non-bloquant
    fcntl(client_fd, F_SETFL, O_NONBLOCK);

    // Récupérer l'adresse IP du client sous forme de texte
    char host[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &caddr.sin_addr, host, sizeof(host));

    // Créer l'objet Client et l'enregistrer
    Client *c = new Client(client_fd, std::string(host));
    _clients[client_fd] = c;

    // Ajouter à poll() pour surveiller les données entrantes
    pollfd pfd;
    pfd.fd = client_fd; pfd.events = POLLIN; pfd.revents = 0;
    _pfds.push_back(pfd);
    _fd_to_pfd_idx[client_fd] = _pfds.size() - 1;
}
```

> **`accept()` vs `_listen_fd`** :
> Le socket `_listen_fd` est comme la réception d'un hôtel : il accueille les nouveaux clients.
> `accept()` crée un socket privé pour chaque client, comme lui attribuer une chambre.
> `_listen_fd` reste disponible pour le prochain client.

### 5.6 — Lire depuis un socket client

```cpp
void Server::handleRead(Client &client)
{
    char buf[4096];
    // recv() lit au maximum 4095 octets
    // MSG_DONTWAIT = non-bloquant (idem O_NONBLOCK mais par appel)
    ssize_t n = recv(client.getFd(), buf, sizeof(buf) - 1, 0);

    if (n == 0) {
        // n == 0 : le client a fermé sa connexion proprement (FIN TCP)
        _dead_fds.insert(client.getFd());
        return;
    }
    if (n < 0) {
        // n < 0 avec errno == EAGAIN : pas de données (normal, non-bloquant)
        // n < 0 avec autre errno : erreur réseau → déconnexion
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            _dead_fds.insert(client.getFd());
        return;
    }

    // Accumuler dans le buffer de lecture du client
    client.readBuf().append(buf, static_cast<size_t>(n));
    // Note: les données sont incomplètes possible (TCP fragmentation)
    // On accumule jusqu'à avoir une ligne complète (\r\n)
    consumeCommands(client);
}
```

### 5.7 — Écrire vers un socket client

```cpp
void Server::handleWrite(Client &client)
{
    std::string &wbuf = client.writeBuf();
    if (wbuf.empty()) {
        // Désactiver POLLOUT si rien à envoyer (économie CPU)
        size_t idx = _fd_to_pfd_idx[client.getFd()];
        _pfds[idx].events &= ~POLLOUT;
        return;
    }

    ssize_t n = send(client.getFd(), wbuf.c_str(), wbuf.size(), MSG_NOSIGNAL);
    // MSG_NOSIGNAL : ne pas générer SIGPIPE si le client est déconnecté

    if (n > 0)
        wbuf.erase(0, static_cast<size_t>(n)); // supprimer ce qui a été envoyé
    // Si wbuf n'est pas vide, POLLOUT reste actif → on réessaiera au prochain tour
}
```

> **Pourquoi un write_buf ?**  
> `send()` peut ne pas envoyer tous les octets en une fois (buffer noyau plein).
> On accumule dans `write_buf` et on envoie progressivement au fil des tours de `poll()`.

---

## 6. Schéma récapitulatif des appels système

```
Démarrage du serveur :
  socket()     → crée le fd d'écoute (_listen_fd = 3)
  setsockopt() → active SO_REUSEADDR
  fcntl()      → active O_NONBLOCK
  bind()       → associe le fd au port 6667
  listen()     → ouvre la file d'attente
  
Connexion d'un client :
  accept()     → crée un nouveau fd (client_fd = 5)
  fcntl()      → le rend non-bloquant
  inet_ntop()  → obtient l'IP du client en texte

Communication :
  recv()       → lit les données du client
  send()       → envoie des données au client
  
Fermeture :
  close()      → ferme le fd et libère les ressources
```

---

## 7. Récapitulatif

- Un **socket TCP** = un canal de communication fiable et ordonné entre deux machines.
- Un **file descriptor** = le numéro entier que l'OS donne à chaque socket ouvert.
- `socket()` → `setsockopt()` → `fcntl()` → `bind()` → `listen()` : les 5 étapes pour créer un serveur.
- `accept()` : crée un socket dédié à chaque nouveau client.
- **Non-bloquant** (`O_NONBLOCK`) : indispensable pour gérer de nombreux clients dans un seul thread.
- Les données ne viennent pas forcément en une fois : on accumule dans un buffer et on traite quand une ligne complète est disponible.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Le socket est bien créé et lié au port

```bash
./ircserv 6667 secret &
# Vérifier que le port est ouvert :
ss -tlnp | grep 6667
# Attendu : LISTEN  0  ...  0.0.0.0:6667
```

### Vérification 2 — SO_REUSEADDR fonctionne

```bash
./ircserv 6667 secret &   # lancer une première fois
kill %1                   # tuer le serveur
./ircserv 6667 secret     # relancer immédiatement
# Sans SO_REUSEADDR : "bind() failed: Address already in use"
# Avec SO_REUSEADDR : ça démarre sans erreur
```

### Vérification 3 — La connexion TCP s'établit

```bash
nc 127.0.0.1 6667
# Le terminal se met en attente (pas d'erreur "connection refused")
# → accept() a bien retourné un fd client
```

Dans les logs du serveur, vérifier :
```
[ircserv] new connection from 127.0.0.1:XXXXX (fd 6)
```

### Vérification 4 — Le port invalide est rejeté

```bash
./ircserv 80 secret    # port réservé → doit échouer
./ircserv abc secret   # non-numérique → doit échouer
./ircserv 99999 secret # hors plage → doit échouer
# Attendu : message d'erreur et exit 1
```

**Prochain chapitre** : [02_demarrage_serveur.md](02_demarrage_serveur.md) — comment tout ça est orchestré dans `main()` et `Server::init()`.
