#include "CommandDispatcher.hpp"
#include "Server.hpp"

CommandDispatcher::CommandDispatcher()
{}

CommandDispatcher::~CommandDispatcher()
{}

void	CommandDispatcher::registerHandler(const std::string &command, Handler handler)
{
	_handlers[command] = handler;
}

void	CommandDispatcher::dispatch(Server &server, Client &client, const Message &msg)
{
	if (msg.command.empty())
		return;

	std::map<std::string, Handler>::iterator it = _handlers.find(msg.command);
	if (it != _handlers.end())
		it->second(server, client, msg);
	else
	{
		if (client.getState() == Client::STATE_REGISTERED)
			server.sendTo(client, Reply::err_unknowncommand(server.getName(), client, msg.command));
	}
}
