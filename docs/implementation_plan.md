# ft_irc — Implementation Plan

> Architectural source-of-truth. Each phase is atomic, independently testable,
> and must be implemented in order. **No phase begins before the previous one
> is fully validated.**

---

## 1. Requirements Synthesis

### 1.1. Hard subject constraints

| Constraint                           | Detail                                                                                            |
|--------------------------------------|---------------------------------------------------------------------------------------------------|
| Language                             | C++ **strictly C++98** (`-std=c++98 -Wall -Wextra -Werror`)                                       |
| Binary name                          | `ircserv`                                                                                         |
| Invocation                           | `./ircserv <port> <password>`                                                                     |
| Concurrency model                    | **Single process, single thread**. No `fork`, no threads.                                         |
| I/O model                            | All sockets **non-blocking** (`fcntl(fd, F_SETFL, O_NONBLOCK)` only — any other flag is banned).  |
| Multiplexing                         | **Exactly one** `poll()` (or `select`/`kqueue`/`epoll`) for all FDs (listen, read, write).        |
| Forbidden                            | External libs, Boost, server-to-server, writing an IRC client.                                    |
| Crash policy                         | The server must never crash, never quit unexpectedly, even OOM. Crash ⇒ grade 0.                  |
| Makefile rules                       | `all`, `clean`, `fclean`, `re`, `$(NAME)`. No relinking.                                          |
| Allowed network funcs                | `socket, close, setsockopt, getsockname, getprotobyname, gethostbyname, getaddrinfo, freeaddrinfo, bind, connect, listen, accept, htons, htonl, ntohs, ntohl, inet_addr, inet_ntoa, inet_ntop, send, recv, signal, sigaction, sig*set, lseek, fstat, fcntl, poll`. |
| Transport                            | TCP/IP (v4 or v6).                                                                                |
| Reference client                     | One real IRC client (irssi / WeeChat / HexChat) chosen and used during defense.                   |

### 1.2. Functional requirements

- Authentication via `PASS` (matches CLI password), then `NICK`, then `USER`.
- After registration: send welcome numerics (`001`–`004` minimum).
- Channels with operators and regular users.
- `JOIN`, `PART`, `PRIVMSG` (user→user, user→channel), `NOTICE`, `PING/PONG`, `QUIT`.
- Operator commands: `KICK`, `INVITE`, `TOPIC`.
- `MODE` flags on channels:
  - `i` invite-only,
  - `t` topic restricted to ops,
  - `k` channel key (password),
  - `o` give/take operator,
  - `l` user limit.
- Broadcast: a message sent by a client into a channel is forwarded to **every other** member.
- Robust against TCP fragmentation: a single `recv()` may yield a partial command, multiple commands, or a command split across many recvs (the subject explicitly demonstrates `'com'` + `'man'` + `'d\n'`).

### 1.3. Pitfall checklist (must be honored everywhere)

1. Every FD created (listen, accept, …) is set non-blocking immediately.
2. **No `recv`/`send` is ever called on an FD that wasn't reported ready by `poll()`.** Doing so = grade 0.
3. Per-client **read accumulator**: parse commands only when `\r\n` (or `\n` for tolerance) is present.
4. Per-client **write queue**: never assume `send()` flushes everything; register `POLLOUT` only when output is pending.
5. `EAGAIN`/`EWOULDBLOCK` is **not** an error — it means "try again later".
6. `recv() == 0` ⇒ peer closed; clean up.
7. Use `signal(SIGPIPE, SIG_IGN)` (or `MSG_NOSIGNAL` where available) to avoid being killed by a write to a closed socket.
8. `SIGINT` must trigger a clean shutdown (close all FDs, free everything) — verifiable with `valgrind`/`leaks`.
9. C++98: no `auto`, no `nullptr`, no range-for, no `<thread>`, no lambdas, no `std::to_string`. Use `std::stringstream`.
10. No external lib: parsing, buffering, command dispatch — all hand-rolled.

---

