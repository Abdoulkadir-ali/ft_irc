# Phase 12 — Le Bot IRC Intégré

> **Pré-requis** : avoir lu [03_boucle_evenementielle.md](03_boucle_evenementielle.md) et [07_registration.md](07_registration.md)  
> **Fichiers concernés** : [includes/Bot.hpp](../includes/Bot.hpp), [srcs/Bot.cpp](../srcs/Bot.cpp), [srcs/Server.cpp](../srcs/Server.cpp) méthodes `init()` et `run()`

---

## 1. Concept de base — Qu'est-ce qu'un bot IRC ?

Un **bot IRC** est un programme qui se connecte à un serveur IRC **comme s'il était un utilisateur humain**. Il a un nick (pseudonyme), peut rejoindre des channels, envoyer des messages, et répondre à des commandes.

La particularité ici : le bot est **intégré dans le serveur lui-même** — ce n'est pas un programme externe. Il tourne **dans le même processus** que le serveur.

Analogie : imaginez un employé de la cafétéria qui est aussi membre de la salle de réunion. Il peut répondre aux questions des participants, mais il n'est pas un participant extérieur — il est "dans la pièce" (dans le même processus).

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Bot** | Programme qui se comporte comme un utilisateur IRC mais de manière automatisée. |
| **`socketpair()`** | Appel système qui crée deux file descriptors bidirectionnels connectés l'un à l'autre. Ce qu'on écrit dans l'un se lit dans l'autre. |
| **`AF_UNIX`** | Domaine Unix (local) : communication dans la même machine, sans réseau. |
| **`SOCK_STREAM`** | Type de socket : flux continu, fiable (comme TCP mais en local). |
| **`_srv_fd`** | L'un des deux bouts du socketpair, traité par le serveur comme s'il venait d'un vrai client TCP. |
| **`_bot_fd`** | L'autre bout, utilisé par le bot pour lire/écrire des messages IRC. |
| **Fake client** | Le client créé à partir de `_srv_fd` : le serveur ne sait pas que c'est un bot, il le traite comme n'importe qui d'autre. |
| **`!commande`** | Préfixe pour déclencher une action du bot dans un channel (`!ping`, `!time`, etc.). |
| **`processLine()`** | Méthode du bot qui analyse les messages IRC qu'il reçoit du serveur. |

---

## 3. La magie du `socketpair()`

C'est le concept central du bot. Voici comment ça marche :

```
socketpair(AF_UNIX, SOCK_STREAM, 0, fds)
           │
           └─ crée deux file descriptors : fds[0] et fds[1]
              Ce sont deux bouts d'un "tuyau bidirectionnel"
              Écrire dans fds[0] → se lit depuis fds[1]
              Écrire dans fds[1] → se lit depuis fds[0]
```

```
              ┌────────────────────────────────────┐
              │           MÊME PROCESSUS            │
              │                                    │
              │  ┌──────────────┐  ┌───────────┐  │
              │  │   SERVER     │  │   BOT     │  │
              │  │              │  │           │  │
              │  │  _clients    │  │  _bot_fd  │  │
              │  │  [_srv_fd]   │  │  = fds[1] │  │
              │  │  = fds[0]  ◄─┼──┼────►      │  │
              │  │            ──┼──┼────►      │  │
              │  └──────────────┘  └───────────┘  │
              │                                    │
              └────────────────────────────────────┘
```

- Le **serveur** voit `_srv_fd` comme un client normal dans `_clients`.
- Le **bot** écrit dans `_bot_fd` → le serveur lit depuis `_srv_fd` et traite les messages IRC.
- Le **serveur** écrit dans `_srv_fd` (vers le "client bot") → le bot lit depuis `_bot_fd`.

---

## 4. Objectif attendu dans ce projet

Le bot doit :
1. S'initialiser via `socketpair()` et se connecter en envoyant PASS/NICK/USER.
2. Rejoindre `#general` automatiquement une fois enregistré.
3. Répondre aux commandes `!ping`, `!time`, `!echo`, `!info`, `!help` dans les channels.
4. Répondre aussi aux messages privés directs.

---

## 5. Pourquoi ces objectifs ?

