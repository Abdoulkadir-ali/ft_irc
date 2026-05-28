#include "Server.hpp"
#include "DccRelay.hpp"

namespace {
	volatile sig_atomic_t	g_stop = 0;
	void	onSignal(int) { g_stop = 1; }
}

Server::Server(int port, const std::string &password)
	: _port(port), _password(password), _server_name("ircserv"),
	  _listen_fd(-1), _server_ip(0), _start_time(std::time(NULL)),
	  _bot(*this, _password), _dcc_relay(*this), _bot_pipe_fd(-1)
{
	registerCommands();
}

Server::~Server()
{
	for (std::map<int, Client *>::iterator it = _clients.begin();
		 it != _clients.end(); ++it) {
		close(it->first);
		delete it->second;
	}
	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		 it != _channels.end(); ++it)
		delete it->second;
	if (_listen_fd >= 0)
		close(_listen_fd);
}

void	Server::init()
{
	std::signal(SIGINT,  onSignal);
	std::signal(SIGTERM, onSignal);
	std::signal(SIGPIPE, SIG_IGN);

	_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (_listen_fd < 0)
		throw std::runtime_error("socket() failed");

	int opt = 1;
	if (setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");

	if (fcntl(_listen_fd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl(O_NONBLOCK) failed");

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(static_cast<unsigned short>(_port));

	if (bind(_listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
		throw std::runtime_error("bind() failed");
	{
		struct sockaddr_in bound;
		socklen_t len = sizeof(bound);
		if (getsockname(_listen_fd, reinterpret_cast<struct sockaddr *>(&bound), &len) == 0)
			_server_ip = ntohl(bound.sin_addr.s_addr);
		if (_server_ip == 0)
			_server_ip = (127u << 24) | 1u;
	}

	if (listen(_listen_fd, SOMAXCONN) < 0)
		throw std::runtime_error("listen() failed");

	{
		pollfd pfd;
		pfd.fd = _listen_fd; pfd.events = POLLIN; pfd.revents = 0;
		_pfds.push_back(pfd);
		_fd_to_pfd_idx[_listen_fd] = 0;
	}

	_dcc_relay.setServerIp(_server_ip);

	_bot.init();
	_bot_pipe_fd = _bot.getBotFd();

	{
		Client *bc = new Client(_bot.getSrvFd(), "127.0.0.1");
		_clients[_bot.getSrvFd()] = bc;
		pollfd bpfd;
		bpfd.fd = _bot.getSrvFd(); bpfd.events = POLLIN; bpfd.revents = 0;
		_pfds.push_back(bpfd);
		_fd_to_pfd_idx[_bot.getSrvFd()] = _pfds.size() - 1;
	}
	{
		pollfd bpfd;
		bpfd.fd = _bot_pipe_fd; bpfd.events = POLLIN; bpfd.revents = 0;
		_pfds.push_back(bpfd);
	}

	std::cerr << "[ircserv] listening on port " << _port << std::endl;
}

void	Server::run()
{
	while (!g_stop) {
		if (_pfds.empty())
			break;

		int n = poll(&_pfds[0], static_cast<nfds_t>(_pfds.size()), -1);
		if (n < 0) {
			if (errno == EINTR) { if (g_stop) break; continue; }
			break;
		}

		for (size_t i = 0; i < _pfds.size(); ++i) {
			if (_pfds[i].revents == 0)
				continue;

			int fd = _pfds[i].fd;
			try {
				if (_pfds[i].fd == _listen_fd) {
					if (_pfds[i].revents & POLLIN) acceptClient();
					continue;
				}
				if (_pfds[i].fd == _bot_pipe_fd) {
					if (_pfds[i].revents & POLLIN) _bot.onRead();
					continue;
				}
				if (_dcc_relay.hasFd(_pfds[i].fd)) {
					_dcc_relay.handleFd(_pfds[i].fd, _pfds[i].revents);
					continue;
				}

				if (_dead_fds.find(fd) != _dead_fds.end())
					continue;

				std::map<int, Client *>::iterator cit = _clients.find(fd);
				if (cit == _clients.end())
					continue;
				Client &c = *cit->second;

				if (_pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
					_dead_fds.insert(fd); continue;
				}
				if (_pfds[i].revents & POLLIN) {
					handleRead(c);
					if (_dead_fds.find(fd) != _dead_fds.end()) continue;
				}
				if (_pfds[i].revents & POLLOUT)
					handleWrite(c);
			} catch (const std::exception &e) {
				std::cerr << "[ircserv] runtime error on fd " << fd
						  << ": " << e.what() << std::endl;
				if (fd != _listen_fd && fd != _bot_pipe_fd)
					_dead_fds.insert(fd);
			} catch (...) {
				std::cerr << "[ircserv] unknown runtime error on fd "
						  << fd << std::endl;
				if (fd != _listen_fd && fd != _bot_pipe_fd)
					_dead_fds.insert(fd);
			}
		}

		reapDisconnected();
	}
	std::cerr << "[ircserv] shutting down" << std::endl;
}

void	Server::acceptClient()
{
	while (true) {
		struct sockaddr_in cli_addr;
		socklen_t len = sizeof(cli_addr);
		int fd = accept(_listen_fd,
						reinterpret_cast<struct sockaddr *>(&cli_addr), &len);
		if (fd < 0) break;

		if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); continue; }

		std::string host = inet_ntoa(cli_addr.sin_addr);
		Client *client = NULL;
		bool insertedClient = false;
		try {
			client = new Client(fd, host);
			_clients[fd] = client;
			insertedClient = true;

			pollfd pfd;
			pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
			_pfds.push_back(pfd);
			_fd_to_pfd_idx[fd] = _pfds.size() - 1;
		} catch (const std::exception &e) {
			std::cerr << "[ircserv] failed to accept client on fd " << fd
					  << ": " << e.what() << std::endl;
			if (insertedClient)
				_clients.erase(fd);
			delete client;
			close(fd);
			continue;
		} catch (...) {
			std::cerr << "[ircserv] failed to accept client on fd " << fd
					  << ": unknown error" << std::endl;
			if (insertedClient)
				_clients.erase(fd);
			delete client;
			close(fd);
			continue;
		}

		std::cerr << "[ircserv] new connection from " << host
				  << ":" << ntohs(cli_addr.sin_port)
				  << " (fd " << fd << ")" << std::endl;
	}
}

void	Server::handleRead(Client &client)
{
	char buf[4096];
	ssize_t n = recv(client.getFd(), buf, sizeof(buf), 0);
	if (n <= 0) { _dead_fds.insert(client.getFd()); return; }

	client.readBuf().append(buf, static_cast<size_t>(n));
	if (client.readBuf().size() > 65536) { _dead_fds.insert(client.getFd()); return; }

	consumeCommands(client);
}

void	Server::handleWrite(Client &client)
{
	if (client.writeBuf().empty()) return;

	ssize_t n = send(client.getFd(), client.writeBuf().c_str(),
					 client.writeBuf().size(), 0);
	if (n <= 0) { _dead_fds.insert(client.getFd()); return; }

	client.writeBuf().erase(0, static_cast<size_t>(n));
	if (client.writeBuf().empty()) {
		std::map<int, size_t>::iterator it = _fd_to_pfd_idx.find(client.getFd());
		if (it != _fd_to_pfd_idx.end())
			_pfds[it->second].events = POLLIN;
	}
}

void	Server::consumeCommands(Client &client)
{
	std::string line;
	while (Parser::extractLine(client.readBuf(), line)) {
		if (line.empty()) continue;
		if (line.size() > 512) line.resize(512);
		Message msg = Parser::tokenize(line);
		if (msg.command.empty()) continue;

		if (client.getState() != Client::STATE_REGISTERED) {
			if (msg.command != "PASS" && msg.command != "NICK"
				&& msg.command != "USER" && msg.command != "CAP"
				&& msg.command != "QUIT" && msg.command != "PING"
				&& msg.command != "PONG") {
				sendTo(client, Reply::err_notregistered(_server_name, client));
				continue;
			}
		}

		_dispatcher.dispatch(*this, client, msg);
		if (client.getState() == Client::STATE_DEAD) break;
	}
}

void	Server::disconnectClient(int fd)
{
	std::map<int, Client *>::iterator it = _clients.find(fd);
	if (it == _clients.end()) return;

	Client *client = it->second;
	std::cerr << "[ircserv] disconnecting fd " << fd;
	if (client->hasNick()) std::cerr << " (" << client->getNick() << ")";
	std::cerr << std::endl;

	if (!client->writeBuf().empty())
		send(fd, client->writeBuf().c_str(), client->writeBuf().size(),
			 MSG_NOSIGNAL | MSG_DONTWAIT);

	if (client->hasNick())
		_nick_index.erase(Utils::toLower(client->getNick()));
	removeClientFromAllChannels(*client);
	close(fd);

	std::map<int, size_t>::iterator idx_it = _fd_to_pfd_idx.find(fd);
	if (idx_it != _fd_to_pfd_idx.end()) {
		size_t idx  = idx_it->second;
		size_t last = _pfds.size() - 1;
		if (idx != last) {
			_pfds[idx] = _pfds[last];
			_fd_to_pfd_idx[_pfds[idx].fd] = idx;
		}
		_pfds.pop_back();
		_fd_to_pfd_idx.erase(idx_it);
	}

	delete client;
	_clients.erase(it);
}

void	Server::reapDisconnected()
{
	std::set<int> dead(_dead_fds);
	_dead_fds.clear();
	for (std::set<int>::iterator it = dead.begin(); it != dead.end(); ++it)
		disconnectClient(*it);
}

const std::string	&Server::getName() const     { return _server_name; }
const std::string	&Server::getPassword() const { return _password; }
uint32_t			Server::getServerIp() const  { return _server_ip; }
Bot					&Server::getBot()      { return _bot; }
DccRelay			&Server::getDccRelay() { return _dcc_relay; }

void	Server::sendTo(Client &client, const std::string &message)
{
	client.appendToWrite(message);
	enableWrite(client);
}

void	Server::markForDisconnect(Client &client)
{
	client.setState(Client::STATE_DEAD);
	_dead_fds.insert(client.getFd());
}

void	Server::enableWrite(Client &client)
{
	std::map<int, size_t>::iterator it = _fd_to_pfd_idx.find(client.getFd());
	if (it != _fd_to_pfd_idx.end())
		_pfds[it->second].events |= POLLOUT;
}

void	Server::enableWriteAll(Channel &chan, Client *except)
{
	const std::map<int, Client *> &members = chan.getMembers();
	for (std::map<int, Client *>::const_iterator it = members.begin();
		 it != members.end(); ++it) {
		if (except && it->second == except) continue;
		enableWrite(*it->second);
	}
}

void	Server::tryRegister(Client &client)
{
	if (client.getState() == Client::STATE_REGISTERED) return;
	if (client.getState() != Client::STATE_PASS_OK)    return;
	if (!client.hasNick() || !client.hasUser())        return;

	client.setState(Client::STATE_REGISTERED);
	_nick_index[Utils::toLower(client.getNick())] = &client;

	sendTo(client, Reply::rpl_welcome(_server_name, client));
	sendTo(client, Reply::rpl_yourhost(_server_name, client));
	sendTo(client, Reply::rpl_created(_server_name, client));
	sendTo(client, Reply::rpl_myinfo(_server_name, client));
}

Client	*Server::findClientByNick(const std::string &nick)
{
	std::string lower = Utils::toLower(nick);
	std::map<std::string, Client *>::iterator it = _nick_index.find(lower);
	if (it != _nick_index.end()) return it->second;

	for (std::map<int, Client *>::iterator cit = _clients.begin();
		 cit != _clients.end(); ++cit) {
		if (Utils::ircCaseEqual(cit->second->getNick(), nick))
			return cit->second;
	}
	return NULL;
}

void	Server::updateNickIndex(Client &client, const std::string &oldNick,
								const std::string &newNick)
{
	if (!oldNick.empty()) _nick_index.erase(Utils::toLower(oldNick));
	if (!newNick.empty() && client.getState() == Client::STATE_REGISTERED)
		_nick_index[Utils::toLower(newNick)] = &client;
}

Channel	*Server::findChannel(const std::string &name)
{
	std::string lower = Utils::toLower(name);
	std::map<std::string, Channel *>::iterator it = _channels.find(lower);
	return it != _channels.end() ? it->second : NULL;
}

Channel	*Server::createChannel(const std::string &name)
{
	std::string lower = Utils::toLower(name);
	try {
		Channel *chan = new Channel(name);
		_channels[lower] = chan;
		return chan;
	} catch (const std::exception &e) {
		std::cerr << "[ircserv] failed to create channel '" << name
				  << "': " << e.what() << std::endl;
	} catch (...) {
		std::cerr << "[ircserv] failed to create channel '" << name
				  << "': unknown error" << std::endl;
	}
	return NULL;
}

void	Server::deleteChannel(const std::string &name)
{
	std::string lower = Utils::toLower(name);
	std::map<std::string, Channel *>::iterator it = _channels.find(lower);
	if (it != _channels.end()) { delete it->second; _channels.erase(it); }
}

void	Server::removeClientFromAllChannels(Client &client)
{
	std::set<std::string> chans = client.getChannels();
	for (std::set<std::string>::iterator it = chans.begin();
		 it != chans.end(); ++it) {
		Channel *chan = findChannel(*it);
		if (chan) {
			chan->removeMember(&client);
			if (chan->memberCount() == 0) deleteChannel(*it);
		}
		client.removeChannel(*it);
	}
}

void	Server::broadcastToClientChannels(Client &client, const std::string &msg,
										  Client *except)
{
	std::set<int> sent;
	const std::set<std::string> &chans = client.getChannels();
	for (std::set<std::string>::const_iterator ci = chans.begin();
		 ci != chans.end(); ++ci) {
		Channel *chan = findChannel(*ci);
		if (!chan) continue;
		const std::map<int, Client *> &members = chan->getMembers();
		for (std::map<int, Client *>::const_iterator mi = members.begin();
			 mi != members.end(); ++mi) {
			if (except && mi->second == except) continue;
			if (sent.find(mi->first) == sent.end()) {
				sendTo(*mi->second, msg);
				sent.insert(mi->first);
			}
		}
	}
}

void	Server::addPolledFd(int fd, short events)
{
	pollfd pfd;
	pfd.fd = fd; pfd.events = events; pfd.revents = 0;
	_pfds.push_back(pfd);
}

void	Server::removePolledFd(int fd)
{
	for (size_t i = 0; i < _pfds.size(); ++i) {
		if (_pfds[i].fd == fd) {
			if (i != _pfds.size() - 1) _pfds[i] = _pfds.back();
			_pfds.pop_back();
			return;
		}
	}
}

void	Server::registerCommands()
{
	_dispatcher.registerHandler("PASS",    cmd_pass);
	_dispatcher.registerHandler("NICK",    cmd_nick);
	_dispatcher.registerHandler("USER",    cmd_user);
	_dispatcher.registerHandler("QUIT",    cmd_quit);
	_dispatcher.registerHandler("PING",    cmd_ping);
	_dispatcher.registerHandler("PONG",    cmd_pong);
	_dispatcher.registerHandler("CAP",     cmd_cap);
	_dispatcher.registerHandler("PRIVMSG", cmd_privmsg);
	_dispatcher.registerHandler("NOTICE",  cmd_notice);
	_dispatcher.registerHandler("JOIN",    cmd_join);
	_dispatcher.registerHandler("PART",    cmd_part);
	_dispatcher.registerHandler("TOPIC",   cmd_topic);
	_dispatcher.registerHandler("KICK",    cmd_kick);
	_dispatcher.registerHandler("INVITE",  cmd_invite);
	_dispatcher.registerHandler("MODE",    cmd_mode);
	_dispatcher.registerHandler("WHOIS",   cmd_whois);
}
