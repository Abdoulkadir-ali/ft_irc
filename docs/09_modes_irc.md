# Phase 09 — Les Modes IRC

> **Pré-requis** : avoir lu [08_canaux.md](08_canaux.md)  
> **Fichiers concernés** : [srcs/commands/cmd_mode.cpp](../srcs/commands/cmd_mode.cpp), [srcs/Channel.cpp](../srcs/Channel.cpp) méthode `getModeString()`

---

## 1. Concept de base — Les modes IRC

Les **modes** sont des drapeaux (flags) qui modifient le comportement d'un channel. C'est le système de configuration d'un salon IRC.

Analogie : imaginez des paramètres dans les réglages d'une salle de réunion :
- 🔒 **Porte fermée** (`+i`) : sur invitation uniquement
- 📌 **Sujet modifiable par le responsable** (`+t`) : seul le président peut changer l'ordre du jour
- 🔑 **Code d'accès** (`+k`) : il faut le mot de passe pour entrer
- 👥 **Capacité maximale** (`+l`) : 50 personnes maximum
- 👑 **Donner des droits** (`+o`) : nommer quelqu'un responsable de la salle

La commande `MODE` permet d'**activer** (`+`) ou de **désactiver** (`-`) ces options.

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Mode** | Un flag (lettre) qui active une fonctionnalité d'un channel. |
| **`+`** | Activer le mode qui suit. |
| **`-`** | Désactiver le mode qui suit. |
| **Mode `+i`** | Invite-Only : on ne peut rejoindre que si on a été invité. |
| **Mode `+t`** | Topic Restricted : seuls les ops peuvent changer le topic. |
| **Mode `+k`** | Key : un mot de passe est requis pour `JOIN`. |
| **Mode `+l`** | Limit : nombre maximum de membres dans le channel. |
| **Mode `+o`** | Operator : donne/retire le statut d'opérateur à un membre. |
| **Mode string** | Représentation textuelle des modes actifs. Ex : `+itk monpass` |
| **Paramètre de mode** | Certains modes nécessitent un argument : `+k <clé>`, `+l <nombre>`, `+o <nick>`. |
| **RPL_CHANNELMODEIS (324)** | Réponse numérique indiquant les modes actuels du channel. |

---

## 3. Tableau des modes implémentés

| Mode | Param requis | Activation | Désactivation | Description |
|------|-------------|-----------|--------------|-------------|
| `i`  | Non | `+i` | `-i` | Invite-only : JOIN bloqué sans INVITE |
| `t`  | Non | `+t` | `-t` | Topic restreint aux ops |
| `k`  | Oui (clé) | `+k motdepasse` | `-k` | Mot de passe pour entrer |
| `l`  | Oui (nombre) | `+l 50` | `-l` | Limite du nombre de membres |
| `o`  | Oui (nick) | `+o alice` | `-o alice` | Donner/retirer le statut op |

---

## 4. Syntaxe de la commande MODE

```
MODE <channel> [modes [paramètres...]]
```

**Exemples :**

```
MODE #general                 → interroger les modes actuels
MODE #general +i              → activer invite-only
MODE #general -i              → désactiver invite-only
MODE #general +it             → activer invite-only ET topic-restricted
MODE #general +k monpassword  → définir un mot de passe
MODE #general -k              → supprimer le mot de passe
MODE #general +l 25           → limiter à 25 membres
MODE #general -l              → supprimer la limite
MODE #general +o alice        → donner le statut op à alice
MODE #general -o alice        → retirer le statut op à alice
MODE #general +itk pass +l 10 → plusieurs modes d'un coup
```

---

## 5. Objectif attendu dans ce projet

`cmd_mode()` doit :
1. Autoriser la **consultation** des modes sans être op.
2. Autoriser la **modification** des modes uniquement aux opérateurs.
3. Gérer les 5 modes (`i`, `t`, `k`, `l`, `o`) avec leurs paramètres.
4. Diffuser les changements de mode à tout le channel.
5. Générer une chaîne de mode compacte pour la diffusion (ex : `+itk monpass`).

---

## 6. Pourquoi ces objectifs ?

### Pourquoi les modes sont-ils réservés aux opérateurs ?

Les modes contrôlent le comportement du channel. Les laisser à tout le monde créerait le chaos : n'importe qui pourrait verrouiller le channel, expulser des membres, changer le topic... Les ops sont les "administrateurs" du channel.

### Pourquoi le format `+itk pass` plutôt qu'une liste de changements ?

IRC utilise un format compact où toutes les lettres de mode sont groupées, puis tous les paramètres suivent. Cela économise de la bande passante et correspond au standard RFC 2812.

### Pourquoi accumuler `appliedModes` ?

