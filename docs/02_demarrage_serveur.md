# Phase 02 — Démarrage du Serveur

> **Pré-requis** : avoir lu [01_reseau_et_sockets.md](01_reseau_et_sockets.md)  
> **Fichiers concernés** : [srcs/main.cpp](../srcs/main.cpp), [srcs/Server.cpp](../srcs/Server.cpp) lignes 1–170, [includes/Server.hpp](../includes/Server.hpp)

---

## 1. Concept de base — Le point d'entrée d'un serveur

Tout programme C++ commence par `main()`. Dans notre cas, `main()` est minimaliste : il valide les arguments, crée un objet `Server`, et lui demande de démarrer. Tout le travail complexe est délégué à la classe `Server`.

Le démarrage du serveur se déroule en **deux étapes** :
1. `Server(port, password)` — le **constructeur** : initialise les membres, enregistre les commandes IRC.
2. `Server::init()` — la **configuration réseau** : crée le socket, le lie au port, configure les signaux, crée le Bot et le DCC relay.

Après `init()`, on entre dans la boucle infinie avec `Server::run()`.

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Argument CLI** | Les valeurs passées en ligne de commande : `./ircserv 6667 monpassword`. `argc` = nombre d'arguments, `argv` = tableau de chaînes. |
| **strtol()** | Fonction C qui convertit une chaîne en entier long, avec détection d'erreur (contrairement à `atoi()` qui ignore les erreurs). |
| **Exception** | Mécanisme C++ pour signaler une erreur. Si `init()` échoue (port occupé, permission refusée...), il lance une `std::runtime_error` qui remonte jusqu'au `catch` dans `main()`. |
| **SIGINT** | Signal envoyé par Ctrl+C. Le serveur l'intercepte pour s'arrêter proprement. |
| **SIGTERM** | Signal d'arrêt propre envoyé par `kill <pid>`. Même comportement que SIGINT. |
| **SIGPIPE** | Signal envoyé quand on écrit dans un socket fermé. On l'ignore (`SIG_IGN`) pour éviter que le serveur plante. |
| **volatile sig_atomic_t** | Type entier dont les accès sont atomiques et toujours lus depuis la mémoire (pas de cache CPU). Indispensable pour les variables modifiées dans un gestionnaire de signal. |
| **`g_stop`** | Variable globale qui devient `1` quand un signal d'arrêt est reçu. La boucle `run()` la vérifie. |

---

## 3. Objectif attendu dans ce projet

Le démarrage doit :
1. Valider que le port est un entier dans `[1024, 65535]`.
2. Valider que le mot de passe n'est pas vide.
3. Créer le socket d'écoute et le lier au port.
4. Mettre en place les gestionnaires de signaux (`SIGINT`, `SIGTERM`, `SIGPIPE`).
5. Initialiser les composants bonus (Bot, DCC relay).
6. Afficher un message de confirmation et entrer dans la boucle.

---

## 4. Pourquoi ces objectifs ?

### Pourquoi valider le port avec `strtol` et non `atoi` ?

```cpp
// MAUVAIS : atoi("abc") retourne 0 sans erreur
int port = atoi(argv[1]);

// BON : strtol détecte les erreurs
char *end = 0;
long port = std::strtol(argv[1], &end, 10);
if (end == argv[1] || *end != '\0' || port < 1024 || port > 65535) {
    // erreur!
}
```

`strtol` remplit `end` avec le pointeur sur le premier caractère non-numérique. Si `end == argv[1]`, aucun chiffre n'a été lu. Si `*end != '\0'`, il y avait des caractères non-numériques.

### Pourquoi les ports < 1024 sont-ils interdits ?

Sur Linux, les ports 0–1023 sont réservés aux services système (80=HTTP, 443=HTTPS, 22=SSH...) et nécessitent les droits `root`. Un étudiant à l'école n'a pas ces droits, donc le projet impose `[1024, 65535]`.

### Pourquoi intercepter les signaux ?

