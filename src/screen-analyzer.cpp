// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include <zeep/http/daemon.hpp>
#include <zeep/crypto.hpp>

#include "bowtie.hpp"
#include "utils.hpp"
#include "screen-data.hpp"
#include "screen-server.hpp"
#include "db-connection.hpp"
#include "user-service.hpp"

namespace po = boost::program_options;
namespace fs = std::filesystem;
namespace zh = zeep::http;
namespace ba = boost::algorithm;

using namespace std::literals;

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

// -----------------------------------------------------------------------

po::options_description get_config_options()
{
	po::options_description config(PACKAGE_NAME R"( config file options)");
	
	config.add_options()
		( "bowtie",				po::value<std::string>(),	"Bowtie executable")
		( "assembly",			po::value<std::string>(),	"Default assembly to use, currently one of hg19 or hg38")
		( "trim-length",		po::value<unsigned>(),		"Trim reads to this length, default is 50")
		( "threads",			po::value<unsigned>(),		"Nr of threads to use")
		( "screen-dir",			po::value<std::string>(),	"Directory containing the screen data")
		( "bowtie-index-hg19",	po::value<std::string>(),	"Bowtie index parameter for HG19")
		( "bowtie-index-hg38",	po::value<std::string>(),	"Bowtie index parameter for HG38")
		( "control",			po::value<std::string>(),	"Name of the screen that contains the four control data replicates for synthetic lethal analysis")
		( "db-host",			po::value<std::string>(),	"Database host")
		( "db-port",			po::value<std::string>(),	"Database port")
		( "db-dbname",			po::value<std::string>(),	"Database name")
		( "db-user",			po::value<std::string>(),	"Database user name")
		( "db-password",		po::value<std::string>(),	"Database password")
		( "address",			po::value<std::string>(),	"External address, default is 0.0.0.0")
		( "port",				po::value<uint16_t>(),		"Port to listen to, default is 10336")
		( "no-daemon,F",									"Do not fork into background")
		( "user,u",				po::value<std::string>(),	"User to run the daemon")
		( "secret",				po::value<std::string>(),	"Secret hashed used in user authentication")
		( "context",			po::value<std::string>(),	"Context name of this server (used e.g. in a reverse proxy setup)")
		( "smtp-server",		po::value<std::string>(),	"SMTP server address for sending out new passwords" )
		( "smtp-port",			po::value<uint16_t>(),		"SMTP server port for sending out new passwords" )
		( "smtp-user",			po::value<std::string>(),	"SMTP server user name for sending out new passwords" )
		( "smtp-password",		po::value<std::string>(),	"SMTP server password name for sending out new passwords" )
		;


	return config;
}

// --------------------------------------------------------------------

