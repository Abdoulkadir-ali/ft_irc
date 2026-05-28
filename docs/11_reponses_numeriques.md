# Phase 11 — Les Réponses Numériques IRC

> **Pré-requis** : avoir lu [05_parsing_messages.md](05_parsing_messages.md)  
> **Fichiers concernés** : [includes/Reply.hpp](../includes/Reply.hpp), [srcs/Reply.cpp](../srcs/Reply.cpp)

---

## 1. Concept de base — Les codes numériques IRC

Dans le protocole IRC, **le serveur ne répond pas en texte libre**. Il utilise des **codes numériques à 3 chiffres** pour toutes ses réponses formelles, qu'il s'agisse d'un succès (`RPL_`) ou d'une erreur (`ERR_`).

C'est comme les **codes HTTP** (200 OK, 404 Not Found, 500 Internal Server Error), mais pour IRC :

- `001–099` : messages de bienvenue / connexion
- `200–299` : réponses informatives
- `300–399` : réponses de canaux et d'état
- `400–499` : erreurs client
- `500–599` : erreurs serveur (rarement implémentées)

Exemples :
```
001 → Bienvenue sur le serveur
332 → Voici le topic du channel
401 → Ce nick n'existe pas
482 → Tu n'es pas opérateur
```

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Numeric reply** | Réponse du serveur avec un code à 3 chiffres (001, 332, 401...). |
| **RPL_xxx** | "Reply" : réponse positive ou informative. |
| **ERR_xxx** | "Error" : réponse négative, signale un problème. |
| **namespace Reply** | Espace de noms C++ regroupant toutes les fonctions de génération de réponses. |
| **nick_or_star** | Fonction interne qui renvoie le nick si connu, sinon `"*"` (avant la registration). |
| **trailing** | La partie après `:` dans un message IRC. Peut contenir des espaces. |
| **`numeric()`** | Fonction de bas niveau qui construit le format standard. |

---

## 3. Format d'une réponse numérique

```
:ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1\r\n
│         │   │                                            │
│         │   └─── le nick du destinataire                │
│         └──────── le code numérique (3 chiffres)        └─ texte (après ":")
└──────────────── le nom du serveur émetteur
```

Ce format est généré par la fonction centrale `Reply::numeric()` :

```cpp
// srcs/Reply.cpp

std::string numeric(const std::string &server, const std::string &code,
                    const std::string &nick, const std::string &text)
{
    return ":" + server + " " + code + " " + nick + " " + text + "\r\n";
    //     ────── préfixe ───   ─code─   ─nick─   ──────── texte ──────
}
```

Toutes les autres fonctions de `Reply::` appellent `numeric()` avec les bons arguments.

---

## 4. Objectif attendu dans ce projet

Le module `Reply` doit :
1. Fournir des fonctions prêtes à l'emploi pour chaque réponse numérique nécessaire.
2. Garantir que le **format** est toujours correct (`:server CODE nick :texte\r\n`).
3. Couvrir toutes les situations : bienvenue, infos de channel, et toutes les erreurs.

---

## 5. Pourquoi ces objectifs ?

### Pourquoi ne pas écrire les messages manuellement à chaque fois ?