Sans gestionnaire de signal, `Ctrl+C` tuerait le processus immédiatement, sans libérer la mémoire, sans fermer les connexions, sans afficher de message. Avec le gestionnaire, on pose un drapeau (`g_stop = 1`) et la boucle s'arrête proprement au prochain tour.

### Pourquoi ignorer `SIGPIPE` ?

Si un client ferme sa connexion et que le serveur essaie d'écrire vers lui, le noyau envoie `SIGPIPE`. Par défaut, ce signal tue le processus. On l'ignore car on gère déjà l'erreur au niveau de `send()` (qui retourne `-1` avec `errno = EPIPE`).

---

## 5. Implémentation — Code détaillé

### 5.1 — main.cpp : validation et lancement

Fichier : [srcs/main.cpp](../srcs/main.cpp)

```cpp
int main(int argc, char **argv)
{
    // Vérification du nombre d'arguments
    if (argc != 3) {
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }

    // Conversion et validation du port
    // strtol(chaîne, &pointeur_fin, base)
    char *end = 0;
    long port = std::strtol(argv[1], &end, 10);
    if (end == argv[1]      // aucun chiffre lu
     || *end != '\0'        // caractères non-numériques après
     || port < 1024         // port réservé système
     || port > 65535) {     // port invalide TCP
        std::cerr << "ircserv: port must be an integer in [1024, 65535]" << std::endl;
        return 1;
    }

    // Validation du mot de passe
    std::string password(argv[2]);
    if (password.empty()) {
        std::cerr << "ircserv: password must not be empty" << std::endl;
        return 1;
    }

    try {
        // Créer le serveur → appelle le constructeur
        Server server(static_cast<int>(port), password);
        
        // Configurer les sockets, signaux, bot...
        server.init();
        
        // Entrer dans la boucle infinie (bloque jusqu'à Ctrl+C)
        server.run();
        
    } catch (const std::exception &e) {
        // Si init() ou run() échouent, on affiche l'erreur proprement
        std::cerr << "ircserv: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

### 5.2 — Constructeur du Server

Fichier : [srcs/Server.cpp](../srcs/Server.cpp), lignes 9–16

```cpp
// Variable globale accessible par le gestionnaire de signal
namespace {
    volatile sig_atomic_t g_stop = 0;    // 0 = continuer, 1 = arrêter
    void onSignal(int) { g_stop = 1; }   // handler : juste poser le drapeau
}

