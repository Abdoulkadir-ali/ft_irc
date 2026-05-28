# Phase 04 — Gestion des Clients

> **Pré-requis** : avoir lu [03_boucle_evenementielle.md](03_boucle_evenementielle.md)  
> **Fichiers concernés** : [includes/Client.hpp](../includes/Client.hpp), [srcs/Client.cpp](../srcs/Client.cpp)

---

## 1. Concept de base — Qu'est-ce qu'un "client" côté serveur ?

Quand quelqu'un lance irssi et se connecte à notre serveur, une connexion TCP est établie. Du côté du serveur, cette connexion est **une prise réseau** (un file descriptor) plus **un état** (qui est cette personne ? est-elle authentifiée ? dans quels salons ?).

La classe `Client` est le **modèle** de cet état. Elle ne fait pas de réseau elle-même — c'est `Server` qui lit et écrit sur le socket. `Client` se contente de stocker :
- Le fd du socket
- L'identité de l'utilisateur (nick, user, realname, host)
- L'état d'authentification
- Les buffers de lecture et d'écriture
- La liste des channels rejoints

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Nick** | Le pseudonyme choisi par l'utilisateur (ex : `alice`). Visible dans les channels. Modifiable en cours de session. |
| **User** | Le nom d'utilisateur système envoyé par le client IRC (ex : `alice42`). Généralement le login Unix. Pas modifiable après connexion. |
| **Realname** | Le "vrai nom" libre que l'utilisateur saisit (ex : `Alice Dupont`). Affiché dans `/whois`. |
| **Host** | L'adresse IP du client, obtenue lors de `accept()`. Utilisée dans le préfixe des messages. |
| **Préfixe (prefix)** | Chaîne `nick!user@host` identifiant l'auteur d'un message. Format IRC standard. Exemple : `alice!alice42@192.168.1.5` |
| **État (State)** | Phase d'authentification dans laquelle se trouve le client. |
| **Read buffer** | Accumulation des données reçues du client, pas encore traitées. |
| **Write buffer** | Accumulation des messages à envoyer au client, pas encore expédiés. |

---

## 3. Les quatre états d'un client

La machine à états d'un `Client` :

```
    Connexion TCP
         │
         ▼
    STATE_NEW          ← Connexion établie, rien reçu encore
         │
         │ cmd_pass() vérifie le mot de passe → OK
         ▼
    STATE_PASS_OK      ← Mot de passe validé
         │
         │ tryRegister() : PASS + NICK + USER tous reçus
         ▼
    STATE_REGISTERED   ← Authentification complète, peut tout faire
         │
         │ Erreur réseau / cmd_quit() / kick...
         ▼
    STATE_DEAD         ← Marqué pour déconnexion
```

```cpp
// Déclaration dans includes/Client.hpp
enum State {
    STATE_NEW,         // 0 — vient de se connecter
    STATE_PASS_OK,     // 1 — PASS validé
    STATE_REGISTERED,  // 2 — pleinement authentifié
    STATE_DEAD         // 3 — marqué pour déconnexion
};
```

---

## 4. Objectif attendu dans ce projet

La classe `Client` doit :
1. Stocker toutes les informations d'identité d'un utilisateur IRC.
2. Gérer les **buffers I/O** pour décorréler la lecture réseau du traitement logique.
3. Tracker les **channels rejoints** (en minuscules pour la comparaison insensible à la casse).
4. Exposer une **interface propre** utilisée par `Server` et les handlers de commandes.

---

## 5. Pourquoi ces objectifs ?

### Pourquoi des buffers séparés par client ?

TCP peut fragmenter les données. Un message `"PRIVMSG #general :Salut\r\n"` de 27 octets peut arriver en deux morceaux :
- Premier `recv()` : `"PRIVMSG #general :Sa"`
- Deuxième `recv()` : `"lut\r\n"`

Sans buffer, on ne pourrait pas traiter le message (incomplet). On accumule dans `_read_buf` jusqu'à voir `\r\n`.

