# Phase 06 — Dispatch des Commandes

> **Pré-requis** : avoir lu [05_parsing_messages.md](05_parsing_messages.md)  
> **Fichiers concernés** : [includes/CommandDispatcher.hpp](../includes/CommandDispatcher.hpp), [srcs/CommandDispatcher.cpp](../srcs/CommandDispatcher.cpp), [includes/Commands.hpp](../includes/Commands.hpp), [srcs/Server.cpp](../srcs/Server.cpp) méthode `registerCommands()`

---

## 1. Concept de base — Comment router une commande vers le bon code ?

Une fois qu'on a parsé `"PRIVMSG #general :Salut"` en `Message { command:"PRIVMSG", params:["#general","Salut"] }`, il faut appeler la fonction qui gère `PRIVMSG`. On pourrait écrire :

```cpp
if (msg.command == "PRIVMSG") cmd_privmsg(srv, cli, msg);
else if (msg.command == "JOIN") cmd_join(srv, cli, msg);
else if (msg.command == "NICK") cmd_nick(srv, cli, msg);
// ... 16 else if ...
```

C'est fonctionnel, mais fragile : à chaque nouvelle commande il faut éditer la chaine `if/else`. La solution élégante est un **registre de commandes** (table de dispatch).

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Pointeur de fonction** | Une variable qui contient l'adresse mémoire d'une fonction. Permet de stocker des fonctions dans une map et de les appeler indirectement. |
| **typedef** | Alias de type en C++. Ici : `typedef void (*Handler)(Server&, Client&, const Message&)` crée un alias `Handler` pour le type "pointeur vers une fonction qui prend ces 3 arguments". |
| **Registre / Dispatch table** | Une `map<string, functionPointer>` : associe un nom de commande à sa fonction. Recherche en O(log n). |
| **Command pattern** | Patron de conception : encapsuler une action dans un objet (ou ici, une fonction), permettant de les enregistrer, router, annuler... |
| **ERR_UNKNOWNCOMMAND (421)** | Code numérique IRC renvoyé quand une commande inconnue est reçue. |

---

## 3. Objectif attendu dans ce projet

Le dispatcher doit :
1. Permettre d'**enregistrer** des paires (nom de commande → fonction handler).
2. **Router** chaque commande parsée vers son handler en O(log n).
3. Envoyer `ERR_UNKNOWNCOMMAND` si la commande est inconnue.

---

## 4. Pourquoi ces objectifs ?

### Pourquoi une map et non une série de if/else ?

- **Extensibilité** : pour ajouter une commande, on ajoute une ligne dans `registerCommands()` et un fichier `.cpp`. Rien à modifier ailleurs.
- **Séparation des responsabilités** : chaque commande est isolée dans son propre fichier. `cmd_join.cpp` ne connaît pas `cmd_nick.cpp`.
- **Testabilité** : on peut tester chaque commande indépendamment.
- **Lisibilité** : `registerCommands()` est un tableau récapitulatif de toutes les commandes supportées.

### Pourquoi la même signature pour tous les handlers ?

```cpp
typedef void (*Handler)(Server &srv, Client &cli, const Message &msg);
```

Chaque handler reçoit :
- `Server &srv` : pour accéder à l'état global (trouver des clients, créer des channels, envoyer des messages)
- `Client &cli` : le client qui a envoyé la commande
- `const Message &msg` : la commande parsée avec ses paramètres

C'est la signature minimale qui permet à n'importe quel handler de faire tout ce dont il a besoin.

---

## 5. Implémentation — Code détaillé

### 5.1 — La signature de type `Handler`

Fichier : [includes/CommandDispatcher.hpp](../includes/CommandDispatcher.hpp)

```cpp
class CommandDispatcher {
public:
    // Définir un alias "Handler" pour le type de pointeur de fonction
    // Traduction : "Handler est un pointeur vers une fonction qui
    //               prend (Server&, Client&, const Message&) et retourne void"
    typedef void (*Handler)(Server &, Client &, const Message &);

    void registerHandler(const std::string &command, Handler handler);
    void dispatch(Server &server, Client &client, const Message &msg);

private:
    // La map qui associe "JOIN" → &cmd_join, "NICK" → &cmd_nick, etc.
    std::map<std::string, Handler> _handlers;
};
```

### 5.2 — `registerHandler()` : enregistrement

Fichier : [srcs/CommandDispatcher.cpp](../srcs/CommandDispatcher.cpp)

