#ifndef DCCRELAY_HPP
# define DCCRELAY_HPP

# include <string>
# include <vector>
# include <poll.h>
# include <stdint.h>
# include <arpa/inet.h>
# include <cerrno>
# include <cstdlib>
# include <cstring>
# include <fcntl.h>
# include <netinet/in.h>
# include <sstream>
# include <sys/socket.h>
# include "Client.hpp"
# include <unistd.h>

class Server;

// Tracks a single DCC SEND relay session
struct DccSession {
	// Phase: WAITING_RECEIVER → CONNECTING_SENDER → RELAYING → DONE
	enum Phase { WAITING_RECEIVER, CONNECTING_SENDER, RELAYING, DONE };

	Phase		phase;

	int			listen_fd;		// relay listen socket (receiver connects here)
	int			recv_fd;		// receiver's data connection
	int			send_fd;		// connection to original sender

	uint32_t	orig_ip;		// original sender IP (host byte order)
	uint16_t	orig_port;		// original sender port

	std::string	recv_buf;		// data read from receiver, queued to write to sender
	std::string	send_buf;		// data read from sender, queued to write to receiver

	DccSession();
};

class DccRelay {
public:
	explicit DccRelay(Server &srv);
	~DccRelay();

	// Returns the server's external IP (used when modifying DCC offers)
	void	setServerIp(uint32_t ip);

	// Intercept a DCC SEND CTCP from `sender` to `target`.
	// Returns a modified DCC SEND line (pointing to relay port) or "" on failure.
	std::string	intercept(Client &sender, Client &target,
						   const std::string &ctcpText);

	// True if fd belongs to one of our relay sessions
	bool	hasFd(int fd) const;

	// Handle poll events for a relay fd
	void	handleFd(int fd, short revents);

private:
	DccRelay();
	DccRelay(const DccRelay &);
	DccRelay &operator=(const DccRelay &);

	DccSession	*sessionForFd(int fd);
	void		removeSession(DccSession *s);
	void		handleListenFd(DccSession &s);
	void		handleRecvFd(DccSession &s, short revents);
	void		handleSendFd(DccSession &s, short revents);

	Server					&_srv;
	uint32_t				_server_ip;
	std::vector<DccSession>	_sessions;
};

// Parse a DCC SEND CTCP text:
// "\x01DCC SEND filename ip port size\x01" → fills out params, returns true
bool parseDccSend(const std::string &ctcp,
				  std::string &filename, uint32_t &ip,
				  uint16_t &port, size_t &filesize);

// Build a DCC SEND CTCP text from parts
std::string buildDccSend(const std::string &filename,
						 uint32_t ip, uint16_t port, size_t filesize);

#endif
