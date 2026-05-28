#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Reply.hpp"
#include "Utils.hpp"

void cmd_part(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "PART"));
		return;
	}

	std::string reason;
	if (msg.params.size() > 1)
		reason = msg.params[1];

	std::vector<std::string> channels = Utils::splitCSV(msg.params[0]);

	for (size_t i = 0; i < channels.size(); ++i) {
		Channel *chan = srv.findChannel(channels[i]);
		if (!chan) {
			srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, channels[i]));
			continue;
		}
		if (!chan->isMember(&cli)) {
			srv.sendTo(cli, Reply::err_notonchannel(srv.getName(), cli, channels[i]));
			continue;
		}

		// Broadcast PART to channel
		std::string partMsg = ":" + cli.getPrefix() + " PART " + chan->getName();
		if (!reason.empty())
			partMsg += " :" + reason;
		partMsg += "\r\n";
		chan->broadcast(partMsg, NULL);
		srv.enableWriteAll(*chan, NULL);

		// Remove from channel
		chan->removeMember(&cli);
		cli.removeChannel(channels[i]);

		// Delete empty channel
		if (chan->memberCount() == 0)
			srv.deleteChannel(channels[i]);
	}
}
