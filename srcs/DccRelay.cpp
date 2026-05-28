#include "DccRelay.hpp"
#include "Server.hpp"

DccSession::DccSession()
	: phase(WAITING_RECEIVER), listen_fd(-1), recv_fd(-1), send_fd(-1),
	  orig_ip(0), orig_port(0)
{}

bool parseDccSend(const std::string &ctcp,
	std::string &filename, uint32_t &ip, uint16_t &port, size_t &filesize)
{
	if (ctcp.size() < 2 || ctcp[0] != '\x01')
		return false;
	std::string body = ctcp.substr(1);
	if (!body.empty() && body[body.size() - 1] == '\x01')
		body.erase(body.size() - 1);
	if (body.substr(0, 9) != "DCC SEND ")
		return false;
	body = body.substr(9);

	std::vector<std::string> toks;
	std::string t;
	for (size_t i = 0; i <= body.size(); ++i)
	{
		if (i == body.size() || body[i] == ' ')
		{
			if (!t.empty())
			{
				toks.push_back(t);
				t.clear();
			}
		}
		else
			t += body[i];
	}
	if (toks.size() < 4)
		return false;

	filename = toks[0];
	ip       = static_cast<uint32_t>(std::strtoul(toks[1].c_str(), NULL, 10));
	port     = static_cast<uint16_t>(std::strtoul(toks[2].c_str(), NULL, 10));
	filesize = static_cast<size_t>(std::strtoul(toks[3].c_str(), NULL, 10));
	return true;
}

std::string buildDccSend(const std::string &filename,
	uint32_t ip, uint16_t port, size_t filesize)
{
	std::ostringstream oss;
	oss << '\x01' << "DCC SEND " << filename << ' '
		<< static_cast<unsigned long>(ip) << ' '
		<< static_cast<unsigned>(port) << ' '
		<< filesize << '\x01';
	return oss.str();
}

// bind on port 0 so the kernel picks a free port for us
static int openRelayListen(uint16_t &out_port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	fcntl(fd, F_SETFL, O_NONBLOCK);

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = 0;

	if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0
		|| listen(fd, 1) < 0)
	{
		close(fd);
		return -1;
	}

	struct sockaddr_in bound;
	socklen_t len = sizeof(bound);
	if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&bound), &len) < 0)
	{
		close(fd);
		return -1;
	}
	out_port = ntohs(bound.sin_port);
	return fd;
}

DccRelay::DccRelay(Server &srv) : _srv(srv), _server_ip(0) {}

DccRelay::~DccRelay()
{
	for (size_t i = 0; i < _sessions.size(); ++i)
	{
		DccSession &s = _sessions[i];
		if (s.listen_fd >= 0)
		{
			_srv.removePolledFd(s.listen_fd);
			close(s.listen_fd);
		}
		if (s.recv_fd >= 0)
		{
			_srv.removePolledFd(s.recv_fd);
			close(s.recv_fd);
		}
		if (s.send_fd >= 0)
		{
			_srv.removePolledFd(s.send_fd);
			close(s.send_fd);
		}
	}
}

void	DccRelay::setServerIp(uint32_t ip) { _server_ip = ip; }

std::string	DccRelay::intercept(Client &/*sender*/, Client &/*target*/,
	const std::string &ctcpText)
{
	std::string filename;
	uint32_t orig_ip;
	uint16_t orig_port;
	size_t   filesize;

	if (!parseDccSend(ctcpText, filename, orig_ip, orig_port, filesize))
		return "";

	uint16_t relay_port = 0;
	int lfd = openRelayListen(relay_port);
	if (lfd < 0)
		return "";

	DccSession s;
	s.listen_fd = lfd;
	s.orig_ip   = orig_ip;
	s.orig_port = orig_port;
	_sessions.push_back(s);
	_srv.addPolledFd(lfd, POLLIN);

	uint32_t adv_ip = _server_ip ? _server_ip : ((127u << 24) | 1u);
	return buildDccSend(filename, adv_ip, relay_port, filesize);
}

bool	DccRelay::hasFd(int fd) const
{
	for (size_t i = 0; i < _sessions.size(); ++i)
	{
		const DccSession &s = _sessions[i];
		if (fd == s.listen_fd || fd == s.recv_fd || fd == s.send_fd)
			return true;
	}
	return false;
}

void	DccRelay::handleFd(int fd, short revents)
{
	DccSession *s = sessionForFd(fd);
	if (!s)
		return;
	if (fd == s->listen_fd)
		handleListenFd(*s);
	else if (fd == s->recv_fd)
		handleRecvFd(*s, revents);
	else if (fd == s->send_fd)
		handleSendFd(*s, revents);
}