## 2. System Architecture & Object Modeling

### 2.1. Class hierarchy (composition, not inheritance)

```
ircserv (main)
   │
   └── Server  (owns the world)
         ├── int                       _listen_fd
         ├── std::vector<pollfd>       _pfds          (listen + all clients)
         ├── std::map<int, Client*>    _clients       (fd → Client)
         ├── std::map<std::string, Channel*> _channels (name → Channel, case-insensitive)
         ├── std::string               _password
         ├── std::string               _server_name
         ├── time_t                    _start_time
         └── CommandDispatcher         _dispatcher
```

### 2.2. Responsibilities

| Class               | Responsibility                                                                                                  |
|---------------------|-----------------------------------------------------------------------------------------------------------------|
| `Server`            | Lifecycle: create listen socket, run event loop, accept clients, route POLLIN/POLLOUT, shut down cleanly.       |
| `Client`            | One TCP connection. Owns: fd, read buffer, write buffer, registration state, nick/user/host, joined channels.   |
| `Channel`           | Name, topic, key, modes (`i,t,k,l`), member set, operator set, invited set, user limit, broadcast helper.       |
| `Message`           | Pure data: `prefix`, `command`, `params[]`, `trailing`. Built by the parser, consumed by command handlers.      |
| `Parser`            | Static functions: `extractLine(buf)`, `tokenize(line) → Message`. No state.                                     |
| `CommandDispatcher` | Maps a command string (`"NICK"`, `"PRIVMSG"`, …) to a handler `void(Server&, Client&, const Message&)`.         |
| `Reply`             | Static helpers building RFC-correct numeric replies (`RPL_WELCOME 001`, `ERR_NICKNAMEINUSE 433`, …).            |
| `Utils`             | `toUpper`, `splitCSV`, `isValidNick`, `isValidChannelName`, case-insensitive compare for nick/channel.          |

### 2.3. Client state machine

```
NEW ──PASS ok──▶ PASS_OK ──NICK──▶ NICK_OK ──USER──▶ REGISTERED ──QUIT/close──▶ DEAD
                                                          │
                                                          ▼
                                                  participates in channels
```

Order tolerance: standard IRC clients send `PASS`, then `NICK`, then `USER`.
Until `REGISTERED`, only `PASS / NICK / USER / CAP / QUIT / PING` are accepted; everything else returns `ERR_NOTREGISTERED 451`.

### 2.4. Memory & ownership rules (C++98)

- `Server` owns `Client*` and `Channel*` via `std::map`. Destructor walks the maps and `delete`s.
- Copy/assignment of `Server`, `Client`, `Channel` is disabled (declared private, not implemented) — the Coplien orthodox-canonical form is reduced to: default constructor + destructor + (private copy/assign).
- No raw `new` outside `Server::acceptClient()` and `Server::createChannel()`. One owner per pointer.

---

## 3. Main Loop Mechanics (Multiplexing)

### 3.1. Bootstrap

1. Parse argv: validate port (1024–65535) and non-empty password.
2. Install signal handlers: `SIGINT`, `SIGTERM` ⇒ set a `volatile sig_atomic_t g_stop`. Ignore `SIGPIPE`.
3. `socket(AF_INET, SOCK_STREAM, 0)` → `_listen_fd`.
4. `setsockopt(SO_REUSEADDR)` to allow quick restarts.
5. `fcntl(_listen_fd, F_SETFL, O_NONBLOCK)`.
6. `bind()` on `INADDR_ANY:port`, `listen(SOMAXCONN)`.
7. Push `{_listen_fd, POLLIN, 0}` into `_pfds`.

### 3.2. Event loop (single `poll()`)

