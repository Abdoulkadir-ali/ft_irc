# Phase 08 — Les Canaux (Channels)

> **Pré-requis** : avoir lu [07_registration.md](07_registration.md)  
> **Fichiers concernés** : [includes/Channel.hpp](../includes/Channel.hpp), [srcs/Channel.cpp](../srcs/Channel.cpp), [srcs/commands/cmd_join.cpp](../srcs/commands/cmd_join.cpp), [srcs/commands/cmd_part.cpp](../srcs/commands/cmd_part.cpp), [srcs/commands/cmd_topic.cpp](../srcs/commands/cmd_topic.cpp), [srcs/commands/cmd_kick.cpp](../srcs/commands/cmd_kick.cpp), [srcs/commands/cmd_invite.cpp](../srcs/commands/cmd_invite.cpp)

---

## 1. Concept de base — Qu'est-ce qu'un channel IRC ?

Un **channel** (ou canal) est un espace de discussion partagé, identifié par un nom commençant par `#` ou `&` (ex : `#general`, `#random`, `&local`).

- `#` = channel global (accessible sur tout le réseau IRC, en théorie)
- `&` = channel local (uniquement sur ce serveur)

Analogie : un channel est une **salle de réunion** dans un immeuble :
- Plusieurs personnes peuvent y entrer (`JOIN`)
- N'importe qui peut parler (`PRIVMSG`)
- Il y a un ou plusieurs responsables : les **opérateurs** (`@`)
- La salle peut être **fermée** (invite-only), avoir un **code d'accès** (key), une **limite de places**...
- Tout ce qui est dit s'entend par tous ceux qui sont dans la pièce (broadcast)

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Channel** | Un salon de discussion identifié par son nom (commence par `#` ou `&`). |
| **Opérateur (op)** | Membre avec privilèges : peut changer les modes, expulser (kick), inviter. Symbolisé par `@` devant le nick dans la liste des membres. |
| **Topic** | Le sujet du channel, affiché quand on rejoint. Modifiable par les ops (si mode `+t`). |
| **Key** | Mot de passe d'accès au channel (mode `+k`). |
| **User limit** | Nombre maximum de membres (mode `+l`). |
| **Invite-only** | Mode `+i` : pour rejoindre, il faut avoir été invité par un op. |
| **broadcast** | Envoyer un message à tous les membres du channel. |
| **JOIN 0** | Commande spéciale pour quitter tous les channels d'un coup. |
| **NAMES list** | Liste des membres du channel envoyée quand on le rejoint. Les ops sont préfixés par `@`. |
| **RPL_NAMREPLY (353)** | Réponse numérique contenant la liste des membres. |

---

## 3. Structure de la classe Channel

Fichier : [includes/Channel.hpp](../includes/Channel.hpp)

```cpp
class Channel {
    std::string             _name;             // "#general" (casse originale)
    std::string             _topic;            // le sujet du channel
    std::string             _key;              // mot de passe (vide = pas de clé)
    int                     _user_limit;       // 0 = pas de limite
    bool                    _invite_only;      // mode +i
    bool                    _topic_restricted; // mode +t
    std::map<int, Client*>  _members;          // fd → Client* (membres)
    std::set<int>           _operators;        // fds des opérateurs
    std::set<std::string>   _invited;          // nicks invités (minuscules)
};
```

---

## 4. Objectif attendu dans ce projet

Les commandes de channel doivent :
- `JOIN` : rejoindre/créer un channel, envoyer topic + liste des membres.
- `PART` : quitter un channel, supprimer le channel s'il est vide.
- `TOPIC` : lire ou modifier le sujet (restreint aux ops si mode `+t`).
- `KICK` : expulser un membre (ops uniquement).
- `INVITE` : inviter quelqu'un (nécessaire si mode `+i`).

---

## 5. Pourquoi ces objectifs ?

### Pourquoi le premier membre devient-il opérateur ?

Sans cette règle, un channel créé serait un espace sans responsable. Personne ne pourrait gérer les modes, expulser des membres indésirables, etc. Donner le statut d'op au créateur est la convention IRC standard.

### Pourquoi supprimer le channel s'il est vide ?

