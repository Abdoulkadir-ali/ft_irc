#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
# include <set>
# include <poll.h>
# include <ctime>
# include <stdint.h>

# include "Bot.hpp"
# include "Channel.hpp"
# include "Client.hpp"
# include "CommandDispatcher.hpp"
# include "Commands.hpp"
# include "Parser.hpp"
# include "Reply.hpp"
# include "Utils.hpp"
# include "DccRelay.hpp"
# include <arpa/inet.h>
# include <cerrno>
# include <csignal>
# include <cstdlib>
# include <cstring>
# include <fcntl.h>
# include <iostream>
# include <netinet/in.h>
# include <stdexcept>
# include <sys/socket.h>
# include <unistd.h>

class Server {
public:
	Server(int port, const std::string &password);
	~Server();

	void	init();
	void	run();

	// --- Public API used by command handlers ---
	const std::string	&getName() const;
	const std::string	&getPassword() const;
	uint32_t			getServerIp() const;

	void	sendTo(Client &client, const std::string &message);
	void	markForDisconnect(Client &client);
	void	tryRegister(Client &client);

	Client	*findClientByNick(const std::string &nick);
	Channel	*findChannel(const std::string &name);
	Channel	*createChannel(const std::string &name);
	void	deleteChannel(const std::string &name);

	void	removeClientFromAllChannels(Client &client);
	void	broadcastToClientChannels(Client &client, const std::string &msg, Client *except);
	void	enableWrite(Client &client);
	void	enableWriteAll(Channel &chan, Client *except);

	// Nick index management (called when nick changes)
	void	updateNickIndex(Client &client, const std::string &oldNick, const std::string &newNick);

	// --- Bonus: Bot ---
	Bot		&getBot();

	// --- Bonus: DCC relay poll-set management ---
	void	addPolledFd(int fd, short events);
	void	removePolledFd(int fd);
	DccRelay	&getDccRelay();

private:
	Server();
	Server(const Server &);
	Server &operator=(const Server &);

	void	acceptClient();
	void	handleRead(Client &client);
	void	handleWrite(Client &client);
	void	consumeCommands(Client &client);
	void	disconnectClient(int fd);
	void	reapDisconnected();
	void	registerCommands();

	int								_port;
	std::string						_password;
	std::string						_server_name;
	int								_listen_fd;
	uint32_t						_server_ip;		// host byte order, set after bind
	std::vector<pollfd>				_pfds;
	std::map<int, size_t>			_fd_to_pfd_idx;	// fd -> index in _pfds (O(1) lookup)
	std::map<int, Client *>			_clients;		// fd -> Client*
	std::map<std::string, Client *>	_nick_index;	// lowercase nick -> Client* (O(log n) lookup)
	std::map<std::string, Channel *>	_channels;	// lowercase name -> Channel*
	std::set<int>					_dead_fds;
	CommandDispatcher				_dispatcher;
	time_t							_start_time;

	Bot			_bot;
	DccRelay	_dcc_relay;
	int			_bot_pipe_fd;	// bot's own end — polled but not in _clients
};

#endif