De même, `send()` peut n'envoyer qu'une partie des données. On garde le reste dans `_write_buf` pour le prochain tour de `poll()`.

### Pourquoi stocker les channels en minuscules ?

IRC est **insensible à la casse** pour les noms de channels et de nicks. `#General` et `#general` désignent le même salon. En stockant en minuscules, toutes les comparaisons deviennent simples : `toLower(name) == toLower(other)`.

### Pourquoi `getPrefix()` est-il si important ?

Le préfixe `nick!user@host` est inclus dans chaque message envoyé au canal. C'est ainsi que les clients IRC savent **qui** a écrit quoi. Exemple :

```
:alice!alice42@192.168.1.5 PRIVMSG #general :Bonjour tout le monde
 └── prefix ──────────────┘ └── command ──┘ └── params ──────────┘
```

---

## 6. Implémentation — Code détaillé

### 6.1 — Constructeur et membres

Fichier : [srcs/Client.cpp](../srcs/Client.cpp)

```cpp
Client::Client(int fd, const std::string &host)
    : _fd(fd),              // le file descriptor du socket
      _state(STATE_NEW),    // état initial : pas encore authentifié
      _host(host)           // l'IP source du client (ex: "192.168.1.5")
    // _nick, _user, _realname : chaînes vides par défaut
    // _read_buf, _write_buf  : chaînes vides par défaut
    // _channels              : set vide par défaut
{}
```

> **Remarque** : `_fd` et `_state` sont les deux membres les plus critiques. Tout le reste peut être vide jusqu'à la registration.

### 6.2 — Le préfixe IRC

```cpp
// Retourne la chaîne "nick!user@host"
// Utilisée dans tous les messages diffusés au canal
std::string Client::getPrefix() const
{
    return _nick + "!" + _user + "@" + _host;
}
```

Exemple : si `_nick = "alice"`, `_user = "alice42"`, `_host = "192.168.1.5"` :
- `getPrefix()` retourne `"alice!alice42@192.168.1.5"`

Ce préfixe est ensuite utilisé dans les handlers :

```cpp
// Dans cmd_join.cpp : informer tout le channel qu'alice a rejoint
std::string joinMsg = ":" + cli.getPrefix() + " JOIN :" + chan->getName() + "\r\n";
// Produit : ":alice!alice42@192.168.1.5 JOIN :#general\r\n"
chan->broadcast(joinMsg, NULL);
```

### 6.3 — Vérifications d'état

```cpp
// Ces deux booléens servent dans tryRegister() pour savoir
// si la registration peut être complétée
bool Client::hasNick() const { return !_nick.empty(); }
bool Client::hasUser() const { return !_user.empty(); }
```

### 6.4 — Les buffers I/O

```cpp
std::string &Client::readBuf()  { return _read_buf; }
std::string &Client::writeBuf() { return _write_buf; }

void Client::appendToWrite(const std::string &data)
{
    _write_buf += data;
}
```

Ces méthodes retournent des **références** (pas des copies), ce qui permet à `Server` de modifier directement les buffers :

```cpp
// Dans Server::handleRead() :
client.readBuf().append(buf, n); // ajoute directement dans _read_buf

// Dans Server::handleWrite() :
client.writeBuf().erase(0, n);   // supprime directement ce qui a été envoyé
```

### 6.5 — Gestion des channels

```cpp
void Client::addChannel(const std::string &name)
{
    // toLower : car IRC est insensible à la casse pour les channels
    // ex: addChannel("#General") stocke "#general"
    _channels.insert(Utils::toLower(name));
}

void Client::removeChannel(const std::string &name)
{
    _channels.erase(Utils::toLower(name));
}

const std::set<std::string> &Client::getChannels() const
{
    return _channels;
}
```