Server::Server(int port, const std::string &password)
    : _port(port),              // port à écouter
      _password(password),      // mot de passe d'authentification
      _server_name("ircserv"),  // nom du serveur dans les messages IRC
      _listen_fd(-1),           // -1 = pas encore créé
      _server_ip(0),            // sera rempli après bind()
      _start_time(std::time(NULL)), // heure de démarrage (pour RPL_CREATED)
      _bot(NULL),               // [BONUS] pas encore créé
      _dcc_relay(NULL),         // [BONUS] pas encore créé
      _bot_pipe_fd(-1)          // [BONUS] pas encore créé
{
    // Enregistre toutes les commandes IRC dans le dispatcher
    registerCommands();
}
```

> **Note** : `-1` est la convention POSIX pour "fd invalide". Toujours initialiser les fd à `-1` pour détecter les cas où ils n'ont pas encore été créés.

### 5.3 — `Server::init()` : la vraie configuration

Fichier : [srcs/Server.cpp](../srcs/Server.cpp), lignes 33–170

```cpp
void Server::init()
{
    // ── Étape 1 : Gestionnaires de signaux ──────────────────────────────
    std::signal(SIGINT,  onSignal);  // Ctrl+C → g_stop = 1
    std::signal(SIGTERM, onSignal);  // kill → g_stop = 1
    std::signal(SIGPIPE, SIG_IGN);   // écriture sur socket fermé → ignoré

    // ── Étape 2 : Créer le socket d'écoute ──────────────────────────────
    _listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listen_fd < 0)
        throw std::runtime_error("socket() failed");
    // _listen_fd vaut maintenant quelque chose comme 3

    // ── Étape 3 : Options du socket ─────────────────────────────────────
    int opt = 1;
    setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(_listen_fd, F_SETFL, O_NONBLOCK);

    // ── Étape 4 : Préparer et lier l'adresse ────────────────────────────
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // écouter sur toutes les interfaces
    addr.sin_port        = htons(static_cast<unsigned short>(_port));
    bind(_listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));

    // ── Étape 5 : Récupérer l'IP réelle après bind ──────────────────────
    // Utile pour le DCC qui doit communiquer l'IP du serveur
    struct sockaddr_in bound;
    socklen_t len = sizeof(bound);
    getsockname(_listen_fd, reinterpret_cast<struct sockaddr *>(&bound), &len);
    _server_ip = ntohl(bound.sin_addr.s_addr);
    if (_server_ip == 0) _server_ip = (127u << 24) | 1u; // 127.0.0.1

    // ── Étape 6 : Mettre en écoute ──────────────────────────────────────
    listen(_listen_fd, SOMAXCONN);

    // ── Étape 7 : Ajouter à poll() ──────────────────────────────────────
    // Le tableau _pfds est celui que poll() surveille
    pollfd pfd;
    pfd.fd     = _listen_fd;
    pfd.events = POLLIN;   // surveiller les connexions entrantes
    pfd.revents = 0;
    _pfds.push_back(pfd);
    _fd_to_pfd_idx[_listen_fd] = 0; // _listen_fd est à l'index 0

    // ── Étape 8 : [BONUS] Créer le DCC relay ────────────────────────────
    _dcc_relay = new DccRelay(*this);
    _dcc_relay->setServerIp(_server_ip);

    // ── Étape 9 : [BONUS] Créer et initialiser le Bot ───────────────────
    _bot = new Bot(*this, _password);
    _bot->init();  // crée la socketpair interne
    _bot_pipe_fd = _bot->getBotFd();

    // Enregistrer le côté "serveur" de la socketpair comme un client normal
    {
        Client *bc = new Client(_bot->getSrvFd(), "127.0.0.1");
        _clients[_bot->getSrvFd()] = bc;
        pollfd bpfd;
        bpfd.fd = _bot->getSrvFd(); bpfd.events = POLLIN; bpfd.revents = 0;
        _pfds.push_back(bpfd);
        _fd_to_pfd_idx[_bot->getSrvFd()] = _pfds.size() - 1;
    }
    // Ajouter le côté "bot" de la socketpair pour que le bot puisse lire
    {
        pollfd bpfd;
        bpfd.fd = _bot_pipe_fd; bpfd.events = POLLIN; bpfd.revents = 0;
        _pfds.push_back(bpfd);
    }

    std::cerr << "[ircserv] listening on port " << _port << std::endl;
}
```

### 5.4 — `registerCommands()` : brancher les handlers

Fichier : [srcs/Server.cpp](../srcs/Server.cpp), `registerCommands()`

```cpp
void Server::registerCommands()
{
    // Associe chaque mot-clé IRC à sa fonction de traitement
    // Ces fonctions sont définies dans srcs/commands/cmd_*.cpp
    _dispatcher.registerHandler("PASS",    cmd_pass);
    _dispatcher.registerHandler("NICK",    cmd_nick);
    _dispatcher.registerHandler("USER",    cmd_user);
    _dispatcher.registerHandler("CAP",     cmd_cap);
    _dispatcher.registerHandler("JOIN",    cmd_join);
    _dispatcher.registerHandler("PART",    cmd_part);
    _dispatcher.registerHandler("TOPIC",   cmd_topic);
    _dispatcher.registerHandler("MODE",    cmd_mode);
    _dispatcher.registerHandler("KICK",    cmd_kick);
    _dispatcher.registerHandler("INVITE",  cmd_invite);
    _dispatcher.registerHandler("PRIVMSG", cmd_privmsg);
    _dispatcher.registerHandler("NOTICE",  cmd_notice);
    _dispatcher.registerHandler("PING",    cmd_ping);
    _dispatcher.registerHandler("PONG",    cmd_pong);
    _dispatcher.registerHandler("QUIT",    cmd_quit);
    _dispatcher.registerHandler("WHOIS",   cmd_whois);
}
```

### 5.5 — Destructeur : nettoyage de la mémoire

```cpp
Server::~Server()
{
    delete _bot;        // libère le Bot (BONUS)
    delete _dcc_relay;  // libère le DCC relay (BONUS)

    // Fermer et libérer tous les clients
    for (std::map<int, Client *>::iterator it = _clients.begin();
         it != _clients.end(); ++it) {
        close(it->first);   // fermer le socket
        delete it->second;  // libérer l'objet Client
    }

    // Libérer tous les channels
    for (std::map<std::string, Channel *>::iterator it = _channels.begin();
         it != _channels.end(); ++it)
        delete it->second;

    if (_listen_fd >= 0)
        close(_listen_fd);  // fermer le socket d'écoute
}
```

> **Bonne pratique** : libérer les ressources dans l'ordre inverse de leur création. Toujours vérifier que le fd est valide (`>= 0`) avant de le fermer.

---

## 6. Schéma du démarrage complet

```
./ircserv 6667 monpassword
         │
         ▼
