#ifndef PARSER_HPP
# define PARSER_HPP

# include "Message.hpp"
# include "Utils.hpp"
# include <string>

namespace Parser {

// Try to extract one complete line (up to \r\n or \n) from buf.
// If found, removes it from buf (including the delimiter) and returns true.
// The extracted line is placed in 'line' (without the trailing \r\n).
bool		extractLine(std::string &buf, std::string &line);

// Tokenize a single IRC line into a Message struct.
Message		tokenize(const std::string &line);

}

#endif
