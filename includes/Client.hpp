#ifndef CLIENT_HPP
# define CLIENT_HPP

# include "Utils.hpp"
# include <ctime>
# include <set>
# include <string>

class Channel;

class Client {
public:
	// Registration states
	enum State {
		STATE_NEW,
		STATE_PASS_OK,
		STATE_REGISTERED,
		STATE_DEAD
	};

	explicit Client(int fd, const std::string &host);
	~Client();

	// --- Getters ---
	int					getFd() const;
	State				getState() const;
	const std::string	&getNick() const;
	const std::string	&getUser() const;
	const std::string	&getRealName() const;
	const std::string	&getHost() const;
	std::string			getPrefix() const;
	bool				hasNick() const;
	bool				hasUser() const;

	// --- Setters ---
	void	setState(State s);
	void	setNick(const std::string &nick);
	void	setUser(const std::string &user);
	void	setRealName(const std::string &realname);

	// --- Buffers ---
	std::string			&readBuf();
	std::string			&writeBuf();
	void				appendToWrite(const std::string &data);

	// --- Channel membership ---
	void				addChannel(const std::string &name);
	void				removeChannel(const std::string &name);
	const std::set<std::string>	&getChannels() const;

private:
	Client();
	Client(const Client &);
	Client &operator=(const Client &);

	int					_fd;
	State				_state;
	std::string			_nick;
	std::string			_user;
	std::string			_realname;
	std::string			_host;
	std::string			_read_buf;
	std::string			_write_buf;
	std::set<std::string>	_channels; // channels this client has joined (lowercase keys)
};

#endif