```
while (!g_stop) {
    poll(&_pfds[0], _pfds.size(), -1);   // -1 = block until event or signal

    for each pollfd pfd in _pfds:
        if (pfd.revents == 0) continue;

        if (pfd.fd == _listen_fd) {
            if (pfd.revents & POLLIN) acceptClient();
            continue;
        }

        Client &c = *_clients[pfd.fd];

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) { disconnect(c); continue; }
        if (pfd.revents & POLLIN)  handleRead(c);
        if (pfd.revents & POLLOUT) handleWrite(c);
    }

    reapDisconnected();   // deferred deletion to keep iteration safe
}
```

### 3.3. Detailed step semantics

- **acceptClient**: `accept()` in a loop until `EAGAIN` (edge of the listen queue is drained in one wakeup); for each new fd → set `O_NONBLOCK`, create `Client`, push pollfd with `POLLIN`.
- **handleRead**: `recv()` into a 4 KiB stack buffer; on `> 0` append to `client.read_buf`; on `0` mark for disconnect; on `-1`: do **not** check `errno` (forbidden by 42 norms in this project — instead, disconnect on `< 0`, since non-blocking sockets that signaled `POLLIN` will return data or 0). After successful read, run `consumeCommands(client)`.
- **handleWrite**: try to drain `client.write_buf` with a single `send()`; on partial send, leave the rest in the buffer; if buffer becomes empty, clear `POLLOUT` from this pollfd.
- **Output queueing**: any code path that wants to send to a client appends to `client.write_buf` and `enableWrite(client)` (sets `POLLOUT`).
- **Deferred deletion**: marking a client dead during iteration; actual `close()` + `delete` + erase from `_pfds` happens after the for-loop to avoid invalidating iterators.

### 3.4. Why one `poll`?

The subject mandates ≤ 1 `poll` for read, write, listen. Listen FD is just another `pollfd` entry; that single `poll()` is the one and only blocking syscall in the program.

---

## 4. Parsing & Buffering Strategy

### 4.1. The fragmentation problem

TCP is a byte stream. A `recv()` may return:
- `"NICK foo\r\nUSER a b c :real\r\nJ"` (multiple commands + 1 partial),
- or just `"co"` then later `"mma"` then later `"nd\n"`.

### 4.2. Read accumulator (per Client)

```
std::string read_buf;   // appended-to on every successful recv()
```

Algorithm `consumeCommands(client)`:

```
loop:
    pos = read_buf.find("\n")
    if pos == npos: break          // wait for more bytes
    line = read_buf.substr(0, pos)
    read_buf.erase(0, pos + 1)
    if !line.empty() && line.back() == '\r': line.pop_back()   // \r\n tolerant
    if line.empty(): continue       // skip empty
    if line.size() > 512: line.resize(512)   // RFC max
    Message msg = Parser::tokenize(line)
    _dispatcher.dispatch(*this, client, msg)
```

### 4.3. IRC line grammar (RFC 2812 §2.3.1, simplified)

```
message  = [ ":" prefix SPACE ] command *( SPACE param ) [ SPACE ":" trailing ] CRLF
prefix   = nick [ "!" user ] [ "@" host ]
command  = 1*letter / 3digit
param    = non-empty, no SPACE, no ":" as first char
trailing = anything until CRLF (may contain spaces and ":")
```

The tokenizer:
1. If line starts with `:`, eat prefix until the first space.
2. Read command word, uppercase it.
3. While there are tokens: if next token starts with `:` → trailing = rest of line, stop; else push word into params.

### 4.4. Write buffer (per Client)

```
std::string write_buf;
```

- Helpers append a full IRC message terminated by `\r\n`.
- Soft cap (e.g. 64 KiB) protects against runaway memory; if exceeded, mark client for disconnect.

### 4.5. Case sensitivity rules

- Commands: case-insensitive (uppercase before dispatch).
- Nicks: case-insensitive comparison (RFC 2812 §2.2 — with the IRC scandinavian rule `{}|^` ↔ `[]\~`; we will implement plain ASCII case-fold; sufficient for evaluation).
- Channel names: case-insensitive comparison; stored as given.

---

## 5. Implementation Phases (Atomic Checklist)

> Each phase ends with a defined, manually verifiable test. **Do not skip ahead.**