```cpp
void CommandDispatcher::registerHandler(const std::string &command, Handler handler)
{
    // Simplement stocker l'association dans la map
    // command : "JOIN", "NICK", "PRIVMSG"...
    // handler : pointeur vers cmd_join, cmd_nick, cmd_privmsg...
    _handlers[command] = handler;
}
```

### 5.3 — `dispatch()` : le routage

```cpp
void CommandDispatcher::dispatch(Server &server, Client &client, const Message &msg)
{
    if (msg.command.empty())
        return;

    // Chercher la commande dans la map
    std::map<std::string, Handler>::iterator it = _handlers.find(msg.command);

    if (it != _handlers.end()) {
        // Trouvé ! Appeler le handler via le pointeur de fonction
        // it->second est le pointeur de fonction
        // it->second(server, client, msg) = appeler la fonction
        it->second(server, client, msg);
    } else {
        // Commande inconnue : envoyer ERR_UNKNOWNCOMMAND (421)
        // Seulement si le client est enregistré (sinon il verrait trop d'erreurs)
        if (client.getState() == Client::STATE_REGISTERED)
            server.sendTo(client,
                Reply::err_unknowncommand(server.getName(), client, msg.command));
    }
}
```

**Anatomie d'un appel indirect :**

```cpp
it->second(server, client, msg);
// it->second est un Handler, c'est-à-dire un pointeur de fonction
// Écrire it->second(a, b, c) est identique à appeler cmd_join(a, b, c)
// si it->second pointe vers cmd_join
```

### 5.4 — `registerCommands()` dans Server : le tableau de commandes

Fichier : [srcs/Server.cpp](../srcs/Server.cpp), méthode `registerCommands()` appelée dans le constructeur

```cpp
void Server::registerCommands()
{
    // ── Registration ───────────────────────────────────────────────────
    _dispatcher.registerHandler("PASS",    cmd_pass);   // mot de passe serveur
    _dispatcher.registerHandler("NICK",    cmd_nick);   // choisir un pseudo
    _dispatcher.registerHandler("USER",    cmd_user);   // définir username + realname
    _dispatcher.registerHandler("CAP",     cmd_cap);    // négociation IRCv3 (répondre NAK)

    // ── Channels ───────────────────────────────────────────────────────
    _dispatcher.registerHandler("JOIN",    cmd_join);   // rejoindre un canal
    _dispatcher.registerHandler("PART",    cmd_part);   // quitter un canal
    _dispatcher.registerHandler("TOPIC",   cmd_topic);  // lire/modifier le sujet
    _dispatcher.registerHandler("MODE",    cmd_mode);   // gérer les modes du canal
    _dispatcher.registerHandler("KICK",    cmd_kick);   // expulser un membre (op only)
    _dispatcher.registerHandler("INVITE",  cmd_invite); // inviter quelqu'un (op only)

    // ── Messagerie ─────────────────────────────────────────────────────
    _dispatcher.registerHandler("PRIVMSG", cmd_privmsg);// message privé ou canal
    _dispatcher.registerHandler("NOTICE",  cmd_notice); // comme PRIVMSG, sans erreurs

    // ── Connexion ──────────────────────────────────────────────────────
    _dispatcher.registerHandler("PING",    cmd_ping);   // keepalive → PONG
    _dispatcher.registerHandler("PONG",    cmd_pong);   // réponse PING (no-op)
    _dispatcher.registerHandler("QUIT",    cmd_quit);   // déconnexion propre

    // ── Info ───────────────────────────────────────────────────────────
    _dispatcher.registerHandler("WHOIS",   cmd_whois);  // info sur un utilisateur
}
```

### 5.5 — La signature uniforme des handlers

Fichier : [includes/Commands.hpp](../includes/Commands.hpp)

```cpp
// Tous les handlers ont EXACTEMENT la même signature
// Cela permet de les stocker dans la même map

void cmd_pass(Server &srv, Client &cli, const Message &msg);
void cmd_nick(Server &srv, Client &cli, const Message &msg);
void cmd_user(Server &srv, Client &cli, const Message &msg);
void cmd_quit(Server &srv, Client &cli, const Message &msg);
void cmd_ping(Server &srv, Client &cli, const Message &msg);
void cmd_pong(Server &srv, Client &cli, const Message &msg);
void cmd_cap(Server &srv, Client &cli, const Message &msg);
void cmd_privmsg(Server &srv, Client &cli, const Message &msg);
void cmd_notice(Server &srv, Client &cli, const Message &msg);
void cmd_join(Server &srv, Client &cli, const Message &msg);
void cmd_part(Server &srv, Client &cli, const Message &msg);
void cmd_topic(Server &srv, Client &cli, const Message &msg);
void cmd_kick(Server &srv, Client &cli, const Message &msg);
void cmd_invite(Server &srv, Client &cli, const Message &msg);
void cmd_mode(Server &srv, Client &cli, const Message &msg);
void cmd_whois(Server &srv, Client &cli, const Message &msg);
```

