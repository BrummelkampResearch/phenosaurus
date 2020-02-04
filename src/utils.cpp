// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>
#include <regex>

#include "utils.hpp"
#include "mrsrc.h"

// -----------------------------------------------------------------------

int get_terminal_width()
{
    int result = 80;

    if (isatty(STDOUT_FILENO))
    {
        struct winsize w;
        ioctl(0, TIOCGWINSZ, &w);
        result = w.ws_col;
    }
    return result;
}

// -----------------------------------------------------------------------

void showVersionInfo()
{
	mrsrc::rsrc version("version.txt");
	if (not version)
		std::cerr << "unknown version, version resource is missing" << std::endl;
	else
	{
		struct membuf : public std::streambuf
		{
			membuf(char* data, size_t length)		{ this->setg(data, data, data + length); }
		} buffer(const_cast<char*>(version.data()), version.size());
		
		std::istream is(&buffer);
		std::string line;
		std::regex
			rxVersionNr(R"(Last Changed Rev: (\d+))"),
			rxVersionDate(R"(Last Changed Date: (\d{4}-\d{2}-\d{2}).*)");

		while (std::getline(is, line))
		{
			std::smatch m;

			if (std::regex_match(line, m, rxVersionNr))
			{
				std::cout << "Last changed revision number: " << m[1] << std::endl;
				continue;
			}

			if (std::regex_match(line, m, rxVersionDate))
			{
				std::cout << "Last changed revision date: " << m[1] << std::endl;
				continue;
			}
		}
	}
}
