#include "Reply.hpp"

namespace Reply {

static std::string nick_or_star(const Client &c)
{
	return c.hasNick() ? c.getNick() : std::string("*");
}

std::string	numeric(const std::string &server, const std::string &code,
					 const std::string &nick, const std::string &text)
{
	return ":" + server + " " + code + " " + nick + " " + text + "\r\n";
}

// ---- Welcome numerics (001-004) ----

std::string rpl_welcome(const std::string &srv, const Client &c)
{
	return numeric(srv, "001", c.getNick(),
		":Welcome to the Internet Relay Network " + c.getPrefix());
}

std::string rpl_yourhost(const std::string &srv, const Client &c)
{
	return numeric(srv, "002", c.getNick(),
		":Your host is " + srv + ", running version 1.0");
}

std::string rpl_created(const std::string &srv, const Client &c)
{
	return numeric(srv, "003", c.getNick(),
		":This server was created today");
}

std::string rpl_myinfo(const std::string &srv, const Client &c)
{
	return numeric(srv, "004", c.getNick(),
		srv + " 1.0 o itkol");
}

// ---- Channel numerics ----

std::string rpl_notopic(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "331", c.getNick(), chan + " :No topic is set");
}

std::string rpl_topic(const std::string &srv, const Client &c, const std::string &chan, const std::string &topic)
{
	return numeric(srv, "332", c.getNick(), chan + " :" + topic);
}

std::string rpl_inviting(const std::string &srv, const Client &c, const std::string &nick, const std::string &chan)
{
	return numeric(srv, "341", c.getNick(), nick + " " + chan);
}

std::string rpl_namreply(const std::string &srv, const Client &c, const std::string &chan, const std::string &names)
{
	return numeric(srv, "353", c.getNick(), "= " + chan + " :" + names);
}

std::string rpl_endofnames(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "366", c.getNick(), chan + " :End of /NAMES list");
}

std::string rpl_channelmodeis(const std::string &srv, const Client &c, const std::string &chan, const std::string &modes)
{
	return numeric(srv, "324", c.getNick(), chan + " " + modes);
}

// ---- Error numerics ----

std::string err_nosuchnick(const std::string &srv, const Client &c, const std::string &nick)
{
	return numeric(srv, "401", nick_or_star(c), nick + " :No such nick/channel");
}

std::string err_nosuchchannel(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "403", nick_or_star(c), chan + " :No such channel");
}

std::string err_cannotsendtochan(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "404", nick_or_star(c), chan + " :Cannot send to channel");
}

std::string err_norecipient(const std::string &srv, const Client &c, const std::string &cmd)
{
	return numeric(srv, "411", nick_or_star(c), ":No recipient given (" + cmd + ")");
}

std::string err_notexttosend(const std::string &srv, const Client &c)
{
	return numeric(srv, "412", nick_or_star(c), ":No text to send");
}

std::string err_unknowncommand(const std::string &srv, const Client &c, const std::string &cmd)
{
	return numeric(srv, "421", nick_or_star(c), cmd + " :Unknown command");
}

std::string err_nonicknamegiven(const std::string &srv, const Client &c)
{
	return numeric(srv, "431", nick_or_star(c), ":No nickname given");
}

std::string err_erroneusnickname(const std::string &srv, const Client &c, const std::string &nick)
{
	return numeric(srv, "432", nick_or_star(c), nick + " :Erroneous nickname");
}

std::string err_nicknameinuse(const std::string &srv, const Client &c, const std::string &nick)
{
	return numeric(srv, "433", nick_or_star(c), nick + " :Nickname is already in use");
}

std::string err_usernotinchannel(const std::string &srv, const Client &c, const std::string &nick, const std::string &chan)
{
	return numeric(srv, "441", nick_or_star(c), nick + " " + chan + " :They aren't on that channel");
}

std::string err_notonchannel(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "442", nick_or_star(c), chan + " :You're not on that channel");
}

std::string err_useronchannel(const std::string &srv, const Client &c, const std::string &nick, const std::string &chan)
{
	return numeric(srv, "443", nick_or_star(c), nick + " " + chan + " :is already on channel");
}

std::string err_notregistered(const std::string &srv, const Client &c)
{
	return numeric(srv, "451", nick_or_star(c), ":You have not registered");
}

std::string err_needmoreparams(const std::string &srv, const Client &c, const std::string &cmd)
{
	return numeric(srv, "461", nick_or_star(c), cmd + " :Not enough parameters");
}

std::string err_alreadyregistered(const std::string &srv, const Client &c)
{
	return numeric(srv, "462", nick_or_star(c), ":You may not reregister");
}

std::string err_passwdmismatch(const std::string &srv, const Client &c)
{
	return numeric(srv, "464", nick_or_star(c), ":Password incorrect");
}

std::string err_channelisfull(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "471", nick_or_star(c), chan + " :Cannot join channel (+l)");
}

std::string err_unknownmode(const std::string &srv, const Client &c, char mode)
{
	std::string ms(1, mode);
	return numeric(srv, "472", nick_or_star(c), ms + " :is unknown mode char to me");
}

std::string err_inviteonlychan(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "473", nick_or_star(c), chan + " :Cannot join channel (+i)");
}

std::string err_badchannelkey(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "475", nick_or_star(c), chan + " :Cannot join channel (+k)");
}

std::string err_chanoprivsneeded(const std::string &srv, const Client &c, const std::string &chan)
{
	return numeric(srv, "482", nick_or_star(c), chan + " :You're not channel operator");
}

}