Un seul `MODE #general +itk pass` peut changer 3 modes. La commande diffusée aux membres ne doit inclure que les modes qui ont **réellement** été changés (un mode déjà actif qu'on réactive ne génère pas de message).

---

## 7. Implémentation — Code détaillé

### 7.1 — Structure générale de `cmd_mode()`

Fichier : [srcs/commands/cmd_mode.cpp](../srcs/commands/cmd_mode.cpp)

```cpp
void cmd_mode(Server &srv, Client &cli, const Message &msg)
{
    // ... vérifications état, paramètres ...

    const std::string &target = msg.params[0];

    // Seuls les modes de channels sont supportés
    // (les user modes comme +i pour un nick sont ignorés)
    if (target[0] != '#' && target[0] != '&')
        return;

    Channel *chan = srv.findChannel(target);
    if (!chan) {
        srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, target));
        return;
    }

    // ── Consultation (pas de mode string) ───────────────────────────────
    if (msg.params.size() < 2) {
        srv.sendTo(cli, Reply::rpl_channelmodeis(srv.getName(), cli,
            chan->getName(), chan->getModeString()));
        return;
    }

    // ── Modification : réservée aux ops ─────────────────────────────────
    if (!chan->isOperator(&cli)) {
        srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chan->getName()));
        return;
    }

    const std::string &modeStr = msg.params[1]; // ex: "+itk" ou "-l" ou "+o-i"
    bool adding = true;         // + = ajouter, - = retirer
    size_t paramIdx = 2;        // prochain paramètre à consommer

    // Accumule les modes réellement appliqués (pour la diffusion)
    std::string appliedModes;
    std::string appliedParams;
    bool lastWasPlus = true;
    bool needSign = true;

    // ── Traiter chaque caractère de la mode string ───────────────────────
    for (size_t i = 0; i < modeStr.size(); ++i) {
        char c = modeStr[i];
        if (c == '+') { adding = true; needSign = true; continue; }
        if (c == '-') { adding = false; needSign = true; continue; }

        switch (c) {
        case 'i': /* ... */ break;
        case 't': /* ... */ break;
        case 'k': /* ... */ break;
        case 'o': /* ... */ break;
        case 'l': /* ... */ break;
        default: srv.sendTo(cli, Reply::err_unknownmode(srv.getName(), cli, c)); break;
        }
    }

    // ── Diffuser le changement si des modes ont été appliqués ────────────
    if (!appliedModes.empty()) {
        std::string modeMsg = ":" + cli.getPrefix() + " MODE " + chan->getName()
            + " " + appliedModes + appliedParams + "\r\n";
        chan->broadcast(modeMsg, NULL);
        srv.enableWriteAll(*chan, NULL);
    }
}
```

### 7.2 — Mode `+i/-i` : invite-only (sans paramètre)

```cpp
case 'i':
    chan->setInviteOnly(adding);  // true ou false
    
    // Ajouter le signe (+/-) uniquement si nécessaire
    // (évite "+i+t" au profit de "+it")
    if (needSign || adding != lastWasPlus) {
        appliedModes += (adding ? "+" : "-");
        lastWasPlus = adding;
        needSign = false;
    }
    appliedModes += 'i';
    break;
```

**Exemple de diffusion :**
```
":alice!alice@host MODE #general +it\r\n"
```

### 7.3 — Mode `+k/-k` : clé/mot de passe (paramètre requis pour `+k`)

```cpp
case 'k':
    if (adding) {
        // +k nécessite un paramètre (le mot de passe)
        if (paramIdx >= msg.params.size()) {
            srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "MODE"));
            continue;
        }
        chan->setKey(msg.params[paramIdx]); // stocker la clé
        // ... accumuler mode et param ...
        appliedParams += " " + msg.params[paramIdx];
        ++paramIdx;
    } else {
        // -k : supprimer la clé (pas de paramètre)
        chan->removeKey();
        // ... accumuler -k ...
    }
    break;
```

### 7.4 — Mode `+o/-o` : donner/retirer statut opérateur (paramètre = nick)

```cpp
case 'o': {
    if (paramIdx >= msg.params.size()) {
        srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "MODE"));
        continue;
    }
    std::string nick = msg.params[paramIdx++];
    Client *tgt = srv.findClientByNick(nick);

    if (!tgt || !chan->isMember(tgt)) {
        srv.sendTo(cli, Reply::err_usernotinchannel(srv.getName(), cli, nick, chan->getName()));
        continue;
    }

    if (adding)
        chan->addOperator(tgt);   // +o alice → alice devient op
    else
        chan->removeOperator(tgt); // -o alice → alice perd ses droits op

    // ... accumuler mode et param ...
    appliedParams += " " + tgt->getNick();
    break;
}
```

### 7.5 — Mode `+l/-l` : limite de membres (paramètre = nombre entier)

```cpp
case 'l':
    if (adding) {
        if (paramIdx >= msg.params.size()) {
            srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "MODE"));
            continue;
        }
        // Convertir la chaîne en entier
        int limit = std::atoi(msg.params[paramIdx].c_str());
        ++paramIdx;
        if (limit <= 0) continue; // ignorer les limites invalides

        chan->setUserLimit(limit);
        // ... accumuler "+l " + limit ...
    } else {
        chan->removeUserLimit(); // _user_limit = 0
        // ... accumuler "-l" ...
    }
    break;
```

### 7.6 — `getModeString()` : la représentation textuelle

Fichier : [srcs/Channel.cpp](../srcs/Channel.cpp)

```cpp
std::string Channel::getModeString() const
{
    std::string modes = "+";    // commence toujours par +
    std::string params;         // paramètres des modes avec arguments

    if (_invite_only)     modes += "i";
    if (_topic_restricted) modes += "t";
    if (!_key.empty()) {
        modes += "k";
        params += " " + _key;   // param pour k
    }
    if (_user_limit > 0) {
        modes += "l";
        params += " " + Utils::intToStr(_user_limit); // param pour l
    }

    if (modes == "+") return "+";   // aucun mode actif → retourne juste "+"
    return modes + params;
    // Exemples :
    // → "+i"         (invite-only seulement)
    // → "+it"        (invite-only + topic restreint)
    // → "+itk pass"  (+ clé "pass")
    // → "+itk pass l 25" (+ limite 25)
}
```

**Utilisé par `RPL_CHANNELMODEIS (324)` quand quelqu'un interroge :**

```
Client envoie : "MODE #general\r\n"
Serveur répond : ":ircserv 324 alice #general +it\r\n"
```

---

## 8. Exemples complets d'interaction

### Exemple 1 : Protéger un channel

```
alice (op) tape : /mode #general +itk secret

Client → Serveur : "MODE #general +itk :secret\r\n"
cmd_mode() :
  modeStr = "+itk"
  +i : chan->setInviteOnly(true)     → appliedModes = "+i"
  +t : chan->setTopicRestricted(true) → appliedModes = "+it"
  +k : chan->setKey("secret")        → appliedModes = "+itk", appliedParams = " secret"

broadcast(":alice!... MODE #general +itk secret\r\n")
```

### Exemple 2 : Nommer un opérateur

```
alice (op) tape : /mode #general +o bob

Client → Serveur : "MODE #general +o bob\r\n"
cmd_mode() :
  tgt = findClientByNick("bob")
  chan->addOperator(tgt)  → bob est maintenant op
  appliedModes = "+o", appliedParams = " bob"

broadcast(":alice!... MODE #general +o bob\r\n")
irssi ajoute "@" devant le nick de bob dans la liste
```

### Exemple 3 : Combinaison de modes

```
MODE #general +it-k
  +i : invite-only activé
  +t : topic restreint activé
  -k : clé supprimée

appliedModes = "+it-k"
```

---

## 9. Récapitulatif

| Mode | Stockage dans Channel | Vérifiée dans... | Paramètre |
|------|-----------------------|-----------------|-----------|
| `i` | `_invite_only : bool` | `cmd_join()` | Non |
| `t` | `_topic_restricted : bool` | `cmd_topic()` | Non |
| `k` | `_key : string` | `cmd_join()` | Oui (`+k`) |
| `l` | `_user_limit : int` | `cmd_join()` | Oui (`+l`) |
| `o` | `_operators : set<int>` | `cmd_kick()`, `cmd_mode()`, `cmd_topic()` | Oui (nick) |

- `+` active, `-` désactive.
- Seuls les **opérateurs** peuvent changer les modes.
- `MODE #chan` sans argument = interroger les modes → `RPL_CHANNELMODEIS (324)`.
- Les changements sont diffusés à **tout le channel**.

---

## Moyen de vérifier que ça marche

> Alice est op dans `#test`. Bob est membre simple.

### Vérification 1 — Consulter les modes

```bash
MODE #test
# Attendu : :ircserv 324 alice #test +  (aucun mode actif)
```

### Vérification 2 — Mode +i (invite-only)

```bash
# Alice :
MODE #test +i
# Attendu : :alice!alice@127.0.0.1 MODE #test +i

# Charlie (non invité) essaie de rejoindre :
JOIN #test
# Attendu : :ircserv 473 charlie #test :Cannot join channel (+i)

# Alice invite charlie :
INVITE charlie #test
# Charlie peut maintenant JOIN #test
```

### Vérification 3 — Mode +k (mot de passe)

```bash
MODE #test +k monpass
JOIN #test                  # sans clé → 475 :Bad channel key
JOIN #test mauvaispass      # mauvaise clé → 475
JOIN #test monpass          # bonne clé → succès
MODE #test -k               # supprimer le mot de passe
```

### Vérification 4 — Mode +l (limite)

```bash
MODE #test +l 2
# Avec déjà 2 membres, un troisième tente JOIN :
# Attendu : :ircserv 471 charlie #test :Cannot join channel (+l)
```

### Vérification 5 — Mode +o (opérateur)

```bash
# Alice donne les droits op à Bob :
MODE #test +o bob
# Bob peut maintenant faire KICK, TOPIC, MODE

# Révoquer :
MODE #test -o bob
```

### Vérification 6 — Seuls les ops peuvent changer les modes

```bash
# Bob (pas op) :
MODE #test +i
# Attendu : :ircserv 482 bob #test :You're not channel operator
```

**Prochain chapitre** : [10_messagerie.md](10_messagerie.md) — `PRIVMSG`, `NOTICE` et le broadcast.
