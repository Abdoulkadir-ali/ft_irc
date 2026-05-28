#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Reply.hpp"
#include "Utils.hpp"

void cmd_nick(Server &srv, Client &cli, const Message &msg)
{
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_nonicknamegiven(srv.getName(), cli));
		return;
	}

	const std::string &nick = msg.params[0];

	if (!Utils::isValidNick(nick)) {
		srv.sendTo(cli, Reply::err_erroneusnickname(srv.getName(), cli, nick));
		return;
	}

	// Check uniqueness (case-insensitive)
	Client *existing = srv.findClientByNick(nick);
	if (existing && existing != &cli) {
		srv.sendTo(cli, Reply::err_nicknameinuse(srv.getName(), cli, nick));
		return;
	}

	std::string oldNick = cli.getNick();

	// If already registered, broadcast NICK change to all shared channels
	if (cli.getState() == Client::STATE_REGISTERED) {
		std::string oldPrefix = cli.getPrefix();
		cli.setNick(nick);
		srv.updateNickIndex(cli, oldNick, nick);
		std::string change = ":" + oldPrefix + " NICK :" + nick + "\r\n";
		// Send to the client itself
		srv.sendTo(cli, change);
		// Broadcast to all channels this client is in
		srv.broadcastToClientChannels(cli, change, &cli);
	} else {
		cli.setNick(nick);
		// Try to complete registration (will add to nick index if successful)
		srv.tryRegister(cli);
	}
}
