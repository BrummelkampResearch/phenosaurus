// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

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
#include <boost/algorithm/string.hpp>

#include <zeep/http/daemon.hpp>
#include <zeep/crypto.hpp>
#include <zeep/streambuf.hpp>

#include "refseq.hpp"
#include "fisher.hpp"
#include "bowtie.hpp"
#include "utils.hpp"
#include "screen-analyzer.hpp"
#include "screen-creator.hpp"
#include "screen-data.hpp"
#include "screen-server.hpp"
#include "screen-service.hpp"
#include "db-connection.hpp"

#include "mrsrc.h"

namespace po = boost::program_options;
namespace fs = std::filesystem;
namespace io = boost::iostreams;
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
		;


	return config;
}

// --------------------------------------------------------------------

int usage()
{
	std::cerr << "Usage: screen-analyzer command [options]" << std::endl
			  << std::endl
			  << "Where command is one of" << std::endl
			  << std::endl
			  << "  create  -- create new screen" << std::endl
			  << "  map     -- map read in a screen to an assembly" << std::endl
			  << "  analyze -- analyze mapped reads" << std::endl
			  << "  refseq  -- create reference gene table" << std::endl
			  << "  server  -- start/stop server process" << std::endl
			  << std::endl;
	return 1;
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
		("screen-name", po::value<std::string>(),	"Screen name")
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
				  << std::endl
				  << config << std::endl;
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

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( sever command [options])",
		{
			{ "command", po::value<std::string>(),		"Server command" }
		}, { }, { "command" });

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

int main_compress(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( dump screen-name assembly file [options])",
		{
			{ "file", po::value<std::string>(),	"The file to dump" }
		},
		{ "screen-name", "assembly", "file" });

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	auto data = ScreenData::load(screenDir);

	std::string assembly = vm["assembly"].as<std::string>();

	unsigned trimLength = 50;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	auto file = vm["file"].as<std::string>();

	data->compress_map(assembly, trimLength, file);

	return result;
}

// --------------------------------------------------------------------

int main_dump(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( dump screen-name assembly file [options])",
		{
			{ "file", po::value<std::string>(),	"The file to dump" }
		},
		{ "screen-name", "assembly", "file" });

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	auto data = ScreenData::load(screenDir);

	std::string assembly = vm["assembly"].as<std::string>();

	unsigned trimLength = 50;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	auto file = vm["file"].as<std::string>();

	data->dump_map(assembly, trimLength, file);

	return result;
}

// --------------------------------------------------------------------

