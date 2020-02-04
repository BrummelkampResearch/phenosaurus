// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <termios.h>
#include <sys/ioctl.h>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <regex>
#include <filesystem>

#include <boost/program_options.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "refann.hpp"
#include "fisher.hpp"
#include "bowtie.hpp"
#include "utils.hpp"
#include "screendata.hpp"

#include "mrsrc.h"

namespace po = boost::program_options;
namespace fs = std::filesystem;
namespace io = boost::iostreams;
using namespace std::literals;

#define APP_NAME "screen-analyzer"

int VERBOSE = 0;

// --------------------------------------------------------------------

// recursively print exception whats:
void print_what (const std::exception& e)
{
	std::cerr << e.what() << std::endl;
	try
	{
		std::rethrow_if_nested(e);
	}
	catch (const std::exception& nested)
	{
		std::cerr << " >> ";
		print_what(nested);
	}
}

// --------------------------------------------------------------------

int usage()
{
	std::cerr << "Usage: screen-analyzer command [options]" << std::endl
			  << std::endl
			  << "Where command is one of" << std::endl
			  << std::endl
			  << "  create" << std::endl
			  << "  bowtie" << std::endl
			  << "  analyze" << std::endl
			  << std::endl;
	return 1;
}

// --------------------------------------------------------------------

