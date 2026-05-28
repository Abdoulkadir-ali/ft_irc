#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Reply.hpp"

void cmd_topic(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "TOPIC"));
		return;
	}

	const std::string &chanName = msg.params[0];
	Channel *chan = srv.findChannel(chanName);
	if (!chan) {
		srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, chanName));
		return;
	}
	if (!chan->isMember(&cli)) {
		srv.sendTo(cli, Reply::err_notonchannel(srv.getName(), cli, chanName));
		return;
	}

	// Query topic (no second param)
	if (msg.params.size() < 2) {
		if (chan->getTopic().empty())
			srv.sendTo(cli, Reply::rpl_notopic(srv.getName(), cli, chan->getName()));
		else
			srv.sendTo(cli, Reply::rpl_topic(srv.getName(), cli, chan->getName(), chan->getTopic()));
		return;
	}

	// Set topic
	if (chan->isTopicRestricted() && !chan->isOperator(&cli)) {
		srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chan->getName()));
		return;
	}

	chan->setTopic(msg.params[1]);

	// Broadcast topic change
	std::string topicMsg = ":" + cli.getPrefix() + " TOPIC " + chan->getName() + " :" + msg.params[1] + "\r\n";
	chan->broadcast(topicMsg, NULL);
	srv.enableWriteAll(*chan, NULL);
}