Garder des channels vides consomme de la mémoire inutilement. Sur un vrai serveur avec des milliers de channels, cela peut devenir significatif. De plus, cela correspond au comportement standard d'IRC.

### Pourquoi la liste d'invités est-elle maintenue ?

Quand une personne est invitée mais n'a pas encore rejoint, elle doit être "mémorisée" pour pouvoir passer le filtre `+i` quand elle essaiera de JOIN. La liste `_invited` sert de "liste de souhaits" provisoire.

### Pourquoi `broadcast()` exclut-il un membre optionnel ?

Dans certains cas, on ne veut pas envoyer le message à l'émetteur lui-même (par exemple pour `PRIVMSG` dans un channel, l'envoyeur ne reçoit pas son propre message). Le paramètre `except` gère ce cas.

---

## 6. Implémentation — Code détaillé

### 6.1 — `Channel::broadcast()` : envoyer à tous les membres

Fichier : [srcs/Channel.cpp](../srcs/Channel.cpp)

```cpp
void Channel::broadcast(const std::string &message, Client *except)
{
    // Parcourir tous les membres du channel
    for (std::map<int, Client *>::const_iterator it = _members.begin();
         it != _members.end(); ++it) {

        // Ignorer le membre passé en paramètre (ex: l'expéditeur)
        if (except && it->second == except)
            continue;

        // Ajouter le message dans le write_buf de chaque membre
        // Le message sera envoyé par handleWrite() au prochain tour de poll()
        it->second->appendToWrite(message);
    }
}
```

> **Remarque** : `broadcast()` n'envoie pas directement via `send()`. Elle accumule dans les `write_buf`. C'est `Server::enableWriteAll()` qui active ensuite `POLLOUT` sur tous ces membres.

### 6.2 — `getModeString()` : représentation textuelle des modes actifs

```cpp
std::string Channel::getModeString() const
{
    std::string modes = "+";
    std::string params;           // les paramètres vont après les lettres de mode

    if (_invite_only)    modes += "i";              // pas de paramètre
    if (_topic_restricted) modes += "t";            // pas de paramètre
    if (!_key.empty()) { modes += "k"; params += " " + _key; }      // param: la clé
    if (_user_limit > 0) { modes += "l"; params += " " + Utils::intToStr(_user_limit); } // param: limite

    if (modes == "+") return "+";  // aucun mode actif
    return modes + params;
    // Exemples : "+it", "+ikl monpass 10", "+"
}
```

### 6.3 — `cmd_join()` : rejoindre un channel

Fichier : [srcs/commands/cmd_join.cpp](../srcs/commands/cmd_join.cpp)