DccSession	*DccRelay::sessionForFd(int fd)
{
	for (size_t i = 0; i < _sessions.size(); ++i)
	{
		DccSession &s = _sessions[i];
		if (fd == s.listen_fd || fd == s.recv_fd || fd == s.send_fd)
			return &s;
	}
	return NULL;
}

void	DccRelay::removeSession(DccSession *s)
{
	if (s->listen_fd >= 0)
	{
		_srv.removePolledFd(s->listen_fd);
		close(s->listen_fd);
		s->listen_fd = -1;
	}
	if (s->recv_fd >= 0)
	{
		_srv.removePolledFd(s->recv_fd);
		close(s->recv_fd);
		s->recv_fd = -1;
	}
	if (s->send_fd >= 0)
	{
		_srv.removePolledFd(s->send_fd);
		close(s->send_fd);
		s->send_fd = -1;
	}

	for (size_t i = 0; i < _sessions.size(); ++i)
	{
		if (&_sessions[i] == s)
		{
			_sessions[i] = _sessions.back();
			_sessions.pop_back();
			return;
		}
	}
}

void	DccRelay::handleListenFd(DccSession &s)
{
	struct sockaddr_in ca;
	socklen_t len = sizeof(ca);
	int rfd = accept(s.listen_fd, reinterpret_cast<struct sockaddr *>(&ca), &len);
	if (rfd < 0)
		return;
	fcntl(rfd, F_SETFL, O_NONBLOCK);

	_srv.removePolledFd(s.listen_fd);
	close(s.listen_fd);
	s.listen_fd = -1;
	s.recv_fd   = rfd;
	s.phase     = DccSession::CONNECTING_SENDER;
	_srv.addPolledFd(rfd, POLLIN);

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0)
	{
		removeSession(&s);
		return;
	}
	fcntl(sfd, F_SETFL, O_NONBLOCK);

	struct sockaddr_in dest;
	std::memset(&dest, 0, sizeof(dest));
	dest.sin_family      = AF_INET;
	dest.sin_addr.s_addr = htonl(s.orig_ip);
	dest.sin_port        = htons(s.orig_port);

	int ret = connect(sfd, reinterpret_cast<struct sockaddr *>(&dest), sizeof(dest));
	if (ret < 0 && errno != EINPROGRESS)
	{
		close(sfd);
		removeSession(&s);
		return;
	}
	s.send_fd = sfd;
	_srv.addPolledFd(sfd, POLLIN | POLLOUT);
}

void	DccRelay::handleRecvFd(DccSession &s, short revents)
{
	if (revents & (POLLERR | POLLHUP | POLLNVAL))
	{
		removeSession(&s);
		return;
	}

	if (revents & POLLIN)
	{
		char buf[8192];
		ssize_t n = recv(s.recv_fd, buf, sizeof(buf), 0);
		if (n <= 0)
		{
			removeSession(&s);
			return;
		}
		s.recv_buf.append(buf, static_cast<size_t>(n));
		if (s.send_fd >= 0)
			_srv.addPolledFd(s.send_fd, POLLIN | POLLOUT);
	}
}

void	DccRelay::handleSendFd(DccSession &s, short revents)
{
	if (revents & (POLLERR | POLLHUP | POLLNVAL))
	{
		removeSession(&s);
		return;
	}

	if (s.phase == DccSession::CONNECTING_SENDER)
	{
		int err = 0;
		socklen_t len = sizeof(err);
		getsockopt(s.send_fd, SOL_SOCKET, SO_ERROR, &err, &len);
		if (err != 0)
		{
			removeSession(&s);
			return;
		}
		s.phase = DccSession::RELAYING;
	}

	if ((revents & POLLOUT) && !s.recv_buf.empty())
	{
		ssize_t n = send(s.send_fd, s.recv_buf.c_str(), s.recv_buf.size(),
			MSG_NOSIGNAL | MSG_DONTWAIT);
		if (n > 0)
			s.recv_buf.erase(0, static_cast<size_t>(n));
		else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			removeSession(&s);
			return;
		}
	}

	if (revents & POLLIN)
	{
		char buf[8192];
		ssize_t n = recv(s.send_fd, buf, sizeof(buf), 0);
		if (n <= 0)
		{
			removeSession(&s);
			return;
		}
		s.send_buf.append(buf, static_cast<size_t>(n));
		if (s.recv_fd >= 0)
			_srv.addPolledFd(s.recv_fd, POLLIN | POLLOUT);
	}

	if ((revents & POLLOUT) && !s.send_buf.empty() && s.recv_fd >= 0)
	{
		ssize_t n = send(s.recv_fd, s.send_buf.c_str(), s.send_buf.size(),
			MSG_NOSIGNAL | MSG_DONTWAIT);
		if (n > 0)
			s.send_buf.erase(0, static_cast<size_t>(n));
		else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			removeSession(&s);
			return;
		}
	}
}
