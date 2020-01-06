#include <iostream>
#include <regex>
// #include <filesystem>

#include "mrsrc.h"

#include <boost/program_options.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------

int VERBOSE;

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

// -----------------------------------------------------------------------


int main(int argc, const char* argv[])
{
	int result = 0;

	po::options_description visible_options("reference-annotation-builder [options]" );
	visible_options.add_options()
		("help,h",								"Display help message")
		("version",								"Print version")
		
		("verbose,v",							"Verbose output")
		;

	po::options_description hidden_options("hidden options");
	hidden_options.add_options()
		("debug,d",		po::value<int>(),				"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible_options).add(hidden_options);

	po::positional_options_description p;
	p.add("index", 1);
	
	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	fs::path configFile = "screen-qc.conf";
	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / "screen-qc.conf";
	
	if (fs::exists(configFile))
	{
		fs::ifstream cfgFile(configFile);
		if (cfgFile.is_open())
			po::store(po::parse_config_file(cfgFile, visible_options), vm);
	}
	
	po::notify(vm);

	if (vm.count("version"))
	{
		showVersionInfo();
		exit(0);
	}

	if (vm.count("help") or vm.count("index") == 0)
	{
		std::cerr << visible_options << std::endl;
		exit(1);
	}

	VERBOSE = vm.count("verbose") != 0;
	if (vm.count("debug"))
		VERBOSE = vm["debug"].as<int>();

	

	return result;
}
