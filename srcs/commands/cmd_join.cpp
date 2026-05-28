#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Reply.hpp"
#include "Utils.hpp"

#include <map>

static void joinOneChannel(Server &srv, Client &cli, const std::string &chanName, const std::string &key)
{
	if (!Utils::isValidChannelName(chanName)) {
		srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, chanName));
		return;
	}

	Channel *chan = srv.findChannel(chanName);
	bool isNew = false;

	if (!chan) {
		chan = srv.createChannel(chanName);
		isNew = true;
		if (!chan)
			return;
	}

	// Already a member?
	if (chan->isMember(&cli))
		return;

	// Check invite-only
	if (chan->isInviteOnly() && !chan->isInvited(cli.getNick())) {
		srv.sendTo(cli, Reply::err_inviteonlychan(srv.getName(), cli, chanName));
		return;
	}

	// Check key
	if (chan->hasKey() && key != chan->getKey()) {
		srv.sendTo(cli, Reply::err_badchannelkey(srv.getName(), cli, chanName));
		return;
	}

	// Check user limit
	if (chan->hasUserLimit() && chan->memberCount() >= chan->getUserLimit()) {
		srv.sendTo(cli, Reply::err_channelisfull(srv.getName(), cli, chanName));
		return;
	}

	// Join
	chan->addMember(&cli);
	cli.addChannel(chanName);

	// First joiner gets op
	if (isNew)
		chan->addOperator(&cli);

	// Remove from invite list once joined
	chan->removeInvited(cli.getNick());

	// Broadcast JOIN to all members (including the joining client)
	std::string joinMsg = ":" + cli.getPrefix() + " JOIN :" + chan->getName() + "\r\n";
	chan->broadcast(joinMsg, NULL);
	srv.enableWriteAll(*chan, NULL);

	// Send TOPIC
	if (!chan->getTopic().empty())
		srv.sendTo(cli, Reply::rpl_topic(srv.getName(), cli, chan->getName(), chan->getTopic()));
	else
		srv.sendTo(cli, Reply::rpl_notopic(srv.getName(), cli, chan->getName()));

	// Send NAMES list
	std::string names;
	const std::map<int, Client *> &members = chan->getMembers();
	for (std::map<int, Client *>::const_iterator it = members.begin();
		 it != members.end(); ++it) {
		if (!names.empty())
			names += " ";
		if (chan->isOperator(it->second))
			names += "@";
		names += it->second->getNick();
	}
	srv.sendTo(cli, Reply::rpl_namreply(srv.getName(), cli, chan->getName(), names));
	srv.sendTo(cli, Reply::rpl_endofnames(srv.getName(), cli, chan->getName()));
}

void cmd_join(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "JOIN"));
		return;
	}

	// RFC 2812: JOIN 0 — leave all channels
	if (msg.params[0] == "0") {
		std::set<std::string> chans = cli.getChannels();
		for (std::set<std::string>::iterator it = chans.begin();
			 it != chans.end(); ++it) {
			Channel *chan = srv.findChannel(*it);
			if (!chan) continue;
			std::string partMsg = ":" + cli.getPrefix() + " PART "
								+ chan->getName() + " :Left all channels\r\n";
			chan->broadcast(partMsg, NULL);
			srv.enableWriteAll(*chan, NULL);
			chan->removeMember(&cli);
			cli.removeChannel(*it);
			if (chan->memberCount() == 0)
				srv.deleteChannel(*it);
		}
		return;
	}

	std::vector<std::string> channels = Utils::splitCSV(msg.params[0]);
	std::vector<std::string> keys;
	if (msg.params.size() > 1)
		keys = Utils::splitCSV(msg.params[1]);

	for (size_t i = 0; i < channels.size(); ++i) {
		std::string key;
		if (i < keys.size())
			key = keys[i];
		joinOneChannel(srv, cli, channels[i], key);
	}
}
