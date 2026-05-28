# Phase 10 — La Messagerie : PRIVMSG et NOTICE

> **Pré-requis** : avoir lu [08_canaux.md](08_canaux.md) et [03_boucle_evenementielle.md](03_boucle_evenementielle.md)  
> **Fichiers concernés** : [srcs/commands/cmd_privmsg.cpp](../srcs/commands/cmd_privmsg.cpp), [srcs/Server.cpp](../srcs/Server.cpp) `sendTo()`, `enableWriteAll()`

---

## 1. Concept de base — Les messages IRC

Envoyer un message sur IRC, c'est l'opération la plus courante. Il existe deux commandes pour ça :

- **`PRIVMSG`** : message "normal", peut générer des erreurs si la cible n'existe pas.
- **`NOTICE`** : message "silencieux", **ne génère jamais d'erreur** même si la cible n'existe pas.

Chaque commande peut cibler soit :
- Un **channel** : le message est diffusé à tous les membres (sauf l'expéditeur).
- Un **utilisateur** : message direct (privé).

Analogie :
- `PRIVMSG` = envoyer une lettre recommandée (avec accusé de réception en cas d'échec).
- `NOTICE` = déposer un flyer dans la boîte aux lettres (sans notification d'échec).

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **PRIVMSG** | Commande IRC pour envoyer un message. Génère des erreurs si la cible est invalide. |
| **NOTICE** | Commande IRC similaire à PRIVMSG, mais silencieuse en cas d'erreur. |
| **target** | La cible du message : un channel (`#general`) ou un nick (`alice`). |
| **broadcast (channel)** | Envoyer le message à tous les membres du channel **sauf** l'expéditeur. |
| **direct (user)** | Envoyer directement dans le `write_buf` du destinataire. |
| **DCC SEND** | Un type spécial de PRIVMSG utilisé pour initier un transfert de fichier. |
| **CTCP** | Client-To-Client Protocol — encapsule des données spéciales dans un PRIVMSG avec `\x01` comme délimiteur. |
| **write_buf** | Tampon interne où les messages sont accumulés avant d'être envoyés via `send()`. |
| **POLLOUT** | Flag poll activé pour écrire les données accumulées dans le `write_buf`. |

---

## 3. Format d'un message PRIVMSG sur le réseau

```
# Client → Serveur (ce qu'alice envoie)
PRIVMSG #general :Bonjour tout le monde !\r\n

# Serveur → autres membres (ce que bob et charlie reçoivent)
:alice!alice@192.168.1.2 PRIVMSG #general :Bonjour tout le monde !\r\n
     │        │             │        │               │
     │        │             │        │               └─ le texte du message
     │        │             │        └─── le channel ciblé
     │        │             └──────────── la commande
     │        └─────────────────────────── alice@192.168.1.2 (user@host)
     └──────────────────────────────────── nick (alice)
```

Le préfixe `:alice!alice@192.168.1.2` est ajouté par le **serveur**. Alice ne l'écrit pas elle-même : le serveur l'insère pour identifier l'expéditeur.

---

## 4. Objectif attendu dans ce projet

`cmd_privmsg()` doit :
1. Vérifier que l'utilisateur est enregistré (sinon ERR_NOTREGISTERED).
2. Vérifier que la cible et le texte sont présents.
3. Pour un **channel** : diffuser à tous les membres sauf l'expéditeur.
4. Pour un **utilisateur** : envoyer directement, en interceptant le cas DCC SEND.
5. Envoyer les erreurs appropriées si la cible n'existe pas.

`cmd_notice()` doit faire la même chose, mais **sans erreur** si quelque chose échoue.

---

## 5. Pourquoi ces objectifs ?

### Pourquoi NOTICE ne génère-t-il jamais d'erreur ?

La convention IRC impose que `NOTICE` ne provoque jamais de réponse automatique. Cela évite les boucles infinies : si un bot reçoit une erreur `NOTICE` et y répond avec `NOTICE`, on risque un échange infini entre deux bots. La règle est simple : un `NOTICE` n'appelle jamais de réponse.

### Pourquoi l'expéditeur ne reçoit-il pas son propre message dans un channel ?

Parce que le client IRC l'affiche déjà localement dès qu'il est tapé. Si le serveur renvoyait aussi le message, l'utilisateur le verrait deux fois. Le paramètre `except = &cli` dans `broadcast()` sert précisément à exclure l'expéditeur.

### Pourquoi le préfixe est-il ajouté par le serveur ?

Parce que le client ne connaît pas son propre `user@host` réseau. C'est le serveur qui connaît l'adresse IP de connexion (`_host`) et peut construire le préfixe correct `nick!user@host`.

### Pourquoi intercepter DCC SEND dans PRIVMSG ?

DCC SEND est un protocole pour transférer des fichiers directement entre clients (peer-to-peer). En réseau privé avec NAT, les adresses IP internes ne sont pas accessibles depuis l'extérieur. Le serveur peut intercepter et relayer la connexion pour contourner ce problème. Cette fonctionnalité est détaillée dans [13_dcc_relay.md](13_dcc_relay.md).

---

## 6. Implémentation — Code détaillé

### 6.1 — `cmd_privmsg()` : vue d'ensemble

Fichier : [srcs/commands/cmd_privmsg.cpp](../srcs/commands/cmd_privmsg.cpp)

```cpp
void cmd_privmsg(Server &srv, Client &cli, const Message &msg)
{
    // ── Vérifications préliminaires ──────────────────────────────────────
    if (cli.getState() != Client::STATE_REGISTERED) {
        srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
        return;
    }
    if (msg.params.empty()) {
        // PRIVMSG sans aucun paramètre → ERR_NORECIPIENT
        srv.sendTo(cli, Reply::err_norecipient(srv.getName(), cli, "PRIVMSG"));
        return;
    }
    if (msg.params.size() < 2) {
        // PRIVMSG #chan (sans texte) → ERR_NOTEXTTOSEND
        srv.sendTo(cli, Reply::err_notexttosend(srv.getName(), cli));
        return;
    }

    const std::string &target = msg.params[0]; // "#general" ou "alice"
    const std::string &text   = msg.params[1]; // "Bonjour !"
    // ...
```

### 6.2 — Envoi dans un channel

```cpp
    // La cible est un channel si elle commence par '#' ou '&'
    if (target[0] == '#' || target[0] == '&') {
        Channel *chan = srv.findChannel(target);
        if (!chan) {
            // Channel inexistant
            srv.sendTo(cli, Reply::err_nosuchnick(srv.getName(), cli, target));
            return;
        }
        if (!chan->isMember(&cli)) {
            // L'expéditeur n'est pas dans le channel
            srv.sendTo(cli, Reply::err_cannotsendtochan(srv.getName(), cli, target));
            return;
        }

        // Construire le message complet avec le préfixe de l'expéditeur
        std::string fullMsg = ":" + cli.getPrefix() + " PRIVMSG " + target + " :" + text + "\r\n";
        //                      │                                               │
        //                      └─ ":alice!alice@host"                          └─ le texte

        // Diffuser à tout le channel, SAUF l'expéditeur
        chan->broadcast(fullMsg, &cli);           // accumule dans write_buf des membres
        srv.enableWriteAll(*chan, &cli);           // active POLLOUT sur tous (sauf alice)
    }
```

### 6.3 — Envoi direct à un utilisateur

```cpp
    } else {
        // La cible est un nick
        Client *dest = srv.findClientByNick(target);
        if (!dest) {
            srv.sendTo(cli, Reply::err_nosuchnick(srv.getName(), cli, target));
            return;
        }

        // Vérifier si c'est un DCC SEND (transfert de fichier)
        if (isDccSend(text) && srv.getDccRelay()) {
            std::string relayed = srv.getDccRelay()->intercept(cli, *dest, text);
            if (!relayed.empty()) {
                // Remplacer l'IP/port dans le message DCC par ceux du relai
                std::string fullMsg = ":" + cli.getPrefix() + " PRIVMSG "
                                    + target + " :" + relayed + "\r\n";
                srv.sendTo(*dest, fullMsg);
                return;  // message DCC modifié envoyé, on s'arrête ici
            }
        }

        // Message direct normal
        std::string fullMsg = ":" + cli.getPrefix() + " PRIVMSG " + target + " :" + text + "\r\n";
        srv.sendTo(*dest, fullMsg);  // accumule dans write_buf de dest
    }
```

### 6.4 — Détection du DCC SEND

```cpp
// Retourne true si le texte est un CTCP DCC SEND
static bool isDccSend(const std::string &text)
{
    return text.size() > 10
        && text[0] == '\x01'              // début du CTCP
        && text.substr(1, 8) == "DCC SEND"; // sous-commande DCC SEND
}
```

Un message DCC SEND ressemble à :
```
\x01DCC SEND fichier.zip 3232235778 1234 102400\x01
      │                  │           │    │
      │                  │           │    └─ taille en octets
      │                  │           └─ port d'écoute
      │                  └─ IP en entier (ex: 192.168.1.2 → 3232235778)
      └─ début CTCP
```

### 6.5 — `cmd_notice()` : version silencieuse

```cpp
void cmd_notice(Server &srv, Client &cli, const Message &msg)
{
    // Silencieux si non enregistré (pas d'erreur envoyée)
    if (cli.getState() != Client::STATE_REGISTERED)
        return;
    if (msg.params.size() < 2)
        return;  // pas d'erreur, juste ignorer

    const std::string &target = msg.params[0];
    const std::string &text   = msg.params[1];
    std::string fullMsg = ":" + cli.getPrefix() + " NOTICE " + target + " :" + text + "\r\n";

    if (target[0] == '#' || target[0] == '&') {
        Channel *chan = srv.findChannel(target);
        if (!chan || !chan->isMember(&cli))
            return;  // silencieux : pas d'erreur
        chan->broadcast(fullMsg, &cli);
        srv.enableWriteAll(*chan, &cli);
    } else {
        Client *dest = srv.findClientByNick(target);
        if (!dest)
            return;  // silencieux : pas d'erreur ERR_NOSUCHNICK
        srv.sendTo(*dest, fullMsg);
    }
}
```

### 6.6 — `srv.sendTo()` et `srv.enableWriteAll()` : le pipeline d'écriture

Ces deux fonctions sont le cœur du mécanisme de sortie. Elles ne **font pas** `send()` immédiatement : elles accumulent les données et arment `POLLOUT` pour que `handleWrite()` s'en charge.

```cpp
// Server::sendTo(client, message)
// → ajoute le message dans client._write_buf
// → active POLLOUT sur le fd du client dans _pfds[]
void Server::sendTo(Client &c, const std::string &msg) {
    c.appendToWrite(msg);     // accumule dans write_buf
    enableWrite(c);           // active POLLOUT sur ce fd
}

// Server::enableWriteAll(channel, except)
// → active POLLOUT sur tous les membres du channel, sauf "except"
void Server::enableWriteAll(Channel &chan, Client *except) {
    const std::map<int, Client *> &m = chan.getMembers();
    for (auto it = m.begin(); it != m.end(); ++it) {
        if (except && it->second == except) continue;
        enableWrite(*it->second);  // POLLOUT activé
    }
}
```

```
                PRIVMSG reçu
                     │
                cmd_privmsg()
                     │
          ┌──────────┴──────────┐
          │ channel              │ user
          │                     │
    broadcast(msg, &cli)    sendTo(*dest, msg)
          │                     │
    write_buf +=           write_buf +=
    (pour chaque membre)   (pour dest)
          │                     │
    enableWriteAll()       enableWrite()
          │                     │
    POLLOUT armé           POLLOUT armé
          │
          ▼
    poll() détecte POLLOUT
          │
    handleWrite() → send()  ← données envoyées au client TCP
```

---

## 7. Schéma d'une session PRIVMSG channel complète

```
[alice] → Serveur : "PRIVMSG #general :Bonjour !\r\n"

Serveur :
  1. tokenize → Message{cmd:"PRIVMSG", params:["#general", "Bonjour !"]}
  2. dispatch → cmd_privmsg(srv, alice, msg)
  3. findChannel("#general") → chan (bob + charlie sont membres)
  4. chan->isMember(&alice) → true
  5. fullMsg = ":alice!alice@192.168.1.2 PRIVMSG #general :Bonjour !\r\n"
  6. broadcast(fullMsg, &alice)
     → bob._write_buf   += fullMsg
     → charlie._write_buf += fullMsg
     (alice n'est PAS dans la diffusion)
  7. enableWriteAll(chan, &alice)
     → bob._pfds[i].events |= POLLOUT
     → charlie._pfds[i].events |= POLLOUT

[poll() retourne POLLOUT pour bob et charlie]
  8. handleWrite(bob)   → send(bob.fd, fullMsg)   → bob voit le message
  9. handleWrite(charlie) → send(charlie.fd, fullMsg) → charlie voit le message

Alice ne reçoit PAS son propre message (broadcast exclu alice).
```

---

## 8. Récapitulatif

| Commande | Cible | Erreurs possibles | Comportement spécial |
|----------|-------|-------------------|---------------------|
| `PRIVMSG #chan :texte` | Channel | ERR_NOSUCHNICK, ERR_CANNOTSENDTOCHAN | broadcast sauf expéditeur |
| `PRIVMSG nick :texte` | Utilisateur | ERR_NOSUCHNICK | interception DCC SEND |
| `NOTICE #chan :texte` | Channel | Aucune | broadcast sauf expéditeur |
| `NOTICE nick :texte` | Utilisateur | Aucune | direct, silencieux |

- Le message est **construit par le serveur** avec le préfixe `:nick!user@host`.
- Les données sont **accumulées dans `write_buf`** et envoyées au prochain tour de `poll()`.
- `NOTICE` ne génère **jamais** de réponse automatique.

---

## Moyen de vérifier que ça marche

> Ouvrir deux terminaux nc, alice et bob tous les deux dans `#test`.

### Vérification 1 — PRIVMSG vers un channel

```bash
# Alice :
PRIVMSG #test :Bonjour tout le monde !
# Bob voit : :alice!alice@127.0.0.1 PRIVMSG #test :Bonjour tout le monde !
# Alice ne reçoit PAS son propre message (pas de doublon)
```

### Vérification 2 — PRIVMSG privé (direct)

```bash
# Alice à Bob :
PRIVMSG bob :Message privé
# Bob voit : :alice!alice@127.0.0.1 PRIVMSG bob :Message privé
# Les autres membres du channel ne voient rien
```

### Vérification 3 — PRIVMSG vers cible inexistante

```bash
PRIVMSG inexistant :test
# Attendu : :ircserv 401 alice inexistant :No such nick/channel

PRIVMSG #inexistant :test
# Attendu : :ircserv 401 alice #inexistant :No such nick/channel
```

### Vérification 4 — NOTICE sans erreur

```bash
NOTICE inexistant :test
# Attendu : silence total (pas de 401, aucune réponse)
```

### Vérification 5 — PRIVMSG hors channel

```bash
# Alice pas dans #test :
PRIVMSG #test :test
# Attendu : :ircserv 404 alice #test :Cannot send to channel
```

**Prochain chapitre** : [11_reponses_numeriques.md](11_reponses_numeriques.md) — les codes numériques IRC (001, 331, 401...).
