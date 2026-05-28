#ifndef REPLY_HPP
# define REPLY_HPP

# include "Client.hpp"
# include <string>

namespace Reply {

// Helper to build ":server NUMERIC nick :text\r\n"
std::string	numeric(const std::string &server, const std::string &code,
					 const std::string &nick, const std::string &text);

// ---- Welcome numerics (001-004) ----
std::string rpl_welcome(const std::string &srv, const Client &c);
std::string rpl_yourhost(const std::string &srv, const Client &c);
std::string rpl_created(const std::string &srv, const Client &c);
std::string rpl_myinfo(const std::string &srv, const Client &c);

// ---- Channel numerics ----
std::string rpl_notopic(const std::string &srv, const Client &c, const std::string &chan);
std::string rpl_topic(const std::string &srv, const Client &c, const std::string &chan, const std::string &topic);
std::string rpl_inviting(const std::string &srv, const Client &c, const std::string &nick, const std::string &chan);
std::string rpl_namreply(const std::string &srv, const Client &c, const std::string &chan, const std::string &names);
std::string rpl_endofnames(const std::string &srv, const Client &c, const std::string &chan);
std::string rpl_channelmodeis(const std::string &srv, const Client &c, const std::string &chan, const std::string &modes);

// ---- Error numerics ----
std::string err_nosuchnick(const std::string &srv, const Client &c, const std::string &nick);
std::string err_nosuchchannel(const std::string &srv, const Client &c, const std::string &chan);
std::string err_cannotsendtochan(const std::string &srv, const Client &c, const std::string &chan);
std::string err_norecipient(const std::string &srv, const Client &c, const std::string &cmd);
std::string err_notexttosend(const std::string &srv, const Client &c);
std::string err_unknowncommand(const std::string &srv, const Client &c, const std::string &cmd);
std::string err_nonicknamegiven(const std::string &srv, const Client &c);
std::string err_erroneusnickname(const std::string &srv, const Client &c, const std::string &nick);
std::string err_nicknameinuse(const std::string &srv, const Client &c, const std::string &nick);
std::string err_usernotinchannel(const std::string &srv, const Client &c, const std::string &nick, const std::string &chan);
std::string err_notonchannel(const std::string &srv, const Client &c, const std::string &chan);
std::string err_useronchannel(const std::string &srv, const Client &c, const std::string &nick, const std::string &chan);
std::string err_notregistered(const std::string &srv, const Client &c);
std::string err_needmoreparams(const std::string &srv, const Client &c, const std::string &cmd);
std::string err_alreadyregistered(const std::string &srv, const Client &c);
std::string err_passwdmismatch(const std::string &srv, const Client &c);
std::string err_channelisfull(const std::string &srv, const Client &c, const std::string &chan);
std::string err_unknownmode(const std::string &srv, const Client &c, char mode);
std::string err_inviteonlychan(const std::string &srv, const Client &c, const std::string &chan);
std::string err_badchannelkey(const std::string &srv, const Client &c, const std::string &chan);
std::string err_chanoprivsneeded(const std::string &srv, const Client &c, const std::string &chan);

}

#endif
