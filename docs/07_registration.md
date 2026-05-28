# Phase 07 — L'Authentification (Registration)

> **Pré-requis** : avoir lu [06_dispatch_commandes.md](06_dispatch_commandes.md)  
> **Fichiers concernés** : [srcs/commands/cmd_pass.cpp](../srcs/commands/cmd_pass.cpp), [srcs/commands/cmd_nick.cpp](../srcs/commands/cmd_nick.cpp), [srcs/commands/cmd_user.cpp](../srcs/commands/cmd_user.cpp), [srcs/commands/cmd_cap.cpp](../srcs/commands/cmd_cap.cpp), [srcs/Server.cpp](../srcs/Server.cpp) méthode `tryRegister()`

---

## 1. Concept de base — Pourquoi une séquence d'authentification ?

Quand vous vous connectez à un serveur IRC, vous ne pouvez pas directement envoyer des messages. Vous devez d'abord **vous identifier** : prouver que vous connaissez le mot de passe du serveur, choisir un pseudonyme, et déclarer votre identité.

C'est comme entrer dans un club privé :
1. Vous frappez à la porte (connexion TCP)
2. Vous donnez le mot de passe du club (`PASS`)
3. Vous dites votre pseudonyme (`NICK`)
4. Vous remplissez votre fiche d'adhérent (`USER`)
5. Le club vous donne votre badge de membre (réponse `001 Welcome`)

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **PASS** | Commande pour envoyer le mot de passe serveur. Doit être la première commande envoyée. |
| **NICK** | Commande pour choisir/changer son pseudonyme. |
| **USER** | Commande pour définir `username`, `mode` et `realname`. Envoyée une seule fois. |
| **CAP** | "Capability Negotiation" — protocole IRCv3 pour activer des extensions. Notre serveur répond "pas de capacités supportées". |
| **001 RPL_WELCOME** | Réponse numérique envoyée quand la registration est complète. Signal que le client peut utiliser le serveur normalement. |
| **tryRegister()** | Fonction appelée après chaque PASS/NICK/USER pour vérifier si tous les critères sont réunis et déclencher la registration. |
| **STATE_PASS_OK** | État interne : le mot de passe a été validé. |
| **STATE_REGISTERED** | État final : le client est pleinement authentifié. |
| **Nick uniqueness** | Deux clients ne peuvent pas avoir le même pseudonyme (insensible à la casse). |
| **IRCv3** | Extension moderne du protocole IRC (2005+). Notre serveur le rejette proprement. |

---

## 3. La séquence de registration

L'ordre dans lequel les commandes peuvent arriver est **flexible** : irssi envoie parfois `NICK` avant `PASS`, WeeChat peut envoyer `CAP LS` en premier. `tryRegister()` est appelé après chaque commande pour détecter quand les 3 conditions sont réunies.

```
Conditions pour STATE_REGISTERED :
  ✓ STATE_PASS_OK          (PASS reçu et validé)
  ✓ client.hasNick()       (NICK reçu et validé)
  ✓ client.hasUser()       (USER reçu)
```

**Scénarios possibles :**

```
Scénario 1 (ordre classique) :
  PASS → STATE_PASS_OK, tryRegister() → échec (pas de NICK/USER)
  NICK → nick stocké,   tryRegister() → échec (pas de USER)
  USER → user stocké,   tryRegister() → SUCCÈS → STATE_REGISTERED → envoie 001

Scénario 2 (irssi envoie NICK avant PASS) :
  NICK → nick stocké,   tryRegister() → échec (pas STATE_PASS_OK)
  PASS → STATE_PASS_OK, tryRegister() → échec (pas de USER)
  USER → user stocké,   tryRegister() → SUCCÈS

Scénario 3 (avec CAP — WeeChat/HexChat) :
  CAP LS → serveur répond "CAP * LS :\r\n" (aucune capacité)
  NICK → nick stocké
  USER → user stocké
  PASS → STATE_PASS_OK, tryRegister() → SUCCÈS
  CAP END → ignoré
```

---

## 4. Objectif attendu dans ce projet

- Implémenter la séquence d'authentification IRC RFC 2812.
- Rejeter les mots de passe incorrects avec `ERR_PASSWDMISMATCH`.
- Valider les nicks selon les règles IRC (longueur, caractères autorisés).
- Rejeter les nicks déjà utilisés avec `ERR_NICKNAMEINUSE`.
- Envoyer les 4 messages de bienvenue (`001`–`004`) après registration.
- Gérer `CAP` pour la compatibilité avec les clients modernes.

---

## 5. Pourquoi ces objectifs ?

### Pourquoi exiger un mot de passe ?

Sans mot de passe, n'importe qui sur le réseau pourrait se connecter à votre serveur. Le projet exige un mot de passe pour contrôler l'accès. C'est `./ircserv 6667 MON_SECRET` — tous les clients doivent envoyer `PASS MON_SECRET`.

