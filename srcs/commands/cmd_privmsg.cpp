#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "DccRelay.hpp"
#include "Reply.hpp"
#include "Channel.hpp"
#include "Utils.hpp"

// Returns true if text is a DCC SEND CTCP message
static bool isDccSend(const std::string &text)
{
	return text.size() > 10
		&& text[0] == '\x01'
		&& text.substr(1, 8) == "DCC SEND";
}

void cmd_privmsg(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_norecipient(srv.getName(), cli, "PRIVMSG"));
		return;
	}
	if (msg.params.size() < 2) {
		srv.sendTo(cli, Reply::err_notexttosend(srv.getName(), cli));
		return;
	}

	const std::string &target = msg.params[0];
	const std::string &text   = msg.params[1];

	// Channel message
	if (target[0] == '#' || target[0] == '&') {
		Channel *chan = srv.findChannel(target);
		if (!chan) {
			srv.sendTo(cli, Reply::err_nosuchnick(srv.getName(), cli, target));
			return;
		}
		if (!chan->isMember(&cli)) {
			srv.sendTo(cli, Reply::err_cannotsendtochan(srv.getName(), cli, target));
			return;
		}
		std::string fullMsg = ":" + cli.getPrefix() + " PRIVMSG " + target + " :" + text + "\r\n";
		chan->broadcast(fullMsg, &cli);
		srv.enableWriteAll(*chan, &cli);
	} else {
		// User message
		Client *dest = srv.findClientByNick(target);
		if (!dest) {
			srv.sendTo(cli, Reply::err_nosuchnick(srv.getName(), cli, target));
			return;
		}
		// Intercept DCC SEND for relay (bonus: file transfer)
		if (isDccSend(text)) {
			std::string relayed = srv.getDccRelay().intercept(cli, *dest, text);
			if (!relayed.empty()) {
				std::string fullMsg = ":" + cli.getPrefix() + " PRIVMSG "
									+ target + " :" + relayed + "\r\n";
				srv.sendTo(*dest, fullMsg);
				return;
			}
		}
		std::string fullMsg = ":" + cli.getPrefix() + " PRIVMSG " + target + " :" + text + "\r\n";
		srv.sendTo(*dest, fullMsg);
	}
}



void cmd_notice(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED)
		return; // NOTICE never generates errors
	if (msg.params.size() < 2)
		return;

	const std::string &target = msg.params[0];
	const std::string &text = msg.params[1];
	std::string fullMsg = ":" + cli.getPrefix() + " NOTICE " + target + " :" + text + "\r\n";

	if (target[0] == '#' || target[0] == '&') {
		Channel *chan = srv.findChannel(target);
		if (!chan || !chan->isMember(&cli))
			return;
		chan->broadcast(fullMsg, &cli);
		srv.enableWriteAll(*chan, &cli);
	} else {
		Client *dest = srv.findClientByNick(target);
		if (!dest)
			return;
		srv.sendTo(*dest, fullMsg);
	}
}