po::variables_map load_options(int argc, char* const argv[], const char* description,
	std::initializer_list<po::option_description> options,
	std::initializer_list<std::string> required,
	std::initializer_list<std::string> positional)
{
	po::options_description visible(description);
	visible.add_options()
		("help,h",								"Display help message")
		("version",								"Print version");

	for (auto& option: options)
	{
		boost::shared_ptr<po::option_description> ptr(new po::option_description(option));
		visible.add(ptr);
	}

	visible.add_options()
		("config", po::value<std::string>(),	"Name of config file to use, default is " PACKAGE_NAME ".conf located in current of home directory")
		("verbose,v",							"Verbose output")
		;

	po::options_description config = get_config_options();

	po::options_description hidden("hidden options");
	hidden.add_options()
		("debug,d", po::value<int>(),				"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible).add(config).add(hidden);

	po::positional_options_description p;
	for (auto& pos: positional)
		p.add(pos.c_str(), 1);
	
	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	fs::path configFile = PACKAGE_NAME ".conf";
	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / PACKAGE_NAME ".conf";
	
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

	if (vm.count("help"))
	{
		std::cout << "usage: " << description << std::endl
				  << visible << std::endl
				  << std::endl;

		if (vm.count("verbose"))
			std::cout << config << std::endl;
		else
			std::cout << "Use --help --verbose to see config file options" << std::endl;
		
		exit(0);
	}

	for (auto r: required)
	{
		if (vm.count(r) == 0)
		{
			std::cerr << visible << std::endl;
			exit(-1);
		}
	}

	VERBOSE = vm.count("verbose") != 0;
	if (vm.count("debug"))
		VERBOSE = vm["debug"].as<int>();

	return vm;
}

// --------------------------------------------------------------------

int main_server(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( server command [options])",
		{
			{ "command", 		po::value<std::string>(),	"Server command" },
		}, { "smtp-server", "smtp-port" }, { "command" });

	// --------------------------------------------------------------------

	if (vm.count("command") == 0)
	{
		std::cerr << R"(
Command should be either:

  start     start a new server
  stop      start a running server
  status    get the status of a running server
  reload    restart a running server with new options
			 )" << std::endl;
		exit(vm.count("help") ? 0 : 1);
	}
	
	// --------------------------------------------------------------------
	
	std::vector<std::string> vConn;
	for (std::string opt: { "db-host", "db-port", "db-dbname", "db-user", "db-password" })
	{
		if (vm.count(opt) == 0)
			continue;
		
		vConn.push_back(opt.substr(3) + "=" + vm[opt].as<std::string>());
	}

	db_connection::init(ba::join(vConn, " "));

	// --------------------------------------------------------------------
	
	std::string smtpServer = vm["smtp-server"].as<std::string>();
	uint16_t smtpPort = vm["smtp-port"].as<uint16_t>();
	std::string smtpUser, smtpPassword;
	if (vm.count("smtp-user"))	smtpUser = vm["smtp-user"].as<std::string>();
	if (vm.count("smtp-password"))	smtpPassword = vm["smtp-password"].as<std::string>();

	user_service::init(smtpServer, smtpPort, smtpUser, smtpPassword);

	// --------------------------------------------------------------------
	
	if (vm.count("bowtie") == 0)
		throw std::runtime_error("Bowtie executable not specified");
	fs::path bowtie = vm["bowtie"].as<std::string>();

	std::string assembly = "hg38";
	if (vm.count("assembly"))
		assembly = vm["assembly"].as<std::string>();

	std::map<std::string,fs::path> assemblyIndices;
	for (auto assembly: { "hg19", "hg38" })
	{
		if (vm.count("bowtie-index-"s + assembly) == 0)
			continue;
		assemblyIndices[assembly] = vm["bowtie-index-"s + assembly].as<std::string>();
	}

	unsigned trimLength = 50;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	unsigned threads = std::thread::hardware_concurrency();
	if (vm.count("threads"))
		threads = vm["threads"].as<unsigned>();

	bowtie_parameters::init(bowtie, threads, trimLength, assembly, assemblyIndices);

	// --------------------------------------------------------------------

	fs::path docroot;

	char exePath[PATH_MAX + 1];
	int r = readlink("/proc/self/exe", exePath, PATH_MAX);
	if (r > 0)
	{
		exePath[r] = 0;
		docroot = fs::weakly_canonical(exePath).parent_path() / "docroot";
	}
	
	if (not fs::exists(docroot))
		throw std::runtime_error("Could not locate docroot directory");

	std::string secret;
	if (vm.count("secret"))
		secret = vm["secret"].as<std::string>();
	else
	{
		secret = zeep::encode_base64(zeep::random_hash());
		std::cerr << "starting with created secret " << secret << std::endl;
	}

	std::string context_name;
	if (vm.count("context"))
		context_name = vm["context"].as<std::string>();

	zh::daemon server([secret,docroot,screenDir=vm["screen-dir"].as<std::string>(),context_name]()
	{
		return createServer(docroot, screenDir, secret, context_name);
	}, "screen-analyzer");

	std::string user = "www-data";
	if (vm.count("user") != 0)
		user = vm["user"].as<std::string>();
	
	std::string address = "127.0.0.1";
	if (vm.count("address"))
		address = vm["address"].as<std::string>();

	uint16_t port = 10338;
	if (vm.count("port"))
		port = vm["port"].as<uint16_t>();

	std::string command = vm["command"].as<std::string>();

	if (command == "start")
	{
		std::cout << "starting server at http://" << address << ':' << port << '/' << std::endl;

		if (vm.count("no-daemon"))
			result = server.run_foreground(address, port);
		else
			result = server.start(address, port, 1, 2, user);
	}
	else if (command == "stop")
		result = server.stop();
	else if (command == "status")
		result = server.status();
	else if (command == "reload")
		result = server.reload();
	else
	{
		std::cerr << "Invalid command" << std::endl;
		result = 1;
	}

	return result;	
}

// --------------------------------------------------------------------

int main(int argc, char* const argv[])
{
	int result = 0;

	std::set_terminate([]()
	{
		std::cerr << "Unhandled exception" << std::endl;
		std::abort();
	});

	// initialize enums

	zeep::value_serializer<ScreenType>::init("screen-type", {
		{ ScreenType::IntracellularPhenotype, "ip" },
		{ ScreenType::IntracellularPhenotypeActivation, "pa" },
		{ ScreenType::SyntheticLethal, "sl" }
	});

	zeep::value_serializer<Mode>::init("mode", {
		{ Mode::Collapse, 			"collapse" },
		{ Mode::LongestTranscript, 	"longest-transcript" },
		{ Mode::LongestExon,		"longest-exon" }
	});

	zeep::value_serializer<Direction>::init("direction", {
		{ Direction::Sense, 	"sense" },
		{ Direction::AntiSense, "antisense" },
		{ Direction::Both, 		"both" }
	});

	zeep::value_serializer<CHROM>::init({
		{ INVALID, 	"unk" },
		{ CHR_1, 	"chr1" },
		{ CHR_2, 	"chr2" },
		{ CHR_3, 	"chr3" },
		{ CHR_4, 	"chr4" },
		{ CHR_5, 	"chr5" },
		{ CHR_6, 	"chr6" },
		{ CHR_7, 	"chr7" },
		{ CHR_8, 	"chr8" },
		{ CHR_9, 	"chr9" },
		{ CHR_10, 	"chr10" },
		{ CHR_11, 	"chr11" },
		{ CHR_12, 	"chr12" },
		{ CHR_13, 	"chr13" },
		{ CHR_14, 	"chr14" },
		{ CHR_15, 	"chr15" },
		{ CHR_16, 	"chr16" },
		{ CHR_17, 	"chr17" },
		{ CHR_18, 	"chr18" },
		{ CHR_19, 	"chr19" },
		{ CHR_20, 	"chr20" },
		{ CHR_21, 	"chr21" },
		{ CHR_22, 	"chr22" },
		{ CHR_23, 	"chr23" },
		{ CHR_X, 	"chrX" },
		{ CHR_Y, 	"chrY" }
	});

	try
	{
		result = main_server(argc, argv);
	}
	catch (const std::exception& ex)
	{
		std::cerr << std::endl
				  << "Fatal exception" << std::endl;

		print_what(ex);
		result = 1;
	}
	
	return result;
}