### Pourquoi valider le nick ?

La RFC 2812 définit des règles strictes :
- Longueur : 1 à 9 caractères
- Premier caractère : lettre ou `[ ] \ \` _ ^ { | }`
- Caractères suivants : alphanumérique ou `- [ ] \ \` _ ^ { | }`

Un nick invalide causerait des problèmes avec d'autres serveurs IRC ou des clients. La validation protège l'intégrité du réseau.

### Pourquoi gérer CAP ?

Les clients IRC modernes (irssi, WeeChat, HexChat) envoient `CAP LS` au début de toute connexion pour détecter les fonctionnalités IRCv3 supportées. Si le serveur n'y répond pas correctement, le client peut rester bloqué en attente. On répond "pas de capacités" (`CAP * LS :`) pour que le client passe à la suite.

### Pourquoi `tryRegister()` est-il appelé plusieurs fois ?

On ne sait pas dans quel ordre les commandes arriveront. Plutôt que de mettre la logique de completion dans chaque handler, on centralise dans `tryRegister()` qui vérifie à chaque fois si le moment est venu.

---

## 6. Implémentation — Code détaillé

### 6.1 — `cmd_pass()` : vérification du mot de passe

Fichier : [srcs/commands/cmd_pass.cpp](../srcs/commands/cmd_pass.cpp)

```cpp
void cmd_pass(Server &srv, Client &cli, const Message &msg)
{
    // Ne pas accepter PASS si déjà enregistré
    if (cli.getState() == Client::STATE_REGISTERED) {
        srv.sendTo(cli, Reply::err_alreadyregistered(srv.getName(), cli));
        return;
    }

    // PASS exige au moins un paramètre
    if (msg.params.empty()) {
        srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "PASS"));
        return;
    }

    // Vérifier le mot de passe
    if (msg.params[0] != srv.getPassword()) {
        // Mauvais mot de passe : envoyer une erreur ET déconnecter
        srv.sendTo(cli, Reply::err_passwdmismatch(srv.getName(), cli));
        srv.markForDisconnect(cli);  // ← déconnexion forcée
        return;
    }

    // Mot de passe correct : avancer dans la registration
    cli.setState(Client::STATE_PASS_OK);

    // Certains clients (irssi) envoient NICK/USER avant PASS
    // tryRegister() va vérifier si c'est déjà complet
    srv.tryRegister(cli);
}
```

> **Important** : En cas de mauvais mot de passe, on déconnecte le client immédiatement via `markForDisconnect()`. Ce n'est pas brutal mais sécurisé : un attaquant qui essaie des mots de passe est immédiatement coupé.

### 6.2 — `cmd_nick()` : choisir ou changer de pseudonyme

Fichier : [srcs/commands/cmd_nick.cpp](../srcs/commands/cmd_nick.cpp)

```cpp
void cmd_nick(Server &srv, Client &cli, const Message &msg)
{
    // NICK sans paramètre
    if (msg.params.empty()) {
        srv.sendTo(cli, Reply::err_nonicknamegiven(srv.getName(), cli));
        return;
    }

    const std::string &nick = msg.params[0];

    // Validation du format du nick (longueur, caractères autorisés)
    if (!Utils::isValidNick(nick)) {
        srv.sendTo(cli, Reply::err_erroneusnickname(srv.getName(), cli, nick));
        return;
    }

    // Vérifier l'unicité (comparaison insensible à la casse)
    // "Alice" et "alice" sont le même nick pour IRC
    Client *existing = srv.findClientByNick(nick);
    if (existing && existing != &cli) {
        // Un autre client utilise déjà ce nick
        srv.sendTo(cli, Reply::err_nicknameinuse(srv.getName(), cli, nick));
        return;
    }

    std::string oldNick = cli.getNick();

    if (cli.getState() == Client::STATE_REGISTERED) {
        // ── Changement de nick en cours de session ───────────────────────
        // Il faut prévenir tous les channels du changement
        std::string oldPrefix = cli.getPrefix(); // "alice!alice42@host"
        cli.setNick(nick);
        srv.updateNickIndex(cli, oldNick, nick);  // mettre à jour _nick_index

        // Message de changement de nick à soi-même ET aux channels communs
        std::string change = ":" + oldPrefix + " NICK :" + nick + "\r\n";
        srv.sendTo(cli, change);
        srv.broadcastToClientChannels(cli, change, &cli); // ne pas s'envoyer en double
    } else {
        // ── Pendant la registration ──────────────────────────────────────
        cli.setNick(nick);
        // Pas encore dans nick_index (seulement après STATE_REGISTERED)
        srv.tryRegister(cli); // peut déclencher la registration complète
    }
}
```

### 6.3 — Validation des nicks : `Utils::isValidNick()`

