#include "Utils.hpp"

namespace Utils {

std::string	toUpper(const std::string &s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); ++i)
		r[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(r[i])));
	return r;
}

std::string	toLower(const std::string &s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); ++i)
		r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	return r;
}

bool	ircCaseEqual(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		char ca = static_cast<char>(std::toupper(static_cast<unsigned char>(a[i])));
		char cb = static_cast<char>(std::toupper(static_cast<unsigned char>(b[i])));
		if (ca != cb)
			return false;
	}
	return true;
}

std::vector<std::string>	splitCSV(const std::string &s)
{
	std::vector<std::string> result;
	std::string token;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == ',') {
			if (!token.empty())
				result.push_back(token);
			token.clear();
		} else {
			token += s[i];
		}
	}
	if (!token.empty())
		result.push_back(token);
	return result;
}

bool	isValidNick(const std::string &nick)
{
	if (nick.empty() || nick.size() > 9)
		return false;
	// first char must be letter or special: [ ] \ ` _ ^ { | }
	char c = nick[0];
	if (!std::isalpha(static_cast<unsigned char>(c))
		&& c != '[' && c != ']' && c != '\\' && c != '`'
		&& c != '_' && c != '^' && c != '{' && c != '|' && c != '}')
		return false;
	for (size_t i = 1; i < nick.size(); ++i) {
		c = nick[i];
		if (!std::isalnum(static_cast<unsigned char>(c))
			&& c != '-' && c != '[' && c != ']' && c != '\\'
			&& c != '`' && c != '_' && c != '^' && c != '{'
			&& c != '|' && c != '}')
			return false;
	}
	return true;
}

bool	isValidChannelName(const std::string &name)
{
	if (name.empty() || name.size() > 50)
		return false;
	if (name[0] != '#' && name[0] != '&')
		return false;
	if (name.size() < 2)
		return false;
	for (size_t i = 1; i < name.size(); ++i) {
		if (name[i] == ' ' || name[i] == '\x07' || name[i] == ',')
			return false;
	}
	return true;
}

std::string	intToStr(int n)
{
	std::stringstream ss;
	ss << n;
	return ss.str();
}

}