### 5.6 — Exemple de handler simple : `cmd_ping`

Fichier : [srcs/commands/cmd_ping.cpp](../srcs/commands/cmd_ping.cpp)

```cpp
void cmd_ping(Server &srv, Client &cli, const Message &msg)
{
    // PING peut avoir un paramètre (le "token" à renvoyer)
    if (msg.params.empty()) {
        // Sans token : répondre PONG simple
        srv.sendTo(cli, ":" + srv.getName() + " PONG " + srv.getName() + "\r\n");
    } else {
        // Avec token : renvoyer le token dans le PONG
        // Cela permet au client de mesurer le temps de réponse
        srv.sendTo(cli, ":" + srv.getName() + " PONG " + srv.getName()
                        + " :" + msg.params[0] + "\r\n");
    }
}
// cmd_pong est vide : le serveur ne PING pas les clients,
// donc recevoir un PONG ne signifie rien à faire
void cmd_pong(Server &, Client &, const Message &) {}
```

### 5.7 — Exemple de handler avec déconnexion : `cmd_quit`

Fichier : [srcs/commands/cmd_quit.cpp](../srcs/commands/cmd_quit.cpp)

```cpp
void cmd_quit(Server &srv, Client &cli, const Message &msg)
{
    // Message de déconnexion optionnel
    std::string reason = "Quit";
    if (!msg.params.empty())
        reason = msg.params[0];

    // Prévenir tous les channels que cet utilisateur quitte
    std::string quitMsg = ":" + cli.getPrefix() + " QUIT :" + reason + "\r\n";
    srv.broadcastToClientChannels(cli, quitMsg, &cli);

    // Retirer de tous les channels
    srv.removeClientFromAllChannels(cli);

    // Envoyer une confirmation au client
    srv.sendTo(cli, "ERROR :Closing link (" + cli.getPrefix()
                    + ") [Quit: " + reason + "]\r\n");

    // Marquer pour déconnexion (sera traité dans reapDisconnected())
    srv.markForDisconnect(cli);
}
```

---

## 6. Schéma complet du pipeline commande → handler

```
Ligne brute reçue : "PRIVMSG alice :Salut!\r\n"
          │
          ▼ Parser::extractLine()
          "PRIVMSG alice :Salut!"
          │
          ▼ Parser::tokenize()
          Message { command:"PRIVMSG", params:["alice", "Salut!"] }
          │
          ▼ CommandDispatcher::dispatch()
          _handlers.find("PRIVMSG")
          → it->second = &cmd_privmsg
          → appel : cmd_privmsg(server, client, msg)
          │
          ▼ cmd_privmsg() :
          - trouve Alice dans _nick_index
          - srv.sendTo(alice, ":bob!bob@host PRIVMSG alice :Salut!\r\n")
          - alice.writeBuf += le message
          - enableWrite(alice) → POLLOUT activé
          │
          ▼ prochain tour de poll() : POLLOUT sur alice
          handleWrite(alice) → send() → Alice reçoit le message
```

---

## 7. Récapitulatif

- `CommandDispatcher` est un **registre de commandes** : une `map<string, pointeur_fonction>`.
- `registerHandler()` = enregistrer une paire `"COMMANDE"` → `fonction`.
- `dispatch()` = chercher dans la map, appeler la fonction si trouvée, envoyer `ERR_UNKNOWNCOMMAND` sinon.
- Tous les handlers ont **la même signature** `(Server&, Client&, const Message&)` → stockables dans la même map.
- Chaque commande est isolée dans son propre fichier `cmd_xxx.cpp` → séparation des responsabilités.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Commande inconnue retourne 421

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
FOOBAR param1 param2
# Attendu : :ircserv 421 alice FOOBAR :Unknown command
```

### Vérification 2 — Toutes les commandes enregistrées sont reconnues

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
PING ircserv
# Attendu : :ircserv PONG ircserv :ircserv  (pas de 421)
```

### Vérification 3 — Commandes avant registration n'exécutent pas le handler complet

```bash
nc 127.0.0.1 6667
JOIN #test   # sans PASS/NICK/USER
# Attendu : 451 Not registered (guard dans cmd_join ou dispatch)
# Pas de crash, pas de création de channel vide
```

**Prochain chapitre** : [07_registration.md](07_registration.md) — les commandes `PASS`/`NICK`/`USER` et la séquence d'authentification.