Fichier : [srcs/Utils.cpp](../srcs/Utils.cpp)

```cpp
bool Utils::isValidNick(const std::string &nick)
{
    // Règle 1 : longueur 1 à 9 caractères
    if (nick.empty() || nick.size() > 9)
        return false;

    // Règle 2 : premier caractère = lettre ou "special" IRC
    // Les "special" sont : [ ] \ ` _ ^ { | }
    char c = nick[0];
    if (!std::isalpha(static_cast<unsigned char>(c))
        && c != '[' && c != ']' && c != '\\'
        && c != '`' && c != '_' && c != '^'
        && c != '{' && c != '|' && c != '}')
        return false;

    // Règle 3 : caractères suivants = alphanumérique ou special ou '-'
    for (size_t i = 1; i < nick.size(); ++i) {
        c = nick[i];
        if (!std::isalnum(static_cast<unsigned char>(c))
            && c != '-' && c != '[' && c != ']' && c != '\\'
            && c != '`' && c != '_' && c != '^'
            && c != '{' && c != '|' && c != '}')
            return false;
    }
    return true;
}
```

**Nicks valides/invalides :**

```
"alice"    → ✓ (letters)
"Bob42"    → ✓ (letter + alphanum)
"_bot"     → ✓ ('_' est special)
"[test]"   → ✓ ('[' et ']' sont special)
""         → ✗ (vide)
"alice bob"→ ✗ (espace interdit)
"12345"    → ✗ (commence par chiffre)
"toolongnick"→ ✗ (> 9 chars)
"ali@ce"   → ✗ ('@' interdit)
```

### 6.4 — `cmd_user()` : définir l'identité

Fichier : [srcs/commands/cmd_user.cpp](../srcs/commands/cmd_user.cpp)

```cpp
void cmd_user(Server &srv, Client &cli, const Message &msg)
{
    // USER ne peut pas être répété après registration
    if (cli.getState() == Client::STATE_REGISTERED) {
        srv.sendTo(cli, Reply::err_alreadyregistered(srv.getName(), cli));
        return;
    }

    // USER exige 4 paramètres : <user> <mode> <unused> <realname>
    // Exemple : USER alice42 0 * :Alice Dupont
    if (msg.params.size() < 4) {
        srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "USER"));
        return;
    }

    cli.setUser(msg.params[0]);     // "alice42"
    // params[1] = mode (ignoré dans notre implémentation)
    // params[2] = serveur non utilisé (toujours "*")
    cli.setRealName(msg.params[3]); // "Alice Dupont" (trailing param)

    srv.tryRegister(cli); // peut déclencher la registration complète
}
```

### 6.5 — `tryRegister()` : le déclencheur de la registration

Fichier : [srcs/Server.cpp](../srcs/Server.cpp)

```cpp
void Server::tryRegister(Client &client)
{
    // Condition 1 : déjà enregistré → rien à faire
    if (client.getState() == Client::STATE_REGISTERED) return;

    // Condition 2 : mot de passe pas encore validé
    if (client.getState() != Client::STATE_PASS_OK) return;

    // Condition 3 : nick pas encore défini
    if (!client.hasNick() || !client.hasUser()) return;

    // ─── Toutes les conditions réunies : ENREGISTRER ──────────────────

    client.setState(Client::STATE_REGISTERED);

    // Ajouter au nick_index (maintenant on peut être trouvé par nick)
    _nick_index[Utils::toLower(client.getNick())] = &client;

    // Envoyer les 4 messages de bienvenue IRC (RFC 2812)
    sendTo(client, Reply::rpl_welcome(_server_name, client));
    // ":ircserv 001 alice :Welcome to the Internet Relay Network alice!alice42@host"
    
    sendTo(client, Reply::rpl_yourhost(_server_name, client));
    // ":ircserv 002 alice :Your host is ircserv, running version 1.0"
    
    sendTo(client, Reply::rpl_created(_server_name, client));
    // ":ircserv 003 alice :This server was created today"
    
    sendTo(client, Reply::rpl_myinfo(_server_name, client));
    // ":ircserv 004 alice ircserv 1.0 o itkol"
}
```

### 6.6 — `cmd_cap()` : compatibilité IRCv3

Fichier : [srcs/commands/cmd_cap.cpp](../srcs/commands/cmd_cap.cpp)

```cpp
void cmd_cap(Server &srv, Client &cli, const Message &msg)
{
    // On ne supporte aucune capacité IRCv3
    if (msg.params.empty()) return;

    if (msg.params[0] == "LS") {
        // Le client demande la liste des capacités
        // On répond avec une liste vide ":"
        srv.sendTo(cli, ":" + srv.getName() + " CAP * LS :\r\n");
    } else if (msg.params[0] == "REQ") {
        // Le client demande d'activer des capacités → on rejette tout (NAK)
        std::string caps = (msg.params.size() > 1) ? msg.params[1] : "";
        srv.sendTo(cli, ":" + srv.getName() + " CAP * NAK :" + caps + "\r\n");
    } else if (msg.params[0] == "END") {
        // Fin de la négociation → rien à faire
    }
}
```

**Interaction typique avec HexChat :**

```
Client → Serveur : "CAP LS 302\r\n"
Serveur → Client : ":ircserv CAP * LS :\r\n"   ← liste vide = aucune capacité

