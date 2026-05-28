#ifndef UTILS_HPP
# define UTILS_HPP

# include <cctype>
# include <sstream>
# include <string>
# include <vector>

namespace Utils {

std::string		toUpper(const std::string &s);
std::string		toLower(const std::string &s);
bool			ircCaseEqual(const std::string &a, const std::string &b);
std::vector<std::string>	splitCSV(const std::string &s);
bool			isValidNick(const std::string &nick);
bool			isValidChannelName(const std::string &name);
std::string		intToStr(int n);

}

#endif
