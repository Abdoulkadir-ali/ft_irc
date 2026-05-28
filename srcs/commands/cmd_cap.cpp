#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"

void cmd_cap(Server &srv, Client &cli, const Message &msg)
{
	// We don't support any IRCv3 capabilities.
	// Just reply with an empty list for CAP LS and NAK for CAP REQ.
	if (msg.params.empty())
		return;
	if (msg.params[0] == "LS") {
		srv.sendTo(cli, ":" + srv.getName() + " CAP * LS :\r\n");
	} else if (msg.params[0] == "REQ") {
		// NAK everything
		std::string caps;
		if (msg.params.size() > 1)
			caps = msg.params[1];
		srv.sendTo(cli, ":" + srv.getName() + " CAP * NAK :" + caps + "\r\n");
	} else if (msg.params[0] == "END") {
		// Do nothing, client is done negotiating
	}
}