Client → Serveur : "PASS monpassword\r\n"
Client → Serveur : "NICK alice\r\n"
Client → Serveur : "USER alice42 0 * :Alice\r\n"
Client → Serveur : "CAP END\r\n"                ← fin de négociation

Serveur → Client : ":ircserv 001 alice :Welcome to the Internet Relay Network alice!alice42@host\r\n"
Serveur → Client : ":ircserv 002 alice :Your host is ircserv, running version 1.0\r\n"
Serveur → Client : ":ircserv 003 alice :This server was created today\r\n"
Serveur → Client : ":ircserv 004 alice ircserv 1.0 o itkol\r\n"
```

---

## 7. Schéma complet de la séquence d'authentification

```
Client se connecte (accept())
         │
         │ PASS monpassword
         ▼
    cmd_pass()
    ├── mot de passe correct ? → setState(PASS_OK)
    └── incorrect ? → ERR_PASSWDMISMATCH + markForDisconnect

         │ NICK alice
         ▼
    cmd_nick()
    ├── format valide ? isValidNick()
    ├── nick déjà pris ? findClientByNick()
    └── setNick("alice") → tryRegister() → échec (pas USER)

         │ USER alice42 0 * :Alice Dupont
         ▼
    cmd_user()
    └── setUser("alice42"), setRealName("Alice Dupont")
        → tryRegister()
          ├── PASS_OK ✓
          ├── hasNick() ✓
          └── hasUser() ✓
              → setState(REGISTERED)
              → _nick_index["alice"] = &client
              → sendTo(001 002 003 004)

Client voit : "Welcome to the Internet Relay Network alice!alice42@host"
```

---

## 8. Récapitulatif

| Commande | Rôle | Erreurs possibles |
|----------|------|-------------------|
| `PASS` | Envoyer le mot de passe | `ERR_PASSWDMISMATCH`, `ERR_ALREADYREGISTERED` |
| `NICK` | Choisir un pseudo | `ERR_ERRONEUSNICKNAME`, `ERR_NICKNAMEINUSE`, `ERR_NONICKNAMEGIVEN` |
| `USER` | Définir username + realname | `ERR_NEEDMOREPARAMS`, `ERR_ALREADYREGISTERED` |
| `CAP` | Négociation IRCv3 | Aucune (rejette tout silencieusement) |
| `tryRegister()` | Déclenche la registration si conditions réunies | Envoie 001–004 |

- L'ordre des commandes `PASS`/`NICK`/`USER` est **flexible** : `tryRegister()` gère tous les ordres.
- `NICK` peut aussi être utilisé **après** la registration pour changer de pseudo.
- Le nick est stocké en casse originale, comparé en minuscules via `Utils::toLower()`.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Flux complet de registration

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice Dupont
# Attendu :
# :ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1
# :ircserv 002 ...
# :ircserv 003 ...
# :ircserv 004 ...
```

### Vérification 2 — Mauvais mot de passe

```bash
nc 127.0.0.1 6667
PASS mauvais_mdp
NICK alice
USER alice 0 * :Alice
# Attendu : :ircserv 464 * :Password incorrect
# Puis connexion fermée par le serveur
```

### Vérification 3 — Nick déjà pris

```bash
# Terminal A : déjà connecté avec nick "alice"
# Terminal B :
nc 127.0.0.1 6667
PASS secret
NICK alice   # nick déjà utilisé
# Attendu : :ircserv 433 * alice :Nickname is already in use
NICK alice2  # nick libre
USER alice2 0 * :Alice 2
# Attendu : registration complète
```

### Vérification 4 — Nick invalide

```bash
nc 127.0.0.1 6667
PASS secret
NICK 123badnick   # commence par un chiffre
# Attendu : :ircserv 432 * 123badnick :Erroneous nickname
NICK alice        # nick valide
USER alice 0 * :Alice
# Registration réussie
```

### Vérification 5 — CAP (compatibilité avec clients modernes)

```bash
nc 127.0.0.1 6667
CAP LS
# Attendu : :ircserv CAP * LS :  (liste vide = pas de capacités)
CAP END
PASS secret
NICK alice
USER alice 0 * :Alice
# Registration complète malgré le CAP initial
```

**Prochain chapitre** : [08_canaux.md](08_canaux.md) — la classe `Channel` et les commandes de gestion des salons.