### Phase 0 — Skeleton & Build

- [ ] `Makefile` with `NAME = ircserv`, flags `-Wall -Wextra -Werror -std=c++98`, `all/clean/fclean/re`, no relinking (proper `.o` deps).
- [ ] `main.cpp` parses argv, prints them, exits 0.
- [ ] Empty headers: `Server.hpp`, `Client.hpp`, `Channel.hpp`, `Parser.hpp`, `Reply.hpp`, `CommandDispatcher.hpp`, `Utils.hpp` (with include guards).

**Test**: `make`, `./ircserv 6667 mypass` prints args and exits.

### Phase 1 — Listening Socket (TCP echo, no IRC)

- [ ] `Server::init()` creates listening socket, `SO_REUSEADDR`, `O_NONBLOCK`, `bind`, `listen`.
- [ ] `Server::run()` calls `poll()` on just the listen FD, accepts new clients, closes them immediately after a hello message.

**Test**:
```
./ircserv 6667 x &
nc 127.0.0.1 6667     # connection succeeds, banner received, connection closes
```

### Phase 2 — Echo Event Loop

- [ ] Add `Client` class (fd + read_buf + write_buf).
- [ ] On `POLLIN`: read; echo bytes back via write_buf (set `POLLOUT`).
- [ ] On `POLLOUT`: drain write_buf; clear `POLLOUT` when empty.
- [ ] On `recv()==0` or POLLHUP: clean disconnect.
- [ ] **Validate the fragmentation pitfall** with the subject's exact test:
  ```
  nc -C 127.0.0.1 6667
  com<Ctrl-D>man<Ctrl-D>d<Enter>
  ```
  Server must echo `command\n` only after the full line arrives.

**Test**: 2 simultaneous `nc` clients echo independently. Subject's `nc -C` fragmentation test passes.

### Phase 3 — Parser & Dispatcher Plumbing

- [ ] `Parser::tokenize` covers prefix, command, params, trailing — unit-tested with hand-crafted strings.
- [ ] `consumeCommands` extracts lines on `\r\n` or `\n`, handles 512-byte cap.
- [ ] `CommandDispatcher` registers handlers in a `std::map<std::string, Handler>`.
- [ ] Stub handlers for `PING`, `CAP`, `QUIT` only.

**Test**: `nc 127.0.0.1 6667` → typing `PING :hello` → server replies `PONG :hello\r\n`. `CAP LS` → `CAP * LS :` (empty list). `QUIT` → server closes connection.

### Phase 4 — IRC Handshake (PASS / NICK / USER)

- [ ] Implement `PASS <password>` — checks against CLI arg; sets `_pass_ok`. Wrong password ⇒ `ERR_PASSWDMISMATCH 464`, then disconnect.
- [ ] Implement `NICK <nick>` — validates charset (RFC: letter | special, then letter|digit|special|`-`), enforces uniqueness (case-insensitive across `_clients`), `ERR_NICKNAMEINUSE 433`, `ERR_ERRONEUSNICKNAME 432`, `ERR_NONICKNAMEGIVEN 431`.
- [ ] Implement `USER <user> <mode> <unused> :<realname>` — stores user/realname; ignores mode/unused.
- [ ] Once `_pass_ok && nick && user` all set, transition to `REGISTERED`, send numerics:
  - `001 RPL_WELCOME`
  - `002 RPL_YOURHOST`
  - `003 RPL_CREATED`
  - `004 RPL_MYINFO`
- [ ] All other commands before `REGISTERED` ⇒ `ERR_NOTREGISTERED 451`.

**Test (irssi/weechat as reference client)**:
```
/connect 127.0.0.1 6667 mypass
/nick alice
```
Client must show "Welcome to the Internet Relay Network alice" and not error out. Wrong password → disconnected with 464.

### Phase 5 — Direct PRIVMSG & PING/PONG/NOTICE/QUIT

