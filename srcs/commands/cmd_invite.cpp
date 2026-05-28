#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Reply.hpp"

void cmd_invite(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.size() < 2) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "INVITE"));
		return;
	}
	const std::string &targetNick = msg.params[0];
	const std::string &chanName = msg.params[1];
	Channel *chan = srv.findChannel(chanName);
	if (!chan) {
		srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, chanName));
		return;
	}
	if (!chan->isMember(&cli)) {
		srv.sendTo(cli, Reply::err_notonchannel(srv.getName(), cli, chanName));
		return;
	}
	if (chan->isInviteOnly() && !chan->isOperator(&cli)) {
		srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chanName));
		return;
	}
	Client *target = srv.findClientByNick(targetNick);
	if (!target) {
		srv.sendTo(cli, Reply::err_nosuchnick(srv.getName(), cli, targetNick));
		return;
	}
	if (chan->isMember(target)) {
		srv.sendTo(cli, Reply::err_useronchannel(srv.getName(), cli, targetNick, chanName));
		return;
	}
	chan->addInvited(targetNick);
	srv.sendTo(cli, Reply::rpl_inviting(srv.getName(), cli, target->getNick(), chan->getName()));
	srv.sendTo(*target, ":" + cli.getPrefix() + " INVITE " + target->getNick() + " :" + chan->getName() + "\r\n");
}
