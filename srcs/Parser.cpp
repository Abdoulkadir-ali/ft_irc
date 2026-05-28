#include "Parser.hpp"

namespace Parser {

bool	extractLine(std::string &buf, std::string &line)
{
	std::string::size_type pos = buf.find('\n');
	if (pos == std::string::npos)
		return false;
	line = buf.substr(0, pos);
	buf.erase(0, pos + 1);
	// strip trailing \r for \r\n tolerance
	if (!line.empty() && line[line.size() - 1] == '\r')
		line.erase(line.size() - 1);
	return true;
}

Message	tokenize(const std::string &raw)
{
	Message msg;
	std::string line(raw);

	// cap at 512 chars (RFC)
	if (line.size() > 512)
		line.resize(512);

	std::string::size_type i = 0;

	// 1. optional prefix
	if (!line.empty() && line[0] == ':') {
		std::string::size_type sp = line.find(' ', 1);
		if (sp == std::string::npos) {
			msg.prefix = line.substr(1);
			return msg;
		}
		msg.prefix = line.substr(1, sp - 1);
		i = sp + 1;
	}

	// skip spaces
	while (i < line.size() && line[i] == ' ')
		++i;

	// 2. command
	{
		std::string::size_type start = i;
		while (i < line.size() && line[i] != ' ')
			++i;
		msg.command = Utils::toUpper(line.substr(start, i - start));
	}

	// 3. params
	while (i < line.size()) {
		// skip spaces
		while (i < line.size() && line[i] == ' ')
			++i;
		if (i >= line.size())
			break;
		if (line[i] == ':') {
			// trailing: rest of line
			msg.params.push_back(line.substr(i + 1));
			break;
		}
		std::string::size_type start = i;
		while (i < line.size() && line[i] != ' ')
			++i;
		msg.params.push_back(line.substr(start, i - start));
	}

	return msg;
}

}