- [ ] `PRIVMSG <target> :<text>` where target is a nickname → forward `:srcprefix PRIVMSG target :text\r\n` to that client. Errors: `401 ERR_NOSUCHNICK`, `411 ERR_NORECIPIENT`, `412 ERR_NOTEXTTOSEND`.
- [ ] `NOTICE` — same as PRIVMSG but **never** generates an automatic error reply.
- [ ] `QUIT [:reason]` — broadcasts `QUIT` to all channels the client is in (Phase 6 will wire this), then closes.
- [ ] `PING/PONG` already from Phase 3, refine prefix.

**Test**: 2 clients connect; `alice` sends `/msg bob hi`; `bob`'s window shows `<alice> hi`.

### Phase 6 — Channels: JOIN / PART / channel PRIVMSG / TOPIC (read)

- [ ] `JOIN <#chan>{,<#chan>} [<key>{,<key>}]`: create on first join; first joiner becomes operator (`+o`); broadcast `:nick!user@host JOIN :#chan`; send `RPL_TOPIC 332` (or `RPL_NOTOPIC 331`) and `RPL_NAMREPLY 353` + `RPL_ENDOFNAMES 366`. Errors: `403 ERR_NOSUCHCHANNEL`, `405 ERR_TOOMANYCHANNELS`, `471 ERR_CHANNELISFULL`, `473 ERR_INVITEONLYCHAN`, `475 ERR_BADCHANNELKEY`.
- [ ] `PART <#chan> [:reason]`: remove member, broadcast PART; if channel empty, delete it.
- [ ] `PRIVMSG #chan :text`: forward to **all members except sender**. `404 ERR_CANNOTSENDTOCHAN` if not a member (or other restrictions later).
- [ ] `TOPIC #chan` (no arg) → `RPL_TOPIC` / `RPL_NOTOPIC`. With arg → set topic if mode `+t` is unset OR sender is op; broadcast.
- [ ] `QUIT` now propagates to channels.

**Test**: 3 clients (`alice`, `bob`, `carol`); `alice` & `bob` `/join #test`; `alice` writes — `bob` sees, `carol` doesn't. `/topic #test :hello` round-trips.

### Phase 7 — Channel Operator Commands

- [ ] `KICK #chan <nick> [:reason]` — only ops; broadcast KICK; remove target.
- [ ] `INVITE <nick> #chan` — only ops if `+i`; adds to channel's invited set; replies `RPL_INVITING 341` to inviter, `INVITE` to invitee.
- [ ] Permission errors: `482 ERR_CHANOPRIVSNEEDED`, `441 ERR_USERNOTINCHANNEL`, `443 ERR_USERONCHANNEL`, `442 ERR_NOTONCHANNEL`.

**Test**: ops can kick/invite; non-ops get 482; invited user can join `+i` channel.

### Phase 8 — MODE (i, t, k, o, l)

- [ ] `MODE #chan` (no flags) → reply current modes (`RPL_CHANNELMODEIS 324`).
- [ ] `MODE #chan +i / -i` invite-only.
- [ ] `MODE #chan +t / -t` topic restricted to ops.
- [ ] `MODE #chan +k <key> / -k` channel password (JOIN now checks key).
- [ ] `MODE #chan +o <nick> / -o <nick>` give/take operator.
- [ ] `MODE #chan +l <n> / -l` user limit (JOIN now checks size).
- [ ] Multi-flag parsing: `MODE #c +itk-l secret` — iterate the flag string, consuming params from the queue in order.
- [ ] Errors: `472 ERR_UNKNOWNMODE`, `461 ERR_NEEDMOREPARAMS`, `467 ERR_KEYSET`.

**Test (matrix)**: each mode set/unset by op, verified by a 2nd client trying to join/topic. Non-op gets 482.

### Phase 9 — Hardening & Edge Cases

