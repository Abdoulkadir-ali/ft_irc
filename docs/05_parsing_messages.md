# Phase 05 — Parsing des Messages IRC

> **Pré-requis** : avoir lu [04_gestion_clients.md](04_gestion_clients.md)  
> **Fichiers concernés** : [includes/Parser.hpp](../includes/Parser.hpp), [srcs/Parser.cpp](../srcs/Parser.cpp), [includes/Message.hpp](../includes/Message.hpp)

---

## 1. Concept de base — Le format d'un message IRC

IRC est un protocole **texte**. Chaque commande est une ligne de texte terminée par `\r\n` (retour chariot + saut de ligne). C'est ce que votre client IRC (irssi, WeeChat...) envoie sur le réseau, et ce que vous recevez avec `recv()`.

Exemples de messages IRC bruts :

```
PASS monpassword\r\n
NICK alice\r\n
USER alice42 0 * :Alice Dupont\r\n
JOIN #general,#random key1\r\n
PRIVMSG #general :Bonjour tout le monde!\r\n
:alice!alice42@192.168.1.5 PRIVMSG #general :Salut\r\n
```

Le serveur doit **décomposer** (parser) chaque ligne en parties exploitables.

---

## 2. Définitions des termes clés

| Terme | Définition |
|-------|-----------|
| **Protocole texte** | Un protocole où les messages sont des chaînes de caractères lisibles (contrairement aux protocoles binaires). Avantage : facile à déboguer avec `telnet`. |
| **\r\n** | Retour à la ligne en style DOS/Windows (CRLF). IRC exige `\r\n` à la fin de chaque message. Le code tolère aussi `\n` seul. |
| **Préfixe** | Partie optionnelle au début d'une ligne IRC, commençant par `:`. Identifie l'expéditeur. Exemple : `:alice!alice42@host`. |
| **Commande** | Le mot-clé qui suit le préfixe. Toujours en MAJUSCULES. Exemple : `PRIVMSG`, `JOIN`. |
| **Paramètre** | Les arguments de la commande. Séparés par des espaces. |
| **Trailing param** | Dernier paramètre précédé d'un `:` qui peut contenir des espaces. Exemple : `:Bonjour tout le monde`. Sans le `:`, chaque mot serait un paramètre séparé. |
| **struct Message** | La structure C++ dans laquelle on stocke le résultat du parsing. |
| **Tokenize** | Découper une chaîne en tokens (morceaux) selon des règles définies. |

---

## 3. La structure `Message`

Fichier : [includes/Message.hpp](../includes/Message.hpp)

```cpp
struct Message {
    std::string              prefix;  // ex: "alice!alice42@192.168.1.5"
    std::string              command; // ex: "PRIVMSG" (toujours majuscules)
    std::vector<std::string> params;  // ex: ["#general", "Bonjour tout le monde"]
};
```

Exemples de parsing :

| Ligne brute | `prefix` | `command` | `params` |
|-------------|----------|-----------|----------|
| `JOIN #general` | `""` | `"JOIN"` | `["#general"]` |
| `PRIVMSG #gen :Salut` | `""` | `"PRIVMSG"` | `["#gen", "Salut"]` |
| `NICK alice` | `""` | `"NICK"` | `["alice"]` |
| `:srv 001 alice :Welcome` | `"srv"` | `"001"` | `["alice", "Welcome"]` |
| `JOIN #a,#b key1,key2` | `""` | `"JOIN"` | `["#a,#b", "key1,key2"]` |

---

## 4. Objectif attendu dans ce projet

Le module `Parser` doit :
1. **Extraire** des lignes complètes d'un buffer (qui peut contenir plusieurs lignes ou une ligne incomplète).
2. **Tokeniser** chaque ligne selon le format IRC (préfixe optionnel, commande, paramètres, trailing).
3. Être **robuste** : tolérer `\r\n` ou `\n`, tronquer à 512 caractères.

---

## 5. Pourquoi ces objectifs ?

### Pourquoi extraire les lignes depuis un buffer ?

TCP est un flux d'octets, pas un flux de messages. `recv()` peut retourner :
- Une ligne complète : `"NICK alice\r\n"`
- Deux lignes d'un coup : `"NICK alice\r\nUSER alice42 0 * :Dupont\r\n"`
- Une ligne incomplète : `"NICK ali"` (le reste arrivera au prochain `recv()`)

`Parser::extractLine()` gère ça proprement en cherchant `\n` dans le buffer.

### Pourquoi le trailing param avec `:` ?

Sans le `:`, `PRIVMSG #general Bonjour tout le monde` serait parsé comme 5 paramètres distincts : `["#general", "Bonjour", "tout", "le", "monde"]`. Le `:` signifie "tout ce qui suit est UN SEUL paramètre, espaces inclus". C'est la règle IRC officielle (RFC 2812).