Le `std::set<std::string>` garantit :
- Pas de doublons (impossible d'être dans le même channel deux fois)
- Itération ordonnée alphabétiquement
- Recherche en O(log n)

### 6.6 — Exemple de cycle complet d'un client

```
1. Connexion TCP :
   Client alice(fd=6, host="192.168.1.5")
   → _state = STATE_NEW

2. Alice envoie "PASS monpassword\r\n" :
   cmd_pass() vérifie le mot de passe → OK
   cli.setState(STATE_PASS_OK)
   srv.tryRegister(cli)
   → mais hasNick() = false → pas encore complet

3. Alice envoie "NICK alice\r\n" :
   cmd_nick() → cli.setNick("alice")
   srv.tryRegister(cli)
   → STATE_PASS_OK ET hasNick() mais pas hasUser() → pas encore complet

4. Alice envoie "USER alice42 0 * :Alice Dupont\r\n" :
   cmd_user() → cli.setUser("alice42"), cli.setRealName("Alice Dupont")
   srv.tryRegister(cli)
   → STATE_PASS_OK ET hasNick() ET hasUser() → COMPLET !
   cli.setState(STATE_REGISTERED)
   sendTo(cli, "001 RPL_WELCOME...")

5. Alice envoie "JOIN #general\r\n" :
   cmd_join() → chan->addMember(&cli)
                cli.addChannel("#general")
   → _channels = {"#general"}

6. Alice ferme irssi :
   recv() retourne 0 (FIN TCP)
   _dead_fds.insert(fd=6)
   → reapDisconnected() → disconnectClient(6)
   → removeClientFromAllChannels(*alice)
      → cli.getChannels() = {"#general"}
      → chan->removeMember(&alice)
      → cli.removeChannel("#general")
   → delete alice
```

---

## 7. Récapitulatif

| Membre | Type | Rôle |
|--------|------|------|
| `_fd` | `int` | Socket réseau du client |
| `_state` | `enum State` | Phase d'authentification |
| `_nick` | `string` | Pseudonyme (modifiable) |
| `_user` | `string` | Nom d'utilisateur (fixe après USER) |
| `_realname` | `string` | Nom réel libre (fixe après USER) |
| `_host` | `string` | Adresse IP source (fixe à la connexion) |
| `_read_buf` | `string` | Données reçues pas encore parsées |
| `_write_buf` | `string` | Données à envoyer pas encore expédiées |
| `_channels` | `set<string>` | Channels rejoints (en minuscules) |

- La classe `Client` est un **conteneur d'état** passif : c'est `Server` qui lit/écrit, `Client` qui stocke.
- Les buffers découplent le réseau du traitement : on accumule, on traite quand c'est complet.
- L'état (`State`) contrôle ce que le client peut faire : impossible d'utiliser `JOIN` avant d'être `STATE_REGISTERED`.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Les commandes avant registration sont refusées

```bash
nc 127.0.0.1 6667
JOIN #test   # sans s'être authentifié
# Attendu : :ircserv 451 * :You have not registered
```

### Vérification 2 — Le buffer gère les messages fragmentés

```bash
# Envoyer un message en deux morceaux avec un délai
(printf 'PASS sec'; sleep 0.5; printf 'ret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n') | nc 127.0.0.1 6667
# Attendu : registration complète malgré la fragmentation
# (le serveur ne doit pas crasher ou ignorer la commande)
```

### Vérification 3 — Le préfixe nick!user@host est correct

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice42 0 * :Alice
JOIN #test
# Depuis un autre client dans #test, alice envoie un PRIVMSG
# Attendu dans l'autre client :
# :alice!alice42@127.0.0.1 PRIVMSG #test :...
```

### Vérification 4 — Changement de nick en cours de session

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice
NICK alice2
# Attendu : :alice!alice@127.0.0.1 NICK :alice2
# Les autres membres du channel partagé voient aussi ce message
```

**Prochain chapitre** : [05_parsing_messages.md](05_parsing_messages.md) — comment transformer `"JOIN #general\r\n"` en structure utilisable.
