#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"

void cmd_ping(Server &srv, Client &cli, const Message &msg)
{
	if (msg.params.empty()) {
		srv.sendTo(cli, ":" + srv.getName() + " PONG " + srv.getName() + "\r\n");
	} else {
		srv.sendTo(cli, ":" + srv.getName() + " PONG " + srv.getName() + " :" + msg.params[0] + "\r\n");
	}
}

void cmd_pong(Server &, Client &, const Message &)
{
	// We don't send PINGs to clients, so no action needed on PONG
}
