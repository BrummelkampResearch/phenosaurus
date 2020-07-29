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

#include "refseq.hpp"
#include "fisher.hpp"
#include "bowtie.hpp"
#include "utils.hpp"
#include "screendata.hpp"
#include "screenserver.hpp"
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
	std::initializer_list<std::string> required = {},
	std::initializer_list<std::string> positional = { "screen-name" })
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

int main_create(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( create screen-name --type <screen-type> [options])",
		{
			{ "type", po::value<std::string>(),		"The screen type to create, should be one of 'ip' or 'sl'" },
			{ "low", po::value<std::string>(),		"The path to the LOW FastQ file" },
			{ "high", po::value<std::string>(),		"The path to the HIGH FastQ file" },
			{ "replicate", po::value<std::vector<std::string>>(),
													"The replicate file, can be specified multiple times"},
			{ "force", new po::untyped_value(true),	"By default a screen is only created if it not already exists, use this flag to delete the old screen before creating a new." }
		},
		{ "screen-name", "screen-dir", "type" });

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	if (fs::exists(screenDir))
	{
		if (vm.count("force"))
			fs::remove_all(screenDir);
		else
			throw std::runtime_error("Screen already exists, use --force to delete old screen");
	}

	auto type = zeep::value_serializer<ScreenType>::from_string(vm["type"].as<std::string>());
	switch (type)
	{
		case ScreenType::IntracellularPhenotype:
		{
			if (not (vm.count("low") and vm.count("high")))
				throw std::runtime_error("For IP screens you should provide both low and high fastq files");

			std::unique_ptr<IPScreenData> data(ScreenData::create<IPScreenData>(screenDir));

			data->addFiles(vm["low"].as<std::string>(), vm["high"].as<std::string>());
			break;
		}

		case ScreenType::SyntheticLethal:
		{
			if (vm.count("replicate") < 1)
				throw std::runtime_error("For IP screens you should provide at least one replicate fastq file");

			std::unique_ptr<SLScreenData> data(ScreenData::create<SLScreenData>(screenDir));

			for (auto& replicate: vm["replicate"].as<std::vector<std::string>>())
				data->addFile(replicate);
			break;
		}
	}

	return result;
}

// --------------------------------------------------------------------

int main_map(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( map screen-name assembly [options])",
		{
			{ "bowtie-index", po::value<std::string>(),	"Bowtie index filename stem for the assembly" },
			{ "force",	new po::untyped_value(true),	"By default a screen is only mapped if it was not mapped already, use this flag to force creating a new mapping." }
		},
		{ "screen-name", "assembly" });

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	const auto& [ data, type ] = ScreenData::create(screenDir);

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

	unsigned trimLength = 50;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	unsigned threads = 1;
	if (vm.count("threads"))
		threads = vm["threads"].as<unsigned>();

	data->map(assembly, trimLength, bowtie, bowtieIndex, threads);

	return result;
}

// --------------------------------------------------------------------

