#include "Client.hpp"

Client::Client(int fd, const std::string &host)
	: _fd(fd), _state(STATE_NEW), _host(host)
{}

Client::~Client()
{}

int	Client::getFd() const { return _fd; }
Client::State	Client::getState() const { return _state; }
const std::string	&Client::getNick() const { return _nick; }
const std::string	&Client::getUser() const { return _user; }
const std::string	&Client::getRealName() const { return _realname; }
const std::string	&Client::getHost() const { return _host; }

std::string	Client::getPrefix() const
{
	return _nick + "!" + _user + "@" + _host;
}

bool	Client::hasNick() const { return !_nick.empty(); }
bool	Client::hasUser() const { return !_user.empty(); }

void	Client::setState(State s) { _state = s; }
void	Client::setNick(const std::string &nick) { _nick = nick; }
void	Client::setUser(const std::string &user) { _user = user; }
void	Client::setRealName(const std::string &realname) { _realname = realname; }

std::string	&Client::readBuf() { return _read_buf; }
std::string	&Client::writeBuf() { return _write_buf; }

void	Client::appendToWrite(const std::string &data)
{
	_write_buf += data;
}

void	Client::addChannel(const std::string &name)
{
	_channels.insert(Utils::toLower(name));
}

void	Client::removeChannel(const std::string &name)
{
	_channels.erase(Utils::toLower(name));
}

const std::set<std::string>	&Client::getChannels() const
{
	return _channels;
}