int main_create(int argc, char* const argv[])
{
	int result = 0;

	po::options_description visible(APP_NAME R"( create screen-name [options])");
	visible.add_options()
		("help,h",								"Display help message")
		("version",								"Print version")

		("low", po::value<std::string>(),		"The path to the LOW FastQ file")
		("high", po::value<std::string>(),		"The path to the HIGH FastQ file")

		("config", po::value<std::string>(),	"Name of config file to use, default is " APP_NAME ".conf located in current of home directory")

		("force",								"By default a screen is only created if it not already exists, use this flag to delete the old screen before creating a new.")

		("verbose,v",							"Verbose output")
		;

	po::options_description config(APP_NAME R"( config file options)");
	config.add_options()
		("bowtie", po::value<std::string>(),	"Bowtie executable")
		("threads", po::value<unsigned>(),		"Nr of threads to use")
		("screen-dir", po::value<std::string>(), "Directory containing the screen data")
		("bowtie-index-hg19", po::value<std::string>(), "Bowtie index parameter for HG19")
		("bowtie-index-hg38", po::value<std::string>(), "Bowtie index parameter for HG38");

	po::options_description hidden("hidden options");
	hidden.add_options()
		("screen-name", po::value<std::string>(),	"Screen name")
		("debug,d", po::value<int>(),				"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible).add(config).add(hidden);

	po::positional_options_description p;
	p.add("screen-name", 1);
	
	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	const std::regex kPosRx(R"((cds|tx)((?:\+|-)[0-9]+)?)");

	fs::path configFile = APP_NAME ".conf";
	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / APP_NAME ".conf";
	
	if (vm.count("config") != 0)
	{
		configFile = vm["config"].as<std::string>();
		if (not fs::exists(configFile))
			throw std::runtime_error("Specified config file does not seem to exist");
	}
	
	if (fs::exists(configFile))
	{
		po::options_description config_options ;
		config_options.add(config).add(hidden);

		std::ifstream cfgFile(configFile);
		if (cfgFile.is_open())
			po::store(po::parse_config_file(cfgFile, config_options), vm);
	}
	
	po::notify(vm);

	if (vm.count("version"))
	{
		showVersionInfo();
		exit(0);
	}

	if (vm.count("screen-name") == 0 or vm.count("screen-dir") == 0 or
		vm.count("low") == 0 or vm.count("high") == 0)
	{
		std::cerr << visible << std::endl;
		exit(-1);
	}

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	if (fs::exists(screenDir))
	{
		if (vm.count("force"))
			fs::remove_all(screenDir);
		else
			throw std::runtime_error("Screen already exists, use --force to delete old screen");
	}

	std::unique_ptr<ScreenData> data(ScreenData::create(screenDir, vm["low"].as<std::string>(), vm["high"].as<std::string>()));

	return result;
}

// --------------------------------------------------------------------

int main_map(int argc, char* const argv[])
{
	int result = 0;

	po::options_description visible(APP_NAME R"( map screen-name assembly [options])");
	visible.add_options()
		("help,h",								"Display help message")
		("version",								"Print version")

		("bowtie-index", po::value<std::string>(),
												"Bowtie index filename stem for the assembly")

		("read-length", po::value<unsigned>(),	"Read length to use, if specified reads will be trimmed to this size")

		("config", po::value<std::string>(),	"Name of config file to use, default is " APP_NAME ".conf located in current of home directory")
		("force",								"By default a screen is only mapped if it was not mapped already, use this flag to force creating a new mapping.")

		("verbose,v",							"Verbose output")
		;

	po::options_description config;
	config.add_options()
		("bowtie", po::value<std::string>(),	"Bowtie executable")
		("threads", po::value<unsigned>(),		"Nr of threads to use")
		("screen-dir", po::value<std::string>(), "Directory containing the screen data")
		("bowtie-index-hg19", po::value<std::string>(), "Bowtie index parameter for HG19")
		("bowtie-index-hg38", po::value<std::string>(), "Bowtie index parameter for HG38");

	po::options_description hidden("hidden options");
	hidden.add_options()
		("screen-name", po::value<std::string>(),	"Screen name")
		("assembly", po::value<std::string>(),		"Assembly name")
		("debug,d", po::value<int>(),				"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible).add(config).add(hidden);

	po::positional_options_description p;
	p.add("screen-name", 1);
	p.add("assembly", 1);
	
	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	const std::regex kPosRx(R"((cds|tx)((?:\+|-)[0-9]+)?)");

	fs::path configFile = APP_NAME ".conf";
	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / APP_NAME ".conf";
	
	if (vm.count("config") != 0)
	{
		configFile = vm["config"].as<std::string>();
		if (not fs::exists(configFile))
			throw std::runtime_error("Specified config file does not seem to exist");
	}
	
	if (fs::exists(configFile))
	{
		po::options_description config_options ;
		config_options.add(config).add(hidden);

		std::ifstream cfgFile(configFile);
		if (cfgFile.is_open())
			po::store(po::parse_config_file(cfgFile, config_options), vm);
	}
	
	po::notify(vm);

	if (vm.count("version"))
	{
		showVersionInfo();
		exit(0);
	}

	if (vm.count("screen-name") == 0 or vm.count("assembly") == 0)
	{
		po::options_description visible_options;
		visible_options.add(visible).add(config);

		std::cerr << visible_options << std::endl;
		exit(-1);
	}

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	std::unique_ptr<ScreenData> data(new ScreenData(screenDir));

	if (vm.count("bowtie") == 0)
		throw std::runtime_error("Bowtie executable not specified");
	fs::path bowtie = vm["bowtie"].as<std::string>();

	std::string assembly = vm["assembly"].as<std::string>();

	fs::path bowtieIndex;
	if (vm.count("bowtie-index") != 0)
		bowtieIndex = vm["bowtie-index"].as<std::string>();
	else
	{
		if (vm.count("bowtie-index-" + assembly) == 0)
			throw std::runtime_error("Bowtie index for assembly " + assembly + " not known and bowtie-index parameter not specified");
		bowtieIndex = vm["bowtie-index-" + assembly].as<std::string>();
	}

	unsigned readLength = 0;
	if (vm.count("read-length"))
		readLength = vm["read-length"].as<unsigned>();
	
	unsigned threads = 1;
	if (vm.count("threads"))
		threads = vm["threads"].as<unsigned>();

	data->map(assembly, bowtie, bowtieIndex, threads, readLength);

	return result;}

// --------------------------------------------------------------------

int main_analyze(int argc, char* const argv[])
{
	return 0;
}

// --------------------------------------------------------------------

int main(int argc, char* const argv[])
{
	int result = 0;

	try
	{
		if (argc < 2)
		{
			usage();
			exit(-1);
		}

		std::string command = argv[1];
		if (command == "create")
			result = main_create(argc - 1, argv + 1);
		else if (command == "map")
			result = main_map(argc - 1, argv + 1);
		else if (command == "analyze")
			result = main_analyze(argc - 1, argv + 1);
		else if (command == "help")
			usage();
		else
			result = usage();
	}
	catch(const std::exception& ex)
	{
		std::cerr << std::endl
				  << "Fatal exception" << std::endl;

		print_what(ex);
		result = 1;
	}
	
	return result;
}
