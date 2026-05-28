#ifndef COMMANDDISPATCHER_HPP
# define COMMANDDISPATCHER_HPP

# include "Message.hpp"
# include <string>
# include <map>

class Server;
class Client;

class CommandDispatcher {
public:
	// Handler function signature
	typedef void (*Handler)(Server &, Client &, const Message &);

	CommandDispatcher();
	~CommandDispatcher();

	void	registerHandler(const std::string &command, Handler handler);
	void	dispatch(Server &server, Client &client, const Message &msg);

private:
	CommandDispatcher(const CommandDispatcher &);
	CommandDispatcher &operator=(const CommandDispatcher &);

	std::map<std::string, Handler>	_handlers;
};

#endif
