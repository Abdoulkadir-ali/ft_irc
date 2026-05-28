#include "Bot.hpp"

Bot::Bot(Server &srv, const std::string &password)
	: _srv(srv), _password(password), _srv_fd(-1), _bot_fd(-1),
	  _registered(false), _nick("IRCBot"), _default_channel("#general")
{}

Bot::~Bot()
{
	if (_bot_fd >= 0) { close(_bot_fd); _bot_fd = -1; }
	// _srv_fd is managed by Server as a normal client fd
}

int	Bot::getSrvFd() const { return _srv_fd; }
int	Bot::getBotFd() const { return _bot_fd; }

void	Bot::init()
{
	int fds[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
		return;
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	fcntl(fds[1], F_SETFL, O_NONBLOCK);
	_srv_fd = fds[0];
	_bot_fd = fds[1];

	std::string reg = "PASS " + _password + "\r\n"
		"NICK " + _nick + "\r\n"
		"USER ircbot 0 * :IRC Bot\r\n";
	send(_bot_fd, reg.c_str(), reg.size(), 0);
}

void	Bot::sendLine(const std::string &line)
{
	std::string msg = line + "\r\n";
	send(_bot_fd, msg.c_str(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
}

void	Bot::onRead()
{
	char buf[4096];
	ssize_t n = recv(_bot_fd, buf, sizeof(buf) - 1, 0);
	if (n <= 0)
		return;
	_read_buf.append(buf, static_cast<size_t>(n));

	std::string line;
	while (Parser::extractLine(_read_buf, line)) {
		if (!line.empty())
			processLine(line);
	}
}

void	Bot::processLine(const std::string &line)
{
	// join our default channel once we're welcomed
	if (line.find(" 001 ") != std::string::npos) {
		_registered = true;
		sendLine("JOIN " + _default_channel);
		return;
	}

	// ":nick!user@host PRIVMSG target :text"
	if (line.size() < 2 || line[0] != ':')
		return;

	std::string::size_type p1 = line.find(' ');
	if (p1 == std::string::npos) return;
	std::string prefix = line.substr(1, p1 - 1);

	std::string rest = line.substr(p1 + 1);
	std::string::size_type p2 = rest.find(' ');
	if (p2 == std::string::npos) return;
	if (rest.substr(0, p2) != "PRIVMSG") return;

	rest = rest.substr(p2 + 1);
	std::string::size_type p3 = rest.find(' ');
	if (p3 == std::string::npos) return;
	std::string target = rest.substr(0, p3);

	rest = rest.substr(p3 + 1);
	std::string text = (!rest.empty() && rest[0] == ':') ? rest.substr(1) : rest;

	if (!text.empty() && text[0] == '\x01')
		return;

	std::string::size_type ex = prefix.find('!');
	std::string from = (ex != std::string::npos) ? prefix.substr(0, ex) : prefix;

	handlePrivmsg(from, target, text);
}

static std::string currentTime()
{
	time_t t = std::time(NULL);
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&t));
	return std::string(buf);
}

void	Bot::handlePrivmsg(const std::string &from, const std::string &target,
	const std::string &text)
{
	bool inChannel = (!target.empty() && (target[0] == '#' || target[0] == '&'));
	bool toUs    = Utils::ircCaseEqual(target, _nick);
	bool trigger = inChannel && text.size() > 1 && text[0] == '!';

	if (!toUs && !trigger)
		return;

	std::string replyTo = inChannel ? target : from;
	std::string cmd = (trigger && !text.empty()) ? text.substr(1) : text;

	std::string::size_type sp = cmd.find(' ');
	std::string kw   = Utils::toUpper(sp != std::string::npos ? cmd.substr(0, sp) : cmd);
	std::string args = (sp != std::string::npos) ? cmd.substr(sp + 1) : "";

	if (kw == "HELP")
		sendLine("PRIVMSG " + replyTo + " :Commands: !ping !time !echo <text> !op !info");
	else if (kw == "OP") {
		if (inChannel)
			sendLine("MODE " + target + " +o " + from);
		else
			sendLine("PRIVMSG " + from + " :Usage: !op in a channel");
	} else if (kw == "PING")
		sendLine("PRIVMSG " + replyTo + " :PONG! Hi " + from + " :)");
	else if (kw == "TIME")
		sendLine("PRIVMSG " + replyTo + " :" + currentTime());
	else if (kw == "ECHO")
		sendLine("PRIVMSG " + replyTo + " :" + (args.empty() ? "echo what?" : args));
	else if (kw == "INFO")
		sendLine("PRIVMSG " + replyTo + " :IRCBot - ft_irc bonus bot. Try !help");
	else if (toUs)
		sendLine("PRIVMSG " + replyTo + " :Unknown command. Try !help");
}