- [ ] Subject's `nc -C` fragmentation test re-verified end-to-end with real commands (`com`, then `mand`, then `\r\n`).
- [ ] Pasting 2000-byte single line: server caps at 512 and continues normally.
- [ ] Killing a client mid-write (`Ctrl-C` on `nc`): server logs disconnect, continues serving others.
- [ ] 50 simultaneous `nc` clients (shell loop) — server stays responsive.
- [ ] `valgrind --leak-check=full ./ircserv 6667 x` (Linux) / `leaks` (macOS) on `SIGINT` shutdown: 0 leaks, 0 still-reachable from server-owned allocations.
- [ ] `setsockopt(SO_REUSEADDR)` allows immediate restart on the same port.
- [ ] `signal(SIGPIPE, SIG_IGN)` confirmed (kill a client, server keeps running on a write).
- [ ] Re-read every allowed/forbidden function: `grep -rE '\b(fork|pthread|std::thread|select|epoll)\b' src/` returns nothing unintended.

**Test**: fuzz-ish session with the reference client (irssi or weechat) covering: connect / nick collision / wrong password / join+key / invite-only / topic / kick / mode toggles / private msg / quit. Server must never crash.

### Phase 10 — README & Submission

- [ ] `README.md`: italic first line, **Description**, **Instructions**, **Resources** (RFC 1459, RFC 2812, modern IRCv3, Beej's network guide, and the AI usage statement).
- [ ] Tag a clean commit; verify `make re` from a fresh clone.

---

## 6. Anti-spaghetti rules (apply always)

1. **One file per class** (`.hpp` + `.cpp`).
2. **Handlers are free functions** registered in the dispatcher map; each lives in `commands/cmd_<name>.cpp` and has the same signature. Adding a command = one file + one map entry.
3. **No I/O outside `Server`**. Handlers only mutate state and call `Server::sendTo(client, msg)` / `Channel::broadcast(...)`.
4. **No `errno` checks** outside the listen/bind setup — the poll-driven model treats every recv/send error as "drop the client", which is sufficient and norm-friendly.
5. **All numerics in one place** (`Reply.hpp/.cpp`) — never hand-format an `001` or `433` inline.
6. **Logs to stderr only**, prefixed `[ircserv]`, off in release builds via a single `LOG()` macro.
7. **No premature optimization**: `std::map` is fine, `std::string` is fine. Profile only if a phase test is too slow (it won't be).

---

## 7. RFC numerics referenced (cheat-sheet for Phase 4–8)

```
001 RPL_WELCOME           002 RPL_YOURHOST            003 RPL_CREATED            004 RPL_MYINFO
221 RPL_UMODEIS           324 RPL_CHANNELMODEIS       331 RPL_NOTOPIC            332 RPL_TOPIC
341 RPL_INVITING          353 RPL_NAMREPLY            366 RPL_ENDOFNAMES
401 ERR_NOSUCHNICK        403 ERR_NOSUCHCHANNEL       404 ERR_CANNOTSENDTOCHAN
411 ERR_NORECIPIENT       412 ERR_NOTEXTTOSEND
421 ERR_UNKNOWNCOMMAND    431 ERR_NONICKNAMEGIVEN     432 ERR_ERRONEUSNICKNAME   433 ERR_NICKNAMEINUSE
441 ERR_USERNOTINCHANNEL  442 ERR_NOTONCHANNEL        443 ERR_USERONCHANNEL
451 ERR_NOTREGISTERED     461 ERR_NEEDMOREPARAMS      462 ERR_ALREADYREGISTRED
464 ERR_PASSWDMISMATCH
471 ERR_CHANNELISFULL     472 ERR_UNKNOWNMODE         473 ERR_INVITEONLYCHAN     475 ERR_BADCHANNELKEY
482 ERR_CHANOPRIVSNEEDED
```

---

## 8. Definition of Done (project-level)

- All 10 phases pass their tests with the chosen reference client.
- `make re` compiles cleanly with `-Wall -Wextra -Werror -std=c++98`, no warnings.
- The server survives every test in Phase 9 without crash, leak, or hang.
- The plan in this document was followed in order; no phase was skipped or merged.
