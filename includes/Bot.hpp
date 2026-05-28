#ifndef BOT_HPP
# define BOT_HPP

# include "Parser.hpp"
# include "Utils.hpp"
# include <ctime>
# include <fcntl.h>
# include <string>
# include <sys/socket.h>
# include <unistd.h>

class Server;

class Bot {
public:
	Bot(Server &srv, const std::string &password);
	~Bot();

	// Called once after server init() — sends PASS/NICK/USER via pipe
	void	init();

	// Called by Server::run() when the bot's pipe fd has data readable
	void	onRead();

	// The server-side fd (added to server's _clients like a normal client)
	int		getSrvFd() const;

	// The bot-side fd (added to poll set; not in _clients)
	int		getBotFd() const;

private:
	Bot();
	Bot(const Bot &);
	Bot &operator=(const Bot &);

	// Write an IRC line to the server (via pipe)
	void	sendLine(const std::string &line);

	// Process one complete IRC line received from the server
	void	processLine(const std::string &line);

	// Parse and handle a PRIVMSG targeting us or a channel trigger
	void	handlePrivmsg(const std::string &from,
						  const std::string &target,
						  const std::string &text);

	Server		&_srv;
	std::string	_password;
	int			_srv_fd;	// fds[0] — server treats this as a client
	int			_bot_fd;	// fds[1] — bot reads/writes here
	std::string	_read_buf;
	bool		_registered;
	std::string	_nick;
	std::string	_default_channel;
};

#endif