### Pourquoi utiliser `socketpair()` plutôt qu'un vrai client réseau ?

Si le bot créait une vraie connexion TCP localhost, ça marcherait aussi — mais ce serait plus complexe (gestion des erreurs réseau, délais, etc.). `socketpair()` est **plus simple, plus rapide et entièrement local**. De plus, le bot bénéficie automatiquement de tout le mécanisme de poll, d'I/O non-bloquant et du parseur IRC déjà en place : il n'y a rien à dupliquer.

### Pourquoi le bot est-il dans le même processus ?

Le projet interdit les threads et les processus enfants (fork). Le bot doit être une fonctionnalité bonus du serveur, pas un daemon séparé. L'astuce `socketpair` permet de simuler un client IRC complet sans aucun processus supplémentaire.

### Pourquoi le prefix `!` pour les commandes ?

C'est une convention IRC universelle. Le préfixe `!` permet de distinguer une commande bot d'un message normal, et évite les faux positifs si quelqu'un écrit juste "ping" dans la conversation.

---

## 6. Implémentation — Code détaillé

### 6.1 — `Bot::init()` : créer la paire de sockets et s'enregistrer

Fichier : [srcs/Bot.cpp](../srcs/Bot.cpp)

```cpp
void Bot::init()
{
    int fds[2];

    // socketpair crée 2 file descriptors bidirectionnels connectés
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        return;  // échec silencieux : le bot ne sera pas disponible

    // Passer les deux fds en mode non-bloquant (comme tous les fds du serveur)
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    _srv_fd = fds[0]; // côté serveur : ajouté dans _clients comme un vrai client
    _bot_fd = fds[1]; // côté bot : le bot lit et écrit ici

    // Le bot s'authentifie en envoyant PASS, NICK, USER dans le tuyau
    // Le serveur recevra ces commandes via _srv_fd et les traitera normalement
    std::string reg = "PASS " + _password + "\r\n"
                      "NICK " + _nick     + "\r\n"
                      "USER ircbot 0 * :IRC Bot\r\n";
    send(_bot_fd, reg.c_str(), reg.size(), 0);
    // Au prochain tour de poll(), le serveur lira PASS/NICK/USER depuis _srv_fd
    // et enregistrera le bot comme un client normal nommé "IRCBot"
}
```

### 6.2 — Intégration dans `Server::init()`

Fichier : [srcs/Server.cpp](../srcs/Server.cpp)

```cpp
// Dans Server::init(), après le socket d'écoute :
if (_bot) {
    _bot->init();
    // Enregistrer _srv_fd comme un "vrai client" dans _clients
    // Le serveur l'ajoute dans _pfds[] pour poll() et dans _clients[]
    // → la magie : le serveur ne sait pas que c'est un bot !
    if (_bot->getSrvFd() >= 0)
        addClientFd(_bot->getSrvFd());

    // Ajouter _bot_fd à _pfds pour que poll() le surveille
    // (quand le serveur écrit vers IRCBot, le bot doit pouvoir lire)
    _bot_pipe_fd = _bot->getBotFd();
    struct pollfd pfd;
    pfd.fd = _bot_pipe_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _pfds.push_back(pfd);
}
```

### 6.3 — `Bot::onRead()` : lire les messages du serveur

```cpp
void Bot::onRead()
{
    char buf[4096];
    // Lire ce que le serveur a envoyé (réponses IRC, PRIVMSG des autres utilisateurs...)
    ssize_t n = recv(_bot_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        return;
    _read_buf.append(buf, static_cast<size_t>(n));

    // Extraire les lignes complètes une par une (utilise le même parseur que le serveur)
    std::string line;
    while (Parser::extractLine(_read_buf, line)) {
        if (!line.empty())
            processLine(line);
    }
}
```

### 6.4 — `Bot::processLine()` : analyser les messages reçus