### Pourquoi normaliser en MAJUSCULES ?

IRC définit les commandes en majuscules (`JOIN`, `PRIVMSG`...) mais les clients peuvent les envoyer en minuscules. `Utils::toUpper()` dans `tokenize()` garantit que le dispatcher trouve toujours `"JOIN"` et jamais `"join"`.

### Pourquoi 512 caractères max ?

La RFC 2812 impose une longueur maximale de 512 octets par message (incluant le `\r\n`). Dépasser cette limite peut être une tentative d'attaque par buffer overflow ou de flooding. On tronque silencieusement.

---

## 6. Implémentation — Code détaillé

### 6.1 — `Parser::extractLine()` : découper le buffer en lignes

Fichier : [srcs/Parser.cpp](../srcs/Parser.cpp), lignes 5–16

```cpp
bool Parser::extractLine(std::string &buf, std::string &line)
{
    // Chercher le caractère newline dans le buffer
    std::string::size_type pos = buf.find('\n');
    if (pos == std::string::npos)
        return false; // Pas de \n = ligne incomplète, on attend la suite

    // Extraire tout ce qui précède le \n
    line = buf.substr(0, pos);

    // Supprimer la ligne (et le \n) du buffer
    // La prochaine ligne sera disponible au prochain appel
    buf.erase(0, pos + 1);

    // IRC utilise \r\n, mais on peut recevoir \n seul
    // On supprime le \r final si présent
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);

    return true; // Une ligne complète a été extraite
}
```

**Cas par cas :**

```
buf = "NICK alice\r\nUSER alice42 0 * :Dupont\r\n"
extractLine() appel 1 : line = "NICK alice",  buf = "USER alice42 0 * :Dupont\r\n"
extractLine() appel 2 : line = "USER alice42 0 * :Dupont", buf = ""
extractLine() appel 3 : retourne false (buf vide)

buf = "NICK ali"   (message incomplet)
extractLine() : retourne false, buf inchangé

buf = "NICK alice\n"  (sans \r — toléré)
extractLine() : line = "NICK alice", buf = ""
```

### 6.2 — `Parser::tokenize()` : analyser une ligne IRC

Fichier : [srcs/Parser.cpp](../srcs/Parser.cpp), lignes 18–75

```cpp
Message Parser::tokenize(const std::string &raw)
{
    Message msg;
    std::string line(raw);

    // ── Sécurité : tronquer à 512 caractères (RFC 2812) ─────────────────
    if (line.size() > 512)
        line.resize(512);

    std::string::size_type i = 0;

    // ── Étape 1 : Préfixe optionnel ──────────────────────────────────────
    // Le préfixe commence par ':' et se termine au premier espace
    if (!line.empty() && line[0] == ':') {
        std::string::size_type sp = line.find(' ', 1);
        if (sp == std::string::npos) {
            // Que le préfixe, pas de commande → message invalide
            msg.prefix = line.substr(1);
            return msg;
        }
        msg.prefix = line.substr(1, sp - 1); // "alice!alice42@host"
        i = sp + 1; // avancer après le préfixe
    }

    // Ignorer les espaces supplémentaires
    while (i < line.size() && line[i] == ' ')
        ++i;

    // ── Étape 2 : Commande ───────────────────────────────────────────────
    {
        std::string::size_type start = i;
        while (i < line.size() && line[i] != ' ')
            ++i;
        // toUpper : normaliser "join" → "JOIN", "privmsg" → "PRIVMSG"
        msg.command = Utils::toUpper(line.substr(start, i - start));
    }

    // ── Étape 3 : Paramètres ─────────────────────────────────────────────
    while (i < line.size()) {
        // Ignorer les espaces entre paramètres
        while (i < line.size() && line[i] == ' ')
            ++i;

        if (i >= line.size())
            break;

        // Trailing param : ':' → tout le reste est UN seul paramètre
        if (line[i] == ':') {
            msg.params.push_back(line.substr(i + 1)); // tout après le ':'
            break; // c'est forcément le dernier paramètre
        }

        // Paramètre normal : jusqu'au prochain espace
        std::string::size_type start = i;
        while (i < line.size() && line[i] != ' ')
            ++i;
        msg.params.push_back(line.substr(start, i - start));
    }

    return msg;
}
```

**Trace d'exécution pas à pas pour `"PRIVMSG #general :Bonjour le monde"` :**