```cpp
// Sous-fonction : rejoindre UN channel avec une clé optionnelle
static void joinOneChannel(Server &srv, Client &cli,
                            const std::string &chanName, const std::string &key)
{
    // ── Validation du nom ───────────────────────────────────────────────
    if (!Utils::isValidChannelName(chanName)) {
        srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, chanName));
        return;
    }

    Channel *chan = srv.findChannel(chanName);
    bool isNew = false;

    if (!chan) {
        // Le channel n'existe pas → le créer
        chan = srv.createChannel(chanName);
        isNew = true;
    }

    if (chan->isMember(&cli)) return; // déjà membre → silencieux

    // ── Vérifications d'accès ───────────────────────────────────────────
    // Mode +i : invite-only
    if (chan->isInviteOnly() && !chan->isInvited(cli.getNick())) {
        srv.sendTo(cli, Reply::err_inviteonlychan(srv.getName(), cli, chanName));
        return;
    }

    // Mode +k : clé requise
    if (chan->hasKey() && key != chan->getKey()) {
        srv.sendTo(cli, Reply::err_badchannelkey(srv.getName(), cli, chanName));
        return;
    }

    // Mode +l : limite de membres
    if (chan->hasUserLimit() && chan->memberCount() >= chan->getUserLimit()) {
        srv.sendTo(cli, Reply::err_channelisfull(srv.getName(), cli, chanName));
        return;
    }

    // ── Rejoindre ───────────────────────────────────────────────────────
    chan->addMember(&cli);
    cli.addChannel(chanName);

    // Premier membre → opérateur automatique
    if (isNew)
        chan->addOperator(&cli);

    // Retirer de la liste d'invités une fois qu'on a rejoint
    chan->removeInvited(cli.getNick());

    // ── Diffusion du JOIN à tout le channel ─────────────────────────────
    // Tous les membres (incluant le nouveau) voient le JOIN
    std::string joinMsg = ":" + cli.getPrefix() + " JOIN :" + chan->getName() + "\r\n";
    chan->broadcast(joinMsg, NULL);   // NULL = envoyer à TOUS
    srv.enableWriteAll(*chan, NULL);

    // ── Envoyer le topic et la liste des membres au nouveau ─────────────
    if (!chan->getTopic().empty())
        srv.sendTo(cli, Reply::rpl_topic(srv.getName(), cli, chan->getName(), chan->getTopic()));
    else
        srv.sendTo(cli, Reply::rpl_notopic(srv.getName(), cli, chan->getName()));

    // Construire la liste des membres (@ = op)
    std::string names;
    const std::map<int, Client *> &members = chan->getMembers();
    for (std::map<int, Client *>::const_iterator it = members.begin();
         it != members.end(); ++it) {
        if (!names.empty()) names += " ";
        if (chan->isOperator(it->second)) names += "@"; // préfixe opérateur
        names += it->second->getNick();
    }
    srv.sendTo(cli, Reply::rpl_namreply(srv.getName(), cli, chan->getName(), names));
    srv.sendTo(cli, Reply::rpl_endofnames(srv.getName(), cli, chan->getName()));
}

void cmd_join(Server &srv, Client &cli, const Message &msg)
{
    if (cli.getState() != Client::STATE_REGISTERED) {
        srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
        return;
    }
    if (msg.params.empty()) {
        srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "JOIN"));
        return;
    }

    // ── Cas spécial : JOIN 0 → quitter tous les channels ────────────────
    if (msg.params[0] == "0") {
        std::set<std::string> chans = cli.getChannels();
        for (std::set<std::string>::iterator it = chans.begin();
             it != chans.end(); ++it) {
            Channel *chan = srv.findChannel(*it);
            if (!chan) continue;
            std::string partMsg = ":" + cli.getPrefix() + " PART "
                                + chan->getName() + " :Left all channels\r\n";
            chan->broadcast(partMsg, NULL);
            srv.enableWriteAll(*chan, NULL);
            chan->removeMember(&cli);
            cli.removeChannel(*it);
            if (chan->memberCount() == 0) srv.deleteChannel(*it);
        }
        return;
    }

    // ── JOIN normal : peut rejoindre plusieurs channels à la fois ────────
    // Syntaxe : JOIN #a,#b,#c key1,key2,key3
    std::vector<std::string> channels = Utils::splitCSV(msg.params[0]);
    std::vector<std::string> keys;
    if (msg.params.size() > 1)
        keys = Utils::splitCSV(msg.params[1]);

    for (size_t i = 0; i < channels.size(); ++i) {
        std::string key = (i < keys.size()) ? keys[i] : "";
        joinOneChannel(srv, cli, channels[i], key);
    }
}
```

### 6.4 — `cmd_part()` : quitter un channel

Fichier : [srcs/commands/cmd_part.cpp](../srcs/commands/cmd_part.cpp)

```cpp
void cmd_part(Server &srv, Client &cli, const Message &msg)
{
    // Peut quitter plusieurs channels : PART #a,#b :raison
    std::vector<std::string> channels = Utils::splitCSV(msg.params[0]);
    std::string reason = (msg.params.size() > 1) ? msg.params[1] : "";

    for (size_t i = 0; i < channels.size(); ++i) {
        Channel *chan = srv.findChannel(channels[i]);
        if (!chan) {
            srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, channels[i]));
            continue;
        }
        if (!chan->isMember(&cli)) {
            srv.sendTo(cli, Reply::err_notonchannel(srv.getName(), cli, channels[i]));
            continue;
        }

        // Diffuser PART à tout le channel (l'émetteur inclus)
        std::string partMsg = ":" + cli.getPrefix() + " PART " + chan->getName();
        if (!reason.empty()) partMsg += " :" + reason;
        partMsg += "\r\n";
        chan->broadcast(partMsg, NULL);
        srv.enableWriteAll(*chan, NULL);

        // Retirer du channel
        chan->removeMember(&cli);
        cli.removeChannel(channels[i]);

        // Supprimer le channel s'il est vide
        if (chan->memberCount() == 0)
            srv.deleteChannel(channels[i]);
    }
}
```

