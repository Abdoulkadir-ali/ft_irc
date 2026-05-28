# Code Review Guide: `ft_irc`

Welcome to the `ft_irc` codebase! This guide is designed to help you navigate and review the code. The project is an RFC-compliant IRC server written in C++98, using a single-process, non-blocking I/O multiplexing model with `poll()`.

---

## 1. Core Architecture Overview

The system is designed with a strict composition model, where the `Server` is the top-level owner of all resources. No raw `new` operations are used outside of defined ownership contexts, and memory leaks are prevented by a centralized map architecture. 

### Key Components:
- **`main.cpp`**: Entry point. Parses CLI arguments (`./ircserv <port> <password>`) and instantiates the `Server`.
- **`Server.hpp` / `Server.cpp`**: The heart of the program. It owns all `Client`s, `Channel`s, and the single `poll()` loop. It manages accepting new connections, network I/O, and tracking system state.
- **`Client.hpp` / `Client.cpp`**: Represents a single connected user. It stores connection state, read/write buffers, and the user's current status (nickname, username, channels joined, registration state).
- **`Channel.hpp` / `Channel.cpp`**: Manages a chat room. It tracks members, operators, channel modes (`+i, +t, +k, +o, +l`), and provides a `broadcast()` helper to send messages to all users in the channel.

---

## 2. Network I/O & The `poll()` Loop

The 42 `ft_irc` project strictly requires non-blocking sockets and a single `poll()` call to manage the listening socket and all active client connections.

### How to review the network flow:
1. **Startup**: Check `Server::init()`. You will see `socket()`, `setsockopt(SO_REUSEADDR)`, `fcntl(O_NONBLOCK)`, `bind()`, and `listen()`. The listener is added to `_pfds`.
2. **Event Loop**: Look at `Server::run()`. This is the single `poll()` loop:
   - If the listen socket has `POLLIN`, `Server::acceptClient()` is called.
   - For client sockets, if `POLLIN` is active, we trigger `handleRead()`.
   - If `POLLOUT` is active, we trigger `handleWrite()`.
   - Any disconnects (`POLLHUP`, `POLLERR`, or a failed `recv()`) queue the client for deferred deletion to avoid invalidating iterators mid-loop.
3. **Buffering**: In `handleRead()`, we append incoming data to the `Client`'s `read_buf`. We never assume a full line arrives in one `recv()`.

---

## 3. Parsing & Command Dispatch

Because TCP guarantees byte-streams but not message boundaries, the server implements strict line-buffering. 

### How to review the parsing flow:
1. **Line Extraction**: Inside `Server::consumeCommands()`, the `Client`'s `read_buf` is searched for `\n`. If found, a full line is extracted.
2. **Tokenization**: The extracted line is passed to `Parser::tokenize()`.
   - **`Parser.hpp/.cpp`**: Converts raw string lines into a `Message` struct (`prefix`, `command`, `params[]`, `trailing`).
3. **Dispatch**: The parsed `Message` is given to `_dispatcher.dispatch(server, client, message)`.
   - **`CommandDispatcher.hpp/.cpp`**: A registry that maps string commands (e.g., `"JOIN"`) to their respective function handlers.

---

## 4. IRC Commands Implementations

All specific IRC commands are implemented as free functions mapped by the `CommandDispatcher`. They live in the `commands/` directory. Each file implements one command.

**Start your review here to understand specific IRC logic:**
- **Connection & Registration**: 
  - `commands/cmd_pass.cpp`
  - `commands/cmd_nick.cpp`
  - `commands/cmd_user.cpp`
  - `commands/cmd_cap.cpp` (Acknowledges `CAP LS` so modern clients proceed)
- **Channel Operations**:
  - `commands/cmd_join.cpp` (Channel creation and joining)
  - `commands/cmd_part.cpp` (Leaving channels)
  - `commands/cmd_topic.cpp` (Viewing/setting topics)
- **Messaging**:
  - `commands/cmd_privmsg.cpp` (User-to-user and user-to-channel messages)
  - `commands/cmd_ping.cpp` & `commands/cmd_quit.cpp`
- **Operator Commands**:
  - `commands/cmd_mode.cpp` (The most complex command! Parses and applies `itkol` channel modes)
  - `commands/cmd_kick.cpp`
  - `commands/cmd_invite.cpp`

---

## 5. Replies & Utils

- **`Reply.hpp` / `Reply.cpp`**: Contains all static helpers that generate RFC 2812 formatted string replies (e.g., `001 RPL_WELCOME`, `433 ERR_NICKNAMEINUSE`). This keeps the command handlers clean and avoids formatting numeric strings everywhere.
- **`Utils.hpp` / `Utils.cpp`**: Helper functions for things like case-insensitive string comparisons, splitting CSVs (useful for `JOIN #a,#b`), and validating character sets.

---

## Summary of Code Review Path

If you want to follow the life of a connection from start to finish, read in this order:
1. `main.cpp` -> `Server::init()` -> `Server::run()` -> `Server::acceptClient()`
2. `Server::handleRead()` -> `Server::consumeCommands()`
3. `Parser::tokenize()`
4. `CommandDispatcher::dispatch()`
5. An example command handler, like `commands/cmd_join.cpp`
6. View how the response is formulated in `Reply.cpp` and queued back via `Server::enableWrite()`.