```
line = "PRIVMSG #general :Bonjour le monde"
i = 0

Étape 1 : line[0] = 'P', pas de ':' → pas de préfixe
msg.prefix = ""

Étape 2 : 
  start = 0
  avancer jusqu'à ' ' → i = 7
  msg.command = toUpper("PRIVMSG") = "PRIVMSG"

Étape 3 (boucle) :
  Ignorer espaces → i = 8
  line[8] = '#', pas de ':' → paramètre normal
    start = 8
    avancer jusqu'à ' ' → i = 16
    params.push_back("#general")  → params = ["#general"]
  
  Ignorer espaces → i = 17
  line[17] = ':', trailing param !
    params.push_back("Bonjour le monde")  → params = ["#general", "Bonjour le monde"]
    break

Résultat :
  msg.prefix  = ""
  msg.command = "PRIVMSG"
  msg.params  = ["#general", "Bonjour le monde"]
```

**Comparaison avec et sans trailing `:`:**

```
"PRIVMSG alice Bonjour le monde"
→ params = ["alice", "Bonjour", "le", "monde"]  ← 4 paramètres !

"PRIVMSG alice :Bonjour le monde"
→ params = ["alice", "Bonjour le monde"]         ← 2 paramètres (correct)
```

### 6.3 — Utilisation dans `consumeCommands()`

Fichier : [srcs/Server.cpp](../srcs/Server.cpp)

```cpp
void Server::consumeCommands(Client &client)
{
    std::string line;

    // Boucle : tant qu'il y a des lignes complètes dans le buffer
    while (Parser::extractLine(client.readBuf(), line)) {
        if (line.empty()) continue;
        if (line.size() > 512) line.resize(512); // double sécurité

        // Transformer la ligne en structure Message
        Message msg = Parser::tokenize(line);
        if (msg.command.empty()) continue; // ligne invalide

        // ... vérifications d'état ...
        _dispatcher.dispatch(*this, client, msg); // router vers le bon handler
    }
    // S'arrête quand extractLine retourne false (buffer vide ou ligne incomplète)
}
```

---

## 7. Schéma de l'ensemble du pipeline de parsing

```
Socket réseau (alice, fd=6)
│
│ recv() → "NICK alice\r\nJOIN #gen\r\n"
▼
client._read_buf = "NICK alice\r\nJOIN #gen\r\n"
│
│ consumeCommands() appelle extractLine() en boucle
│
├── extractLine() → line = "NICK alice",  buf = "JOIN #gen\r\n"
│       │
│       ▼ tokenize("NICK alice")
│       Message { prefix:"", command:"NICK", params:["alice"] }
│       │
│       ▼ dispatcher.dispatch() → cmd_nick(srv, alice, msg)
│
└── extractLine() → line = "JOIN #gen", buf = ""
        │
        ▼ tokenize("JOIN #gen")
        Message { prefix:"", command:"JOIN", params:["#gen"] }
        │
        ▼ dispatcher.dispatch() → cmd_join(srv, alice, msg)
```

---

## 8. Récapitulatif

- IRC est un protocole **texte** : une commande = une ligne terminée par `\r\n`.
- `extractLine()` gère la **fragmentation TCP** en extrayant des lignes complètes d'un buffer accumulé.
- `tokenize()` décompose chaque ligne en `{prefix, command, params}`.
- Le **trailing param** (`:...`) permet d'avoir des espaces dans le dernier paramètre.
- La commande est toujours **normalisée en majuscules** pour le dispatcher.

---

## Moyen de vérifier que ça marche

### Vérification 1 — Commandes en minuscules acceptées

```bash
nc 127.0.0.1 6667
pass secret\r\nnick alice\r\nuser alice 0 * :Alice\r\n
# Attendu : registration complète (tokenize normalise en majuscules)
```

### Vérification 2 — Trailing param avec espaces

```bash
nc 127.0.0.1 6667
PASS secret
NICK alice
USER alice 0 * :Alice Dupont avec espaces
JOIN #test
PRIVMSG #test :Bonjour tout le monde !
# "Bonjour tout le monde !" doit arriver comme UN seul paramètre
# Attendu dans l'autre client : PRIVMSG #test :Bonjour tout le monde !
```

### Vérification 3 — Lignes fragmentées et multiples

```bash
# Envoyer 2 commandes collées (pas de \r\n séparé)
printf 'PASS secret\r\nNICK alice\r\nUSER alice 0 * :Alice\r\n' | nc 127.0.0.1 6667
# extractLine() doit les séparer correctement
```

### Vérification 4 — Message trop long tronqué

```bash
# Générer une ligne de 600 caractères
python3 -c "print('PRIVMSG #test :' + 'A'*600)" | nc 127.0.0.1 6667
# Le serveur ne doit pas crasher (tronqué à 512)
```

**Prochain chapitre** : [06_dispatch_commandes.md](06_dispatch_commandes.md) — comment `CommandDispatcher` route chaque commande vers la bonne fonction.
