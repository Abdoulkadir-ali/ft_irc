#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Reply.hpp"

void cmd_pass(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() == Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_alreadyregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "PASS"));
		return;
	}
	if (msg.params[0] != srv.getPassword()) {
		srv.sendTo(cli, Reply::err_passwdmismatch(srv.getName(), cli));
		srv.markForDisconnect(cli);
		return;
	}
	cli.setState(Client::STATE_PASS_OK);
	// If NICK and USER were already received (irssi sends NICK/USER before PASS),
	// try to complete registration now
	srv.tryRegister(cli);
}