```cpp
void Bot::processLine(const std::string &line)
{
    // ── Détecter le 001 (bienvenue) pour rejoindre le channel ───────────
    if (line.find(" 001 ") != std::string::npos) {
        _registered = true;
        sendLine("JOIN " + _default_channel);  // "JOIN #general\r\n"
        return;
    }

    // ── Parser les PRIVMSG : ":alice!alice@host PRIVMSG #general :!ping" ─
    if (line.size() < 2 || line[0] != ':')
        return;

    // Extraire le préfixe (avant le premier espace)
    std::string::size_type p1 = line.find(' ');
    std::string prefix = line.substr(1, p1 - 1); // "alice!alice@host"

    // Extraire la commande
    std::string rest = line.substr(p1 + 1);       // "PRIVMSG #general :!ping"
    std::string::size_type p2 = rest.find(' ');
    if (rest.substr(0, p2) != "PRIVMSG") return;  // ignorer tout sauf PRIVMSG

    // Extraire la cible
    rest = rest.substr(p2 + 1);
    std::string::size_type p3 = rest.find(' ');
    std::string target = rest.substr(0, p3);      // "#general" ou "IRCBot"

    // Extraire le texte (après ":")
    rest = rest.substr(p3 + 1);
    std::string text = (!rest.empty() && rest[0] == ':') ? rest.substr(1) : rest;
    // text = "!ping" ou "!echo bonjour"

    // Ignorer les messages CTCP (comme les VERSION, PING CTCP)
    if (!text.empty() && text[0] == '\x01')
        return;

    // Extraire le nick de l'expéditeur ("alice!alice@host" → "alice")
    std::string::size_type ex = prefix.find('!');
    std::string from = (ex != std::string::npos) ? prefix.substr(0, ex) : prefix;

    handlePrivmsg(from, target, text);
}
```

### 6.5 — `Bot::handlePrivmsg()` : répondre aux commandes

```cpp
void Bot::handlePrivmsg(const std::string &from, const std::string &target,
                        const std::string &text)
{
    bool inChannel = (!target.empty() && (target[0] == '#' || target[0] == '&'));
    bool toUs    = Utils::ircCaseEqual(target, _nick); // message privé direct à IRCBot
    bool trigger = inChannel && text.size() > 1 && text[0] == '!'; // "!ping" dans un channel

    // Ignorer si ce n'est ni un message direct ni une commande !
    if (!toUs && !trigger)
        return;

    // Répondre dans le channel si c'est une commande channel, sinon en privé à l'expéditeur
    std::string replyTo = inChannel ? target : from;

    // Extraire le mot-clé : "!ping world" → kw="PING", args="world"
    std::string cmd = (trigger && !text.empty()) ? text.substr(1) : text;
    std::string::size_type sp = cmd.find(' ');
    std::string kw   = Utils::toUpper(sp != std::string::npos ? cmd.substr(0, sp) : cmd);
    std::string args = (sp != std::string::npos) ? cmd.substr(sp + 1) : "";

    // ── Commandes supportées ─────────────────────────────────────────────
    if (kw == "HELP")
        sendLine("PRIVMSG " + replyTo + " :Commands: !ping !time !echo <text> !info");

    else if (kw == "PING")
        sendLine("PRIVMSG " + replyTo + " :PONG! Hi " + from + " :)");

    else if (kw == "TIME")
        sendLine("PRIVMSG " + replyTo + " :" + currentTime());
        // currentTime() → "2025-07-17 14:30:00 UTC"

    else if (kw == "ECHO")
        sendLine("PRIVMSG " + replyTo + " :" + (args.empty() ? "echo what?" : args));
        // "!echo bonjour tout le monde" → "bonjour tout le monde"

    else if (kw == "INFO")
        sendLine("PRIVMSG " + replyTo + " :IRCBot - ft_irc bonus bot. Try !help");

    else if (toUs) // message privé avec commande inconnue
        sendLine("PRIVMSG " + replyTo + " :Unknown command. Try !help");
}
```

### 6.6 — `Bot::sendLine()` : écrire vers le serveur

```cpp
void Bot::sendLine(const std::string &line)
{
    std::string msg = line + "\r\n";
    // Envoyer dans _bot_fd → le serveur lira depuis _srv_fd
    // et traitera le message comme s'il venait d'un client réseau
    send(_bot_fd, msg.c_str(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    // MSG_NOSIGNAL : ne pas envoyer SIGPIPE si l'autre bout est fermé
    // MSG_DONTWAIT : ne pas bloquer si le buffer est plein
}
```

---

