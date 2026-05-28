#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Reply.hpp"
#include "Utils.hpp"

// 311 RPL_WHOISUSER
static std::string rpl_whoisuser(const std::string &srv, const Client &c,
								 const Client &tgt)
{
	return ":" + srv + " 311 " + c.getNick() + " " + tgt.getNick()
		+ " " + tgt.getUser() + " " + tgt.getHost() + " * :"
		+ tgt.getRealName() + "\r\n";
}

// 312 RPL_WHOISSERVER
static std::string rpl_whoisserver(const std::string &srv, const Client &c,
								   const Client &tgt)
{
	return ":" + srv + " 312 " + c.getNick() + " " + tgt.getNick()
		+ " " + srv + " :IRC server\r\n";
}

// 319 RPL_WHOISCHANNELS
static std::string rpl_whoischannels(const std::string &srv, const Client &c,
									 const Client &tgt, const std::string &chans)
{
	return ":" + srv + " 319 " + c.getNick() + " " + tgt.getNick()
		+ " :" + chans + "\r\n";
}

// 318 RPL_ENDOFWHOIS
static std::string rpl_endofwhois(const std::string &srv, const Client &c,
								  const std::string &nick)
{
	return ":" + srv + " 318 " + c.getNick() + " " + nick
		+ " :End of /WHOIS list\r\n";
}

void cmd_whois(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "WHOIS"));
		return;
	}

	const std::string &nick = msg.params[0];
	Client *tgt = srv.findClientByNick(nick);
	if (!tgt) {
		srv.sendTo(cli, Reply::err_nosuchnick(srv.getName(), cli, nick));
		srv.sendTo(cli, rpl_endofwhois(srv.getName(), cli, nick));
		return;
	}

	srv.sendTo(cli, rpl_whoisuser(srv.getName(), cli, *tgt));
	srv.sendTo(cli, rpl_whoisserver(srv.getName(), cli, *tgt));

	// List channels the target is in
	const std::set<std::string> &chans = tgt->getChannels();
	if (!chans.empty()) {
		std::string chanList;
		for (std::set<std::string>::const_iterator it = chans.begin();
			 it != chans.end(); ++it) {
			if (!chanList.empty()) chanList += " ";
			Channel *chan = srv.findChannel(*it);
			if (chan && chan->isOperator(tgt))
				chanList += "@";
			chanList += *it;
		}
		srv.sendTo(cli, rpl_whoischannels(srv.getName(), cli, *tgt, chanList));
	}

	srv.sendTo(cli, rpl_endofwhois(srv.getName(), cli, tgt->getNick()));
}