### 6.5 — `cmd_topic()` : lire ou modifier le sujet

Fichier : [srcs/commands/cmd_topic.cpp](../srcs/commands/cmd_topic.cpp)

```cpp
void cmd_topic(Server &srv, Client &cli, const Message &msg)
{
    Channel *chan = srv.findChannel(msg.params[0]);
    // ... vérifications chan != NULL et isMember ...

    // Sans deuxième paramètre → lire le topic
    if (msg.params.size() < 2) {
        if (chan->getTopic().empty())
            srv.sendTo(cli, Reply::rpl_notopic(srv.getName(), cli, chan->getName()));
        else
            srv.sendTo(cli, Reply::rpl_topic(srv.getName(), cli, chan->getName(),
                                             chan->getTopic()));
        return;
    }

    // Avec deuxième paramètre → modifier le topic
    // Restreint aux ops si mode +t est actif
    if (chan->isTopicRestricted() && !chan->isOperator(&cli)) {
        srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chan->getName()));
        return;
    }

    chan->setTopic(msg.params[1]);

    // Diffuser le nouveau topic à tous les membres
    std::string topicMsg = ":" + cli.getPrefix() + " TOPIC "
                         + chan->getName() + " :" + msg.params[1] + "\r\n";
    chan->broadcast(topicMsg, NULL);
    srv.enableWriteAll(*chan, NULL);
}
```

### 6.6 — `cmd_kick()` : expulser un membre

Fichier : [srcs/commands/cmd_kick.cpp](../srcs/commands/cmd_kick.cpp)

```cpp
void cmd_kick(Server &srv, Client &cli, const Message &msg)
{
    // KICK #channel nick :raison
    const std::string &chanName  = msg.params[0];
    const std::string &targetNick = msg.params[1];
    std::string reason = (msg.params.size() > 2) ? msg.params[2] : cli.getNick();
    // Par convention : raison par défaut = nick de l'opérateur qui kick

    Channel *chan = srv.findChannel(chanName);
    // ... vérifications ...

    // Seul un opérateur peut kick
    if (!chan->isOperator(&cli)) {
        srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chanName));
        return;
    }

    Client *target = srv.findClientByNick(targetNick);
    if (!target || !chan->isMember(target)) {
        srv.sendTo(cli, Reply::err_usernotinchannel(srv.getName(), cli, targetNick, chanName));
        return;
    }

    // Diffuser le KICK à tout le channel (la victime reçoit aussi le message)
    std::string kickMsg = ":" + cli.getPrefix() + " KICK " + chan->getName()
                        + " " + target->getNick() + " :" + reason + "\r\n";
    chan->broadcast(kickMsg, NULL);
    srv.enableWriteAll(*chan, NULL);

    // Retirer la victime du channel
    chan->removeMember(target);
    target->removeChannel(chanName);

    if (chan->memberCount() == 0) srv.deleteChannel(chanName);
}
```

### 6.7 — `cmd_invite()` : inviter quelqu'un

Fichier : [srcs/commands/cmd_invite.cpp](../srcs/commands/cmd_invite.cpp)

```cpp
void cmd_invite(Server &srv, Client &cli, const Message &msg)
{
    // INVITE alice #general
    const std::string &targetNick = msg.params[0];
    const std::string &chanName   = msg.params[1];

    Channel *chan = srv.findChannel(chanName);
    // ... vérifications : canal existe, on en est membre ...

    // En mode +i : seuls les ops peuvent inviter
    if (chan->isInviteOnly() && !chan->isOperator(&cli)) {
        srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chanName));
        return;
    }

    Client *target = srv.findClientByNick(targetNick);
    if (!target) {
        srv.sendTo(cli, Reply::err_nosuchnick(srv.getName(), cli, targetNick));
        return;
    }
    if (chan->isMember(target)) {
        srv.sendTo(cli, Reply::err_useronchannel(srv.getName(), cli, targetNick, chanName));
        return;
    }

    // Ajouter à la liste d'invités du channel
    chan->addInvited(targetNick); // stocké en minuscules

    // Confirmer à l'invitant
    srv.sendTo(cli, Reply::rpl_inviting(srv.getName(), cli, target->getNick(), chan->getName()));

    // Prévenir la personne invitée (elle verra "alice vous invite dans #general")
    srv.sendTo(*target, ":" + cli.getPrefix() + " INVITE "
                       + target->getNick() + " :" + chan->getName() + "\r\n");
}
```