## 7. Schéma de flux complet

```
alice tape dans irssi : /msg IRCBot !time

1. irssi → Serveur TCP : "PRIVMSG IRCBot :!time\r\n"
2. Server::handleRead(alice_fd) → tokenize → cmd_privmsg()
3. cmd_privmsg() : target="IRCBot", c'est un user
   → srv.sendTo(*bot_client, ":alice!alice@host PRIVMSG IRCBot :!time\r\n")
   → bot_client._write_buf += ":alice!alice@host PRIVMSG IRCBot :!time\r\n"
   → enableWrite(bot_client) → POLLOUT sur _srv_fd

4. handleWrite(_srv_fd) → send(_srv_fd, ...)
   → les données arrivent dans _bot_fd (via socketpair)

5. poll() : POLLIN sur _bot_pipe_fd (_bot_fd)
6. Server::run() détecte _bot_pipe_fd → _bot->onRead()
7. Bot::onRead() : recv(_bot_fd) → _read_buf += ":alice!alice@host PRIVMSG IRCBot :!time\r\n"
8. Parser::extractLine() → line = ":alice!alice@host PRIVMSG IRCBot :!time"
9. Bot::processLine() → handlePrivmsg("alice", "IRCBot", "!time")
10. kw == "TIME" → sendLine("PRIVMSG alice :2025-07-17 14:30:00 UTC")
    → send(_bot_fd, "PRIVMSG alice :2025-07-17 14:30:00 UTC\r\n")

11. poll() : POLLIN sur _srv_fd
12. Server::handleRead(_srv_fd) → tokenize → cmd_privmsg()
    → target="alice" → srv.sendTo(alice, ":IRCBot!ircbot@... PRIVMSG alice :2025-07-17...\r\n")

13. alice voit dans irssi :
    <IRCBot> 2025-07-17 14:30:00 UTC
```

---

## 8. Récapitulatif

| Élément | Rôle |
|---------|------|
| `socketpair()` | Crée le "tuyau" bidirectionnel entre serveur et bot |
| `_srv_fd` | Côté serveur — enregistré comme client normal dans `_clients` |
| `_bot_fd` | Côté bot — ajouté dans `_pfds`, lu dans `onRead()` |
| `init()` | Envoie PASS/NICK/USER via `_bot_fd` pour s'enregistrer |
| `onRead()` | Lit et parse les messages IRC reçus du serveur |
| `processLine()` | Détecte 001 (JOIN), filtre les PRIVMSG |
| `handlePrivmsg()` | Répond aux commandes `!ping`, `!time`, `!echo`, `!info`, `!help` |
| `sendLine()` | Écrit une commande IRC vers le serveur via `_bot_fd` |

- Le bot est **invisible** pour le reste du serveur : il passe par le même pipeline que tout client.
- La technique `socketpair` est une astuce **élégante et zero-overhead** pour simuler un client interne.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Le bot est visible comme un client normal

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
JOIN #general
# Le bot est dans #general — vérifier avec :
WHOIS IRCBot
# Attendu : infos du bot (nick, user, host)
```

### Vérification 2 — Commandes bot dans un channel

```bash
# Alice dans #general :
PRIVMSG #general :!ping
# Attendu (rapidement) : :IRCBot!ircbot@127.0.0.1 PRIVMSG #general :PONG!

PRIVMSG #general :!time
# Attendu : :IRCBot!... PRIVMSG #general :Current time: Wed May 28 ...

PRIVMSG #general :!echo Bonjour
# Attendu : :IRCBot!... PRIVMSG #general :Bonjour

PRIVMSG #general :!help
# Attendu : liste des commandes disponibles
```

### Vérification 3 — Message privé au bot

```bash
PRIVMSG IRCBot :!ping
# Attendu : :IRCBot!... PRIVMSG alice :PONG!
```

### Vérification 4 — Le bot est toujours dans #general après reconnexion d'alice

```bash
# Alice quitte et revient :
PART #general
JOIN #general
# Le bot est toujours là dans la NAMES list (@IRCBot ou IRCBot)
```

**Prochain chapitre** : [13_dcc_relay.md](13_dcc_relay.md) — le relais de transfert de fichiers DCC.