int analyze_ip(po::variables_map& vm, IPScreenData& screenData)
{
	if (vm.count("assembly") == 0 or
		vm.count("mode") == 0 or (vm["mode"].as<std::string>() != "collapse" and vm["mode"].as<std::string>() != "longest") or
		vm.count("start") == 0 or vm.count("end") == 0 or
		(vm.count("overlap") != 0 and vm["overlap"].as<std::string>() != "both" and vm["overlap"].as<std::string>() != "neither"))
	{
		std::cerr << R"(
Mode longest means take the longest transcript for each gene

Mode collapse means, for each gene take the region between the first 
start and last end.

Start and end should be either 'cds' or 'tx' with an optional offset 
appended. Optionally you can also specify cdsStart, cdsEnd, txStart
or txEnd to have the start at the cdsEnd e.g.

Overlap: in case of both, all genes will be added, in case of neither
the parts with overlap will be left out.

Examples:

    --mode=longest --start=cds-100 --end=cds

        For each gene take the longest transcript. For these we take the 
        cdsStart minus 100 basepairs as start and cdsEnd as end. This means
        no  3' UTR and whatever fits in the 100 basepairs of the 5' UTR.

    --mode=collapse --start=tx --end=tx+1000

        For each gene take the minimum txStart of all transcripts as start
        and the maximum txEnd plus 1000 basepairs as end. This obviously
        includes both 5' UTR and 3' UTR.

)"				<< std::endl;
		exit(vm.count("help") ? 0 : 1);
	}

	std::string assembly = vm["assembly"].as<std::string>();

	unsigned trimLength = 0;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	// -----------------------------------------------------------------------

	bool cutOverlap = true;
	if (vm.count("overlap") and vm["overlap"].as<std::string>() == "both")
		cutOverlap = false;
	
	Mode mode;
	if (vm["mode"].as<std::string>() == "collapse")
		mode = Mode::Collapse;
	else // if (vm["mode"].as<std::string>() == "longest")
		mode = Mode::Longest;

	auto transcripts = loadTranscripts(assembly, mode, vm["start"].as<std::string>(), vm["end"].as<std::string>(), cutOverlap);

	// -----------------------------------------------------------------------

	std::vector<Insertions> lowInsertions, highInsertions;

	screenData.analyze(assembly, trimLength, transcripts, lowInsertions, highInsertions);

	long lowSenseCount = 0, lowAntiSenseCount = 0;
	for (auto& i: lowInsertions)
	{
		lowSenseCount += i.sense.size();
		lowAntiSenseCount += i.antiSense.size();
	}

	long highSenseCount = 0, highAntiSenseCount = 0;
	for (auto& i: highInsertions)
	{
		highSenseCount += i.sense.size();
		highAntiSenseCount += i.antiSense.size();
	}

	// -----------------------------------------------------------------------
	
	std::cerr << std::endl
			<< std::string(get_terminal_width(), '-') << std::endl
			<< "Low: " << std::endl
			<< " sense      : " << std::setw(10) << lowSenseCount << std::endl
			<< " anti sense : " << std::setw(10) << lowAntiSenseCount << std::endl
			<< "High: " << std::endl
			<< " sense      : " << std::setw(10) << highSenseCount << std::endl
			<< " anti sense : " << std::setw(10) << highAntiSenseCount << std::endl;

	Direction direction = Direction::Sense;
	if (vm.count("direction"))
	{
		if (vm["direction"].as<std::string>() == "sense")
			direction = Direction::Sense;
		else if (vm["direction"].as<std::string>() == "antisense" or vm["direction"].as<std::string>() == "anti-sense")
			direction = Direction::AntiSense;
		else if (vm["direction"].as<std::string>() == "both")
			direction = Direction::Both;
		else
		{
			std::cerr << "invalid direction" << std::endl;
			exit(1);
		}
	}

	for (auto& dp: screenData.dataPoints(transcripts, lowInsertions, highInsertions, direction))
	{
		std::cout << dp.geneName << '\t'
				<< dp.low << '\t'
				<< dp.high << '\t'
				<< dp.pv << '\t'
				<< dp.fcpv << '\t'
				<< std::log2(dp.mi) << std::endl;
	}

	return 0;
}