main() valide les arguments
         │
         ▼
Server server(6667, "monpassword")
  └─ constructeur initialise les membres
  └─ registerCommands() remplit _dispatcher

         │
         ▼
server.init()
  ├─ signal(SIGINT/SIGTERM/SIGPIPE)
  ├─ socket()   → _listen_fd = 3
  ├─ setsockopt(SO_REUSEADDR)
  ├─ fcntl(O_NONBLOCK)
  ├─ bind()     → lie le fd 3 au port 6667
  ├─ listen()   → accepte les connexions
  ├─ _pfds.push_back({fd:3, events:POLLIN})
  ├─ new DccRelay(...)
  ├─ new Bot(...) → socketpair → fd 4 et fd 5
  └─ [ircserv] listening on port 6667

         │
         ▼
server.run()   ← boucle infinie, voir Phase 03
```

---

## 7. Récapitulatif

- `main()` valide les arguments avec `strtol` (plus robuste que `atoi`) et délègue tout à `Server`.
- Le **constructeur** est léger : il initialise les membres et enregistre les commandes.
- `init()` fait tout le travail réseau : socket, options, bind, listen, signaux, bot, DCC.
- Les **signaux** (`SIGINT`, `SIGTERM`) sont interceptés proprement via un drapeau global `g_stop`.
- Le **destructeur** libère toute la mémoire : clients, channels, sockets — dans le bon ordre.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Arguments invalides

```bash
./ircserv              # manque les args  → Usage: ...
./ircserv 6667         # manque password  → Usage: ...
./ircserv abc secret   # port non entier  → port must be an integer...
./ircserv 6667 ""      # password vide    → password must not be empty
```

### Vérification 2 — Démarrage propre

```bash
./ircserv 6667 secret
# Attendu :
# [ircserv] listening on port 6667
```

### Vérification 3 — Arrêt propre avec Ctrl+C

```bash
./ircserv 6667 secret
# Connecter un client : nc 127.0.0.1 6667
# Appuyer sur Ctrl+C dans le terminal du serveur
# Attendu : le serveur s'arrête sans SIGABRT ni fuite mémoire
```

Avec valgrind pour vérifier l'absence de fuites :
```bash
valgrind --leak-check=full ./ircserv 6667 secret
# Connecter, déconnecter, Ctrl+C
# Attendu : 0 leaks, 0 errors
```

### Vérification 4 — SIGTERM depuis un autre terminal

```bash
./ircserv 6667 secret &
pid=$!
sleep 1
kill $pid      # envoie SIGTERM
# Attendu : le serveur s'arrête proprement (même comportement que Ctrl+C)
```

**Prochain chapitre** : [03_boucle_evenementielle.md](03_boucle_evenementielle.md) — le cœur du serveur : la boucle `poll()`.
