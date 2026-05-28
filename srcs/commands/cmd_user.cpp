#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Reply.hpp"

void cmd_user(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() == Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_alreadyregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.size() < 4) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "USER"));
		return;
	}

	cli.setUser(msg.params[0]);
	// params[1] = mode (ignored), params[2] = unused (ignored)
	// params[3] = realname (may have been the trailing param)
	cli.setRealName(msg.params[3]);
	srv.tryRegister(cli);
}
