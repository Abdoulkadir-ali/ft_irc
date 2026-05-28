#ifndef COMMANDS_HPP
# define COMMANDS_HPP

# include "Message.hpp"

class Server;
class Client;

// Each command handler has the same signature
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

#endif
