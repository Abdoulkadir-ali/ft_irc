#include "Channel.hpp"

Channel::Channel(const std::string &name)
	: _name(name), _user_limit(0), _invite_only(false), _topic_restricted(false)
{}

Channel::~Channel()
{}

const std::string	&Channel::getName() const { return _name; }
const std::string	&Channel::getTopic() const { return _topic; }
const std::string	&Channel::getKey() const { return _key; }
int					Channel::getUserLimit() const { return _user_limit; }
bool				Channel::isInviteOnly() const { return _invite_only; }
bool				Channel::isTopicRestricted() const { return _topic_restricted; }
bool				Channel::hasKey() const { return !_key.empty(); }
bool				Channel::hasUserLimit() const { return _user_limit > 0; }

std::string	Channel::getModeString() const
{
	std::string modes = "+";
	std::string params;
	if (_invite_only) modes += "i";
	if (_topic_restricted) modes += "t";
	if (!_key.empty()) {
		modes += "k";
		params += " " + _key;
	}
	if (_user_limit > 0) {
		modes += "l";
		params += " " + Utils::intToStr(_user_limit);
	}
	if (modes == "+")
		return "+";
	return modes + params;
}

void	Channel::setTopic(const std::string &topic) { _topic = topic; }
void	Channel::setKey(const std::string &key) { _key = key; }
void	Channel::removeKey() { _key.clear(); }
void	Channel::setUserLimit(int limit) { _user_limit = limit; }
void	Channel::removeUserLimit() { _user_limit = 0; }
void	Channel::setInviteOnly(bool on) { _invite_only = on; }
void	Channel::setTopicRestricted(bool on) { _topic_restricted = on; }

void	Channel::addMember(Client *client)
{
	_members[client->getFd()] = client;
}

void	Channel::removeMember(Client *client)
{
	_members.erase(client->getFd());
	_operators.erase(client->getFd());
}

bool	Channel::isMember(Client *client) const
{
	return _members.find(client->getFd()) != _members.end();
}

int	Channel::memberCount() const
{
	return static_cast<int>(_members.size());
}

const std::map<int, Client *>	&Channel::getMembers() const
{
	return _members;
}

void	Channel::addOperator(Client *client)
{
	_operators.insert(client->getFd());
}

void	Channel::removeOperator(Client *client)
{
	_operators.erase(client->getFd());
}

bool	Channel::isOperator(Client *client) const
{
	return _operators.find(client->getFd()) != _operators.end();
}

void	Channel::addInvited(const std::string &nick)
{
	_invited.insert(Utils::toLower(nick));
}

bool	Channel::isInvited(const std::string &nick) const
{
	return _invited.find(Utils::toLower(nick)) != _invited.end();
}

void	Channel::removeInvited(const std::string &nick)
{
	_invited.erase(Utils::toLower(nick));
}

void	Channel::broadcast(const std::string &message, Client *except)
{
	for (std::map<int, Client *>::const_iterator it = _members.begin();
		 it != _members.end(); ++it) {
		if (except && it->second == except)
			continue;
		it->second->appendToWrite(message);
	}
}
