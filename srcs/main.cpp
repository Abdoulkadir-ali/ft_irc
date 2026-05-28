#include "Server.hpp"

int main(int argc, char **argv)
{
	if (argc != 3) {
		std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
		return 1;
	}

	char *end = 0;
	long port = std::strtol(argv[1], &end, 10);
	if (end == argv[1] || *end != '\0' || port < 1024 || port > 65535) {
		std::cerr << "ircserv: port must be an integer in [1024, 65535]" << std::endl;
		return 1;
	}

	std::string password(argv[2]);
	if (password.empty()) {
		std::cerr << "ircserv: password must not be empty" << std::endl;
		return 1;
	}

	try
	{
		Server server(static_cast<int>(port), password);
		server.init();
		server.run();
	} catch (const std::exception &e) {
		std::cerr << "ircserv: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