int main_update_manifests(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( update-manifests [options])", {});

	fs::path screenDir = vm["screen-dir"].as<std::string>();

	screen_service::init(screenDir);

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

    pqxx::work tx(db_connection::instance());

    const zeep::value_serializer<ScreenType> s;

	std::vector<screen_info> screens;

    for (const auto[name, type, cell_line, description, long_description, ignore, scientist, created ]:
            tx.stream<std::string, std::string, std::string, std::string, std::string, bool, std::string, std::optional<std::string>>(
        R"(SELECT name, screen_type, cell_line, description, long_description, ignored,
            (SELECT username FROM auth.users WHERE id = scientist_id), trim(both '"' from to_json(screen_date)::text) AS created FROM screens)"))
    {
        screen_info screen;

        if (type == "IP")
            screen.type = ScreenType::IntracellularPhenotype;
        else if (type == "SL")
            screen.type = ScreenType::SyntheticLethal;
        else
            continue;

        screen.name = name;
	    screen.cell_line = cell_line;
	    screen.description = description;
	    screen.long_description = long_description;
	    screen.ignore = ignore;
	    screen.scientist = scientist;

        if (created)
    	    screen.created = zeep::value_serializer<boost::posix_time::ptime>::from_string(*created);
		else
			screen.created = boost::posix_time::second_clock::local_time();

		fs::path sdir = screenDir / screen.name;
		if (not fs::is_directory(sdir))
		{
			std::cout << "Screen " << screen.name << " does not exist" << std::endl;
			continue;
		}
		
		if (fs::exists(sdir / "manifest.json"))
		{
			std::cout << "Manifest for " << screen.name << " already exists" << std::endl;
			continue;
		}

		for (auto di = fs::directory_iterator(sdir); di != fs::directory_iterator(); ++di)
		{
			if (di->is_directory())
				continue;
			
			auto name = di->path().filename();
			if (name.extension() == ".gz")
				name = name.stem();
			if (name.extension() != ".fastq")
				continue;
			
			std::error_code ec;
			auto cp = fs::canonical(di->path(), ec);
			if (ec)
				cp = di->path();

			screen.files.emplace_back(screen_file{name.stem(), cp });
		}

		screens.push_back(screen);
	}

	tx.commit();

	for (auto& screen: screens)
	{
        try
        {
			screen_service::instance().update_screen(screen.name, screen);

            std::cout << "Updated " << screen.name << std::endl;
        }
        catch (const std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
        }
    }

	return result;
}

// --------------------------------------------------------------------

int main_guess_manifests(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( guess-manifests [options])", {});

	fs::path screenDir = vm["screen-dir"].as<std::string>();

	screen_service::init(screenDir);

	// --------------------------------------------------------------------

	for (auto di = fs::directory_iterator(screenDir); di != fs::directory_iterator(); ++di)
	{
		if (not di->is_directory())
			continue;
		
		auto manifest = di->path() / "manifest.json";
		if (fs::exists(manifest))
		{
			if (VERBOSE)
				std::cerr << manifest << " exists" << std::endl;
			continue;
		}
		
		bool hasLow = false, hasHigh = false, hasRepl[4] = {};

		for (fs::directory_iterator iter(di->path()); iter != fs::directory_iterator(); ++iter)
		{
			if (iter->is_directory())
				continue;
			
			auto name = iter->path().filename();
			if (name.extension() == ".gz")
				name = name.stem();
			if (name.extension() != ".fastq")
				continue;
			
			name = name.stem();
			
			if (name == "low")
				hasLow = true;
			else if (name == "high")
				hasHigh = true;
			else if (name.string().length() == 11 and name.string().compare(0, 10, "replicate-") == 0)
			{
				char d = name.string().back();
				if (d >= '1' and d <= '4')
					hasRepl[d - '1'] = true;
			}
		}

        screen_info screen;
		if (hasLow and hasHigh)
			screen.type = ScreenType::IntracellularPhenotype;
		else if (hasRepl[0] or hasRepl[1] or hasRepl[2] or hasRepl[3])
			screen.type = ScreenType::SyntheticLethal;
		else
		{
			std::cerr << "Could not deduce type for " << di->path().filename() << std::endl;
			continue;
		}
		
        screen.name = di->path().filename();
	    // screen.cell_line = cell_line;
	    // screen.description = description;
	    // screen.long_description = long_description;
	    // screen.ignore = ignore;
	    // screen.scientist = scientist;

		std::error_code ec;
		auto ft = fs::last_write_time(di->path() / "hg38", ec);
		if (ec)
			ft = fs::last_write_time(di->path() / "hg19", ec);

		if (not ec)
		{
			auto lastWriteTime = std::chrono::duration_cast<std::chrono::seconds>(ft - decltype(ft)::clock::time_point{}).count();
			screen.created = boost::posix_time::from_time_t(lastWriteTime);
		}

		for (auto fi = fs::directory_iterator(di->path()); fi != fs::directory_iterator(); ++fi)
		{
			if (fi->is_directory())
				continue;
			
			auto name = fi->path().filename();
			if (name.extension() == ".gz")
				name = name.stem();
			if (name.extension() != ".fastq")
				continue;
			
			std::error_code ec;
			auto cp = fs::canonical(fi->path(), ec);
			if (ec)
				cp = fi->path();

			screen.files.emplace_back(screen_file{name.stem(), cp });
		}

		std::ofstream mff(manifest);
		if (not mff.is_open())
		{
			std::cerr << "Could not open manifest file for writing" << std::endl;
			continue;
		}

		zeep::json::element mfe;
		to_element(mfe, screen);

		mff << mfe;

		if (VERBOSE)
			std::cerr << manifest << " written" << std::endl;

	}

	return 0;
}

// --------------------------------------------------------------------

int main(int argc, char* const argv[])
{
	int result = 0;

	// initialize enums

	zeep::value_serializer<ScreenType>::init("screen-type", {
		{ ScreenType::IntracellularPhenotype, "ip" },
		{ ScreenType::SyntheticLethal, "sl" }
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
		else if (command == "refseq")
			result = main_refseq(argc - 1, argv + 1);
		else if (command == "server")
			result = main_server(argc - 1, argv + 1);
		else if (command == "compress")
			result = main_compress(argc - 1, argv + 1);
		else if (command == "dump")
			result = main_dump(argc - 1, argv + 1);
		else if (command == "update-manifest")
			result = main_update_manifests(argc - 1, argv + 1);
		else if (command == "guess-manifest")
			result = main_guess_manifests(argc - 1, argv + 1);
		else if (command == "help" or command == "--help" or command == "-h" or command == "-?")
			usage();
		else if (command == "version" or command == "-v" or command == "--version")
			showVersionInfo();
		else
			result = usage();
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
