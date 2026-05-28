#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"

void cmd_quit(Server &srv, Client &cli, const Message &msg)
{
	std::string reason = "Quit";
	if (!msg.params.empty())
		reason = msg.params[0];

	std::string quitMsg = ":" + cli.getPrefix() + " QUIT :" + reason + "\r\n";

	// Broadcast to all channels the client is in
	srv.broadcastToClientChannels(cli, quitMsg, &cli);

	// Remove client from all channels
	srv.removeClientFromAllChannels(cli);

	// Send ERROR to the client
	srv.sendTo(cli, "ERROR :Closing link (" + cli.getPrefix() + ") [Quit: " + reason + "]\r\n");

	srv.markForDisconnect(cli);
}
