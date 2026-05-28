#include "Commands.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "Reply.hpp"
#include "Utils.hpp"

#include <cstdlib>

void cmd_mode(Server &srv, Client &cli, const Message &msg)
{
	if (cli.getState() != Client::STATE_REGISTERED) {
		srv.sendTo(cli, Reply::err_notregistered(srv.getName(), cli));
		return;
	}
	if (msg.params.empty()) {
		srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "MODE"));
		return;
	}
	const std::string &target = msg.params[0];

	// Only channel modes are supported
	if (target[0] != '#' && target[0] != '&')
		return; // ignore user modes silently

	Channel *chan = srv.findChannel(target);
	if (!chan) {
		srv.sendTo(cli, Reply::err_nosuchchannel(srv.getName(), cli, target));
		return;
	}

	// Query mode (no flags)
	if (msg.params.size() < 2) {
		srv.sendTo(cli, Reply::rpl_channelmodeis(srv.getName(), cli,
			chan->getName(), chan->getModeString()));
		return;
	}

	// Setting modes requires op
	if (!chan->isOperator(&cli)) {
		srv.sendTo(cli, Reply::err_chanoprivsneeded(srv.getName(), cli, chan->getName()));
		return;
	}

	const std::string &modeStr = msg.params[1];
	bool adding = true;
	size_t paramIdx = 2; // next param to consume

	std::string appliedModes;
	std::string appliedParams;
	bool lastWasPlus = true;
	bool needSign = true;

	for (size_t i = 0; i < modeStr.size(); ++i) {
		char c = modeStr[i];
		if (c == '+') {
			adding = true;
			needSign = true;
			continue;
		}
		if (c == '-') {
			adding = false;
			needSign = true;
			continue;
		}

		switch (c) {
		case 'i':
			chan->setInviteOnly(adding);
			if (needSign || adding != lastWasPlus) {
				appliedModes += (adding ? "+" : "-");
				lastWasPlus = adding;
				needSign = false;
			}
			appliedModes += 'i';
			break;

		case 't':
			chan->setTopicRestricted(adding);
			if (needSign || adding != lastWasPlus) {
				appliedModes += (adding ? "+" : "-");
				lastWasPlus = adding;
				needSign = false;
			}
			appliedModes += 't';
			break;

		case 'k':
			if (adding) {
				if (paramIdx >= msg.params.size()) {
					srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "MODE"));
					continue;
				}
				chan->setKey(msg.params[paramIdx]);
				if (needSign || adding != lastWasPlus) {
					appliedModes += "+";
					lastWasPlus = true;
					needSign = false;
				}
				appliedModes += 'k';
				appliedParams += " " + msg.params[paramIdx];
				++paramIdx;
			} else {
				chan->removeKey();
				if (needSign || adding != lastWasPlus) {
					appliedModes += "-";
					lastWasPlus = false;
					needSign = false;
				}
				appliedModes += 'k';
			}
			break;

		case 'o': {
			if (paramIdx >= msg.params.size()) {
				srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "MODE"));
				continue;
			}
			std::string nick = msg.params[paramIdx++];
			Client *tgt = srv.findClientByNick(nick);
			if (!tgt || !chan->isMember(tgt)) {
				srv.sendTo(cli, Reply::err_usernotinchannel(srv.getName(), cli, nick, chan->getName()));
				continue;
			}
			if (adding)
				chan->addOperator(tgt);
			else
				chan->removeOperator(tgt);
			if (needSign || adding != lastWasPlus) {
				appliedModes += (adding ? "+" : "-");
				lastWasPlus = adding;
				needSign = false;
			}
			appliedModes += 'o';
			appliedParams += " " + tgt->getNick();
			break;
		}

		case 'l':
			if (adding) {
				if (paramIdx >= msg.params.size()) {
					srv.sendTo(cli, Reply::err_needmoreparams(srv.getName(), cli, "MODE"));
					continue;
				}
				int limit = std::atoi(msg.params[paramIdx].c_str());
				++paramIdx;
				if (limit <= 0)
					continue;
				chan->setUserLimit(limit);
				if (needSign || adding != lastWasPlus) {
					appliedModes += "+";
					lastWasPlus = true;
					needSign = false;
				}
				appliedModes += 'l';
				appliedParams += " " + Utils::intToStr(limit);
			} else {
				chan->removeUserLimit();
				if (needSign || adding != lastWasPlus) {
					appliedModes += "-";
					lastWasPlus = false;
					needSign = false;
				}
				appliedModes += 'l';
			}
			break;

		default:
			srv.sendTo(cli, Reply::err_unknownmode(srv.getName(), cli, c));
			break;
		}
	}

	// Broadcast the mode change if any modes were applied
	if (!appliedModes.empty()) {
		std::string modeMsg = ":" + cli.getPrefix() + " MODE " + chan->getName()
			+ " " + appliedModes + appliedParams + "\r\n";
		chan->broadcast(modeMsg, NULL);
		srv.enableWriteAll(*chan, NULL);
	}
}
