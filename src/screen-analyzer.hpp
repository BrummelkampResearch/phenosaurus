// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <boost/program_options.hpp>

int usage();

boost::program_options::options_description get_config_options();

boost::program_options::variables_map load_options(int argc, char* const argv[], const char* description,
	std::initializer_list<boost::program_options::option_description> options,
	std::initializer_list<std::string> required = {},
	std::initializer_list<std::string> positional = { "screen-name" });