La centralisation dans `Reply::` garantit :
- La **cohérence** du format (pas d'oubli de `\r\n`, du `:` devant le texte, etc.)
- La **maintenabilité** : changer le format en un seul endroit
- La **lisibilité** : `Reply::err_nicknameinuse(...)` est bien plus clair que construire la chaîne manuellement

### Pourquoi `nick_or_star()` ?

Avant que le client ait envoyé `NICK`, son nick est inconnu. Mais le protocole exige quand même un nick dans la réponse. La convention IRC est d'utiliser `"*"` comme placeholder. `nick_or_star()` gère automatiquement les deux cas.

```cpp
static std::string nick_or_star(const Client &c) {
    return c.hasNick() ? c.getNick() : std::string("*");
}
```

### Pourquoi le `namespace Reply` ?

En C++, un `namespace` regroupe des fonctions liées sous un même préfixe. Cela évite les conflits de noms et clarifie l'intention. `Reply::err_passwdmismatch()` indique clairement que c'est une réponse de type erreur, générée par le module Reply.

---

## 6. Catalogue complet des codes implémentés

### 6.1 — Messages de bienvenue (001–004)

Envoyés dans l'ordre quand le client est enregistré (après PASS+NICK+USER).

| Code | Nom | Contenu |
|------|-----|---------|
| `001` | `RPL_WELCOME` | "Welcome to the Internet Relay Network nick!user@host" |
| `002` | `RPL_YOURHOST` | "Your host is ircserv, running version 1.0" |
| `003` | `RPL_CREATED` | "This server was created today" |
| `004` | `RPL_MYINFO` | "ircserv 1.0 o itkol" (modes utilisateur et channel disponibles) |

```cpp
std::string rpl_welcome(const std::string &srv, const Client &c)
{
    return numeric(srv, "001", c.getNick(),
        ":Welcome to the Internet Relay Network " + c.getPrefix());
    // Ex : ":ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1\r\n"
}

std::string rpl_myinfo(const std::string &srv, const Client &c)
{
    return numeric(srv, "004", c.getNick(),
        srv + " 1.0 o itkol");
    // "o"     = modes utilisateur supportés (none dans ce projet)
    // "itkol" = modes de channel supportés : +i +t +k +o +l
}
```

### 6.2 — Informations de channel (3xx)

| Code | Nom | Quand ? |
|------|-----|---------|
| `324` | `RPL_CHANNELMODEIS` | Réponse à `MODE #chan` (sans modification) |
| `331` | `RPL_NOTOPIC` | Quand un channel n'a pas de topic |
| `332` | `RPL_TOPIC` | Le topic actuel du channel |
| `341` | `RPL_INVITING` | Confirmation d'une invitation (`INVITE`) |
| `353` | `RPL_NAMREPLY` | Liste des membres lors d'un JOIN |
| `366` | `RPL_ENDOFNAMES` | Fin de la liste des membres |

```cpp
// 331 : pas de topic
std::string rpl_notopic(const std::string &srv, const Client &c, const std::string &chan)
{
    return numeric(srv, "331", c.getNick(), chan + " :No topic is set");
    // Ex : ":ircserv 331 alice #general :No topic is set\r\n"
}

// 332 : topic existant
std::string rpl_topic(const std::string &srv, const Client &c,
                      const std::string &chan, const std::string &topic)
{
    return numeric(srv, "332", c.getNick(), chan + " :" + topic);
    // Ex : ":ircserv 332 alice #general :Bienvenue sur ce channel !\r\n"
}

// 353 : liste des membres
// Le "= " devant #channel signifie "public channel" (convention IRC)
std::string rpl_namreply(const std::string &srv, const Client &c,
                         const std::string &chan, const std::string &names)
{
    return numeric(srv, "353", c.getNick(), "= " + chan + " :" + names);
    // Ex : ":ircserv 353 alice = #general :@alice bob charlie\r\n"
    //                                       │      │    └─ membres normaux
    //                                       └── @ = opérateur
}

// 341 : confirmation d'invitation
std::string rpl_inviting(const std::string &srv, const Client &c,
                         const std::string &nick, const std::string &chan)
{
    return numeric(srv, "341", c.getNick(), nick + " " + chan);
    // Ex : ":ircserv 341 alice bob #general\r\n"
    //   → "alice, tu as bien invité bob dans #general"
}
```

### 6.3 — Erreurs d'existence (4xx — nick/channel introuvable)

| Code | Nom | Cause |
|------|-----|-------|
| `401` | `ERR_NOSUCHNICK` | Nick ou channel inexistant |
| `403` | `ERR_NOSUCHCHANNEL` | Nom de channel invalide |
| `404` | `ERR_CANNOTSENDTOCHAN` | Pas membre → ne peut pas écrire |
| `411` | `ERR_NORECIPIENT` | PRIVMSG sans destinataire |
| `412` | `ERR_NOTEXTTOSEND` | PRIVMSG sans texte |
| `421` | `ERR_UNKNOWNCOMMAND` | Commande inconnue |

```cpp
std::string err_nosuchnick(const std::string &srv, const Client &c, const std::string &nick)
{
    return numeric(srv, "401", nick_or_star(c), nick + " :No such nick/channel");
    // Ex : ":ircserv 401 alice zzz :No such nick/channel\r\n"
}

std::string err_unknowncommand(const std::string &srv, const Client &c, const std::string &cmd)
{
    return numeric(srv, "421", nick_or_star(c), cmd + " :Unknown command");
    // Ex : ":ircserv 421 alice FOOBAR :Unknown command\r\n"
}
```

### 6.4 — Erreurs de nick (43x)

| Code | Nom | Cause |
|------|-----|-------|
| `431` | `ERR_NONICKNAMEGIVEN` | NICK envoyé sans paramètre |
| `432` | `ERR_ERRONEUSNICKNAME` | Nick contient des caractères invalides |
| `433` | `ERR_NICKNAMEINUSE` | Le nick est déjà utilisé |

```cpp
std::string err_nicknameinuse(const std::string &srv, const Client &c, const std::string &nick)
{
    return numeric(srv, "433", nick_or_star(c), nick + " :Nickname is already in use");
    // Ex : ":ircserv 433 * alice :Nickname is already in use\r\n"
    //                   │
    //                   └─ "*" car le client n'a pas encore de nick
}
```

### 6.5 — Erreurs de membership (44x)

| Code | Nom | Cause |
|------|-----|-------|
| `441` | `ERR_USERNOTINCHANNEL` | KICK/MODE cible un nick absent du channel |
| `442` | `ERR_NOTONCHANNEL` | Le client n'est pas dans le channel |
| `443` | `ERR_USERONCHANNEL` | INVITE d'un nick déjà présent |

### 6.6 — Erreurs de registration et de paramètres (45x–46x)

| Code | Nom | Cause |
|------|-----|-------|
| `451` | `ERR_NOTREGISTERED` | Commande nécessitant l'inscription envoyée avant |
| `461` | `ERR_NEEDMOREPARAMS` | Paramètres manquants |
| `462` | `ERR_ALREADYREGISTERED` | PASS envoyé une deuxième fois |
| `464` | `ERR_PASSWDMISMATCH` | Mauvais mot de passe |

```cpp
std::string err_notregistered(const std::string &srv, const Client &c)
{
    return numeric(srv, "451", nick_or_star(c), ":You have not registered");
    // Ex : ":ircserv 451 * :You have not registered\r\n"
}

std::string err_passwdmismatch(const std::string &srv, const Client &c)
{
    return numeric(srv, "464", nick_or_star(c), ":Password incorrect");
}
```

### 6.7 — Erreurs de channel (47x–48x)

| Code | Nom | Cause |
|------|-----|-------|
| `471` | `ERR_CHANNELISFULL` | Channel plein (`+l`) |
| `472` | `ERR_UNKNOWNMODE` | Lettre de mode inconnue |
| `473` | `ERR_INVITEONLYCHAN` | Pas d'invitation pour channel `+i` |
| `475` | `ERR_BADCHANNELKEY` | Mauvais mot de passe (`+k`) |
| `482` | `ERR_CHANOPRIVSNEEDED` | Commande réservée aux opérateurs |

```cpp
std::string err_chanoprivsneeded(const std::string &srv, const Client &c, const std::string &chan)
{
    return numeric(srv, "482", nick_or_star(c), chan + " :You're not channel operator");
    // Ex : ":ircserv 482 alice #general :You're not channel operator\r\n"
}

std::string err_inviteonlychan(const std::string &srv, const Client &c, const std::string &chan)
{
    return numeric(srv, "473", nick_or_star(c), chan + " :Cannot join channel (+i)");
}
```

---

## 7. Vue d'ensemble : tableau récapitulatif complet

| Code | Préfixe | Nom | Module émetteur |
|------|---------|-----|-----------------|
| 001 | RPL | WELCOME | tryRegister() |
| 002 | RPL | YOURHOST | tryRegister() |
| 003 | RPL | CREATED | tryRegister() |
| 004 | RPL | MYINFO | tryRegister() |
| 324 | RPL | CHANNELMODEIS | cmd_mode() |
| 331 | RPL | NOTOPIC | cmd_join(), cmd_topic() |
| 332 | RPL | TOPIC | cmd_join(), cmd_topic() |
| 341 | RPL | INVITING | cmd_invite() |
| 353 | RPL | NAMREPLY | cmd_join() |
| 366 | RPL | ENDOFNAMES | cmd_join() |
| 401 | ERR | NOSUCHNICK | cmd_privmsg(), cmd_whois() |
| 403 | ERR | NOSUCHCHANNEL | cmd_join(), cmd_mode() |
| 404 | ERR | CANNOTSENDTOCHAN | cmd_privmsg() |
| 411 | ERR | NORECIPIENT | cmd_privmsg() |
| 412 | ERR | NOTEXTTOSEND | cmd_privmsg() |
| 421 | ERR | UNKNOWNCOMMAND | CommandDispatcher |
| 431 | ERR | NONICKNAMEGIVEN | cmd_nick() |
| 432 | ERR | ERRONEUSNICKNAME | cmd_nick() |
| 433 | ERR | NICKNAMEINUSE | cmd_nick() |
| 441 | ERR | USERNOTINCHANNEL | cmd_kick(), cmd_mode() |
| 442 | ERR | NOTONCHANNEL | cmd_part(), cmd_topic() |
| 443 | ERR | USERONCHANNEL | cmd_invite() |
| 451 | ERR | NOTREGISTERED | tous les handlers |
| 461 | ERR | NEEDMOREPARAMS | tous les handlers |
| 462 | ERR | ALREADYREGISTERED | cmd_pass() |
| 464 | ERR | PASSWDMISMATCH | cmd_pass() |
| 471 | ERR | CHANNELISFULL | cmd_join() |
| 472 | ERR | UNKNOWNMODE | cmd_mode() |
| 473 | ERR | INVITEONLYCHAN | cmd_join() |
| 475 | ERR | BADCHANNELKEY | cmd_join() |
| 482 | ERR | CHANOPRIVSNEEDED | cmd_mode(), cmd_kick(), cmd_topic() |

---

## 8. Schéma d'utilisation

```
Client envoie : "NICK alice_123_alice_long\r\n"
                         │
                    cmd_nick()
                         │
                  isValidNick() → false  (trop long : >9 chars)
                         │
          srv.sendTo(cli, Reply::err_erroneusnickname(srv.getName(), cli, "alice_123_alice_long"))
                         │
          Reply::numeric("ircserv", "432", "*", "alice_123_alice_long :Erroneous nickname")
                         │
          → ":ircserv 432 * alice_123_alice_long :Erroneous nickname\r\n"
                         │
          ajouté dans client._write_buf → envoyé au prochain POLLOUT
```

---

## 9. Récapitulatif

- Toutes les réponses du serveur sont des **chaînes de la forme** `:server CODE nick :texte\r\n`.
- La fonction `Reply::numeric()` est la brique de base qui assemble ce format.
- `nick_or_star()` gère le cas où le client n'a pas encore de nick (`"*"`).
- Les **RPL_** sont des succès/informations, les **ERR_** sont des erreurs.
- Le module `Reply` est **purement fonctionnel** : il ne fait que construire des chaînes, sans état.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Vérifier le format exact des réponses

Utiliser `cat -A` ou `hexdump` pour voir les `\r\n` réels :

```bash
nc 127.0.0.1 6667 | cat -A
PASS secret
NICK alice
USER alice 0 * :Alice
# Attendu : chaque ligne se termine par ^M$ (= \r\n)
# Exemple : :ircserv 001 alice :Welcome...^M$
```

### Vérification 2 — Le `*` avant la registration

```bash
nc 127.0.0.1 6667
PASS mauvais
# Attendu : :ircserv 464 * :Password incorrect
#                         ^ nick est * car NICK pas encore envoyé
```

### Vérification 3 — Toutes les erreurs courantes

```bash
# Après registration :
JOIN #inexistant_tres_long_xxxxxxxxxx   # nom valide mais crée le channel
KICK #test alice              # si pas op : 482
MODE #test +i                 # si pas op : 482
INVITE inexistant #test       # 401
TOPIC #vide                   # 403 No such channel
```

### Vérification 4 — RPL_NAMREPLY a le bon format

```bash
JOIN #test
# Attendu :
# :ircserv 353 alice = #test :@alice   (@ = op)
# :ircserv 366 alice #test :End of NAMES list
# Quand bob rejoint, la liste devient : @alice bob
```

**Prochain chapitre** : [12_bot.md](12_bot.md) — le bot intégré et la magie de `socketpair()`.