int analyze_sl(po::variables_map& vm, SLScreenData& screenData, SLScreenData& controlData)
{
	if (vm.count("assembly") == 0 or vm.count("control") == 0 or
		((vm.count("start") == 0 or vm.count("end") == 0) and vm.count("gene-bed-file") == 0))
	{
		std::cerr << R"(
Start and end should be either 'cds' or 'tx' with an optional offset 
appended. Optionally you can also specify cdsStart, cdsEnd, txStart
or txEnd to have the start at the cdsEnd e.g.
)"				<< std::endl;
		exit(vm.count("help") ? 0 : 1);
	}

	std::string assembly = vm["assembly"].as<std::string>();

	unsigned trimLength = 0;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	// -----------------------------------------------------------------------

	std::vector<Transcript> transcripts;

	if (vm.count("gene-bed-file"))
		transcripts = loadTranscripts(vm["gene-bed-file"].as<std::string>());
	else
	{
		transcripts = loadTranscripts(assembly, Mode::Longest, vm["start"].as<std::string>(), vm["end"].as<std::string>(), true);
		filterOutExons(transcripts);
	}

	// reorder transcripts based on chr > end-position, makes code easier and faster
	std::sort(transcripts.begin(), transcripts.end(), [](auto& a, auto& b)
	{
		int d = a.chrom - b.chrom;
		if (d == 0)
			d = a.start() - b.start();
		return d < 0;
	});

	// --------------------------------------------------------------------
	
	unsigned replicate = 1;
	if (vm.count("replicate"))
		replicate = vm["replicate"].as<unsigned short>();

	unsigned groupSize = 500;
	if (vm.count("group-size"))
		groupSize = vm["group-size"].as<unsigned short>();

	// -----------------------------------------------------------------------

	for (auto& dp: screenData.dataPoints(replicate, assembly, trimLength, transcripts, controlData, groupSize))
	{
		std::cout << dp.geneName << '\t'
				  << dp.sense << '\t'
				  << dp.antisense << '\t'
				  << dp.pv << '\t'
				  << dp.fcpv << '\t'
				  << dp.sense_normalized << '\t'
				  << dp.antisense_normalized << '\t'
				  << dp.ref_fcpv[0] << '\t'
				  << dp.ref_pv[0] << '\t'
				  << dp.ref_fcpv[1] << '\t'
				  << dp.ref_pv[1] << '\t'
				  << dp.ref_fcpv[2] << '\t'
				  << dp.ref_pv[2] << '\t'
				  << dp.ref_fcpv[3] << '\t'
				  << dp.ref_pv[3] << '\t'
				  << (dp.sense + dp.antisense) << '\t'
				  << ((dp.sense_normalized + 1.0f) / (dp.sense_normalized + dp.antisense_normalized + 2.0f)) << std::endl;
	}

	return 0;
}

int main_analyze(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( analyze screen-name assembly [options])",
		{
			{ "mode",		po::value<std::string>(),	"Mode, should be either collapse, longest" },
			{ "start",		po::value<std::string>(),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "end",		po::value<std::string>(),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "overlap",	po::value<std::string>(),	"Supported values are both or neither." },
			{ "direction",	po::value<std::string>(),	"Direction for the counted integrations, can be 'sense', 'antisense' or 'both'" },

			{ "replicate",	po::value<unsigned short>(),"The replicate to use, in case of synthetic lethal"},
			{ "group-size",	po::value<unsigned short>(),"The group size to use for normalizing insertions counts in SL, default is 500"},

			{ "gene-bed-file", po::value<std::string>(),	"Optionally provide a gene BED file instead of calculating one" },
			
			{ "output",		po::value<std::string>(),	"Output file" }
		}, { "screen-name", "assembly" });

	// fail early
	std::ofstream out;
	if (vm.count("output"))
	{
		out.open(vm["output"].as<std::string>());
		if (not out.is_open())
			throw std::runtime_error("Could not open output file");
	}

	std::streambuf* sb = nullptr;
	if (out.is_open())
		sb = std::cout.rdbuf(out.rdbuf());

	fs::path screenDir = vm["screen-dir"].as<std::string>();

	const auto& [ data, type ] = ScreenData::create(screenDir / vm["screen-name"].as<std::string>());

	switch (type)
	{
		case ScreenType::IntracellularPhenotype:
			result = analyze_ip(vm, *static_cast<IPScreenData*>(data.get()));
			break;

		case ScreenType::SyntheticLethal:
		{
			SLScreenData controlScreen(screenDir / vm["control"].as<std::string>());
			result = analyze_sl(vm, *static_cast<SLScreenData*>(data.get()), controlScreen);
			break;
		}
	}

	if (sb)
		std::cout.rdbuf(sb);

	return result;
}

// --------------------------------------------------------------------
// Write out the resulting genes as a BED file