---

## 7. Schéma d'une session JOIN complète

```
Alice tape /join #general

Client → Serveur : "JOIN #general\r\n"
                │
                ▼ cmd_join()
                │
                ├── #general n'existe pas → createChannel("#general")
                │     → _channels["#general"] = new Channel("#general")
                │
                ├── chan->addMember(&alice)
                ├── alice.addChannel("#general")
                ├── chan->addOperator(&alice)  ← premier membre = op
                │
                ├── broadcast(":alice!... JOIN :#general\r\n", NULL)
                │     → alice._write_buf += ":alice!... JOIN :#general\r\n"
                │
                ├── rpl_notopic → alice._write_buf += ":srv 331 alice #general :No topic\r\n"
                ├── rpl_namreply → alice._write_buf += ":srv 353 alice = #general :@alice\r\n"
                └── rpl_endofnames → alice._write_buf += ":srv 366 alice #general :End of NAMES\r\n"

poll() : POLLOUT sur alice
handleWrite() : send() tout le write_buf

Alice voit dans irssi :
  * Now talking in #general
  * Topic: (none)
  * Users: @alice
```

---

## 8. Récapitulatif

| Commande | Qui peut l'utiliser | Ce qu'elle fait |
|----------|---------------------|-----------------|
| `JOIN #chan [key]` | Tout membre enregistré | Rejoindre/créer un channel |
| `PART #chan [raison]` | Membres du channel | Quitter un channel |
| `TOPIC #chan [nouveau]` | Tout membre (lecture) / Op si +t (écriture) | Lire/modifier le sujet |
| `KICK #chan nick [raison]` | Opérateurs uniquement | Expulser un membre |
| `INVITE nick #chan` | Tout membre (op si +i) | Inviter quelqu'un |

- Le **premier membre** d'un channel devient automatiquement **opérateur**.
- Un channel **vide** est automatiquement **supprimé**.
- `broadcast()` + `enableWriteAll()` forment le mécanisme de diffusion aux membres.
- La liste d'invités (`_invited`) permet le mode `+i` (invite-only).

---

## Moyen de vérifier que ça marche

> Ouvrir **deux terminaux** avec `nc`, les deux connectés et enregistrés.

### Vérification 1 — JOIN crée le channel et envoie la liste des membres

```bash
# Terminal A (alice) :
JOIN #test
# Attendu :
# :alice!alice@127.0.0.1 JOIN :#test
# :ircserv 331 alice #test :No topic is set
# :ircserv 353 alice = #test :@alice
# :ircserv 366 alice #test :End of NAMES list
```

### Vérification 2 — Deuxième client voit l'arrivée du premier

```bash
# Bob rejoint après alice :
JOIN #test
# Bob voit : alice dans la NAMES list
# Alice voit : :bob!bob@127.0.0.1 JOIN :#test
```

### Vérification 3 — TOPIC

```bash
# Alice (op) :
TOPIC #test :Sujet de test
# Attendu : :alice!alice@127.0.0.1 TOPIC #test :Sujet de test
# Bob voit aussi ce message

TOPIC #test
# Attendu : :ircserv 332 alice #test :Sujet de test
```

### Vérification 4 — KICK

```bash
# Alice (op) expulse Bob :
KICK #test bob :Dehors !
# Bob voit : :alice!alice@127.0.0.1 KICK #test bob :Dehors !
# Bob n'est plus dans le channel
```

### Vérification 5 — Channel supprimé quand vide

```bash
# Alice seule dans #test :
PART #test
# Puis réessayer JOIN #test depuis un autre client
# Le topic et les modes doivent être réinitialisés (nouveau channel)
```

**Prochain chapitre** : [09_modes_irc.md](09_modes_irc.md) — les modes `+i` `+t` `+k` `+l` `+o` et la commande `MODE`.
