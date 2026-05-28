#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Reply.hpp"

void cmd_kick(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.size() < 2) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "KICK"));
		return;
	}

	const std::string &chanName = msg.params[0];
	const std::string &targetNick = msg.params[1];
	std::string reason = cli.getNick();
	if (msg.params.size() > 2)
		reason = msg.params[2];

	Channel *chan = srv.findChannel(chanName);
	if (!chan) {
		srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, chanName));
		return;
	}
	if (!chan->isMember(&cli)) {
		srv.sendTo(cli, Reply::err_notonchannel(srv.getName(), cli, chanName));
		return;
	}
	if (!chan->isOperator(&cli)) {
		srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chanName));
		return;
	}

	Client *target = srv.findClientByNick(targetNick);
	if (!target || !chan->isMember(target)) {
		srv.sendTo(cli, Reply::err_usernotinchannel(srv.getName(), cli, targetNick, chanName));
		return;
	}

	// Broadcast KICK
	std::string kickMsg = ":" + cli.getPrefix() + " KICK " + chan->getName() + " " + target->getNick() + " :" + reason + "\r\n";
	chan->broadcast(kickMsg, NULL);
	srv.enableWriteAll(*chan, NULL);

	// Remove target from channel
	chan->removeMember(target);
	target->removeChannel(chanName);

	// Delete empty channel
	if (chan->memberCount() == 0)
		srv.deleteChannel(chanName);
}