int main_refseq(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( refseq [options])",
		{
			{ "mode",		po::value<std::string>(),	"Mode, should be either collapse, longest" },
			{ "start",		po::value<std::string>(),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "end",		po::value<std::string>(),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "overlap",	po::value<std::string>(),	"Supported values are both or neither." },
			// { "direction",	po::value<std::string>(),	"Direction for the counted integrations, can be 'sense', 'antisense' or 'both'" },
			{ "no-exons",	new po::untyped_value(true),"Leave out exons" },
			{ "sort",		po::value<std::string>(),	"Sort result by 'name' or 'position'"},
			{ "output",		po::value<std::string>(),	"Output file" }
		}, { "assembly" });

	if (vm.count("assembly") == 0 or
		vm.count("mode") == 0 or (vm["mode"].as<std::string>() != "collapse" and vm["mode"].as<std::string>() != "longest") or
		vm.count("start") == 0 or vm.count("end") == 0 or
		(vm.count("overlap") != 0 and vm["overlap"].as<std::string>() != "both" and vm["overlap"].as<std::string>() != "neither"))
	{
		std::cerr << R"(
Mode longest means take the longest transcript for each gene

Mode collapse means, for each gene take the region between the first 
start and last end.

Overlap: in case of both, all genes will be added, in case of neither
the parts with overlap will be left out.
)"				<< std::endl;
		exit(vm.count("help") ? 0 : 1);
	}

	// fail early
	std::ofstream out;
	if (vm.count("output"))
	{
		out.open(vm["output"].as<std::string>());
		if (not out.is_open())
			throw std::runtime_error("Could not open output file");
	}

	bool cutOverlap = true;
	if (vm.count("overlap") and vm["overlap"].as<std::string>() == "both")
		cutOverlap = false;

	std::string assembly = vm["assembly"].as<std::string>();

	Mode mode;
	if (vm["mode"].as<std::string>() == "collapse")
		mode = Mode::Collapse;
	else // if (vm["mode"].as<std::string>() == "longest")
		mode = Mode::Longest;

	auto transcripts = loadTranscripts(assembly, mode, vm["start"].as<std::string>(), vm["end"].as<std::string>(), cutOverlap);
	if (vm.count("no-exons"))
		filterOutExons(transcripts);
	
	if (vm.count("sort") and vm["sort"].as<std::string>() == "name")
		std::sort(transcripts.begin(), transcripts.end(), [](auto& a, auto& b) { return a.name < b.name; });

	std::streambuf* sb = nullptr;
	if (out.is_open())
		sb = std::cout.rdbuf(out.rdbuf());

	for (auto& transcript: transcripts)
	{
		for (auto& range: transcript.ranges)
		{
			std::cout
				<< transcript.chrom << '\t'
				<< range.start << '\t'
				<< range.end << '\t'
				<< transcript.geneName << '\t'
				<< 0 << '\t'
				<< transcript.strand << std::endl;
		}
	}

	if (sb)
		std::cout.rdbuf(sb);

	return result;

	return 0;
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
	
	std::string address = "0.0.0.0";
	if (vm.count("address"))
		address = vm["address"].as<std::string>();

	uint16_t port = 10338;
	if (vm.count("port"))
		port = vm["port"].as<uint16_t>();

	std::string command = vm["command"].as<std::string>();

	if (command == "start")
	{
		std::cout << "starting server at " << address << ':' << port << std::endl;

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

int main_correct(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( correct screen-name assembly offset [options])",
		{
			{ "offset", po::value<int>(),	"The offset to correct the - strand with" }
		},
		{ "screen-name", "assembly", "offset" });

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	const auto& [ data, type ] = ScreenData::create(screenDir);

	std::string assembly = vm["assembly"].as<std::string>();

	unsigned trimLength = 50;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	int offset = vm["offset"].as<int>();

	data->correct_map(assembly, trimLength, offset);

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

	const auto& [ data, type ] = ScreenData::create(screenDir);

	std::string assembly = vm["assembly"].as<std::string>();

	unsigned trimLength = 50;
	if (vm.count("trim-length"))
		trimLength = vm["trim-length"].as<unsigned>();
	
	auto file = vm["file"].as<std::string>();

	data->dump_map(assembly, trimLength, file);

	return result;
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
		else if (command == "correct")
			result = main_correct(argc - 1, argv + 1);
		else if (command == "dump")
			result = main_dump(argc - 1, argv + 1);
		else if (command == "help")
			usage();
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
