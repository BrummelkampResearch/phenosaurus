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

// --------------------------------------------------------------------

int usage()
{
	std::cerr << "Usage: screen-analyzer command [options]" << std::endl
			  << std::endl
			  << "Where command is one of" << std::endl
			  << std::endl
			  << "  create  -- create new screen" << std::endl
			  << "  map     -- map a screen to an assembly" << std::endl
			  << "  analyze -- analyze mapped reads" << std::endl
			  << "  refseq  -- create reference gene table" << std::endl
			  << "  server  -- start/stop server process" << std::endl
			  << std::endl;
	return 1;
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

int main_map(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( map screen-name assembly [options])",
		{
			{ "screen-name",	po::value<std::string>(),		"The screen to map" },
			{ "bowtie-index",	po::value<std::string>(),		"Bowtie index filename stem for the assembly" },
			{ "force",			new po::untyped_value(true),	"By default a screen is only mapped if it was not mapped already, use this flag to force creating a new mapping." }
		},
		{ "screen-name", "assembly" },
		{ "screen-name", "assembly" });

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= vm["screen-name"].as<std::string>();

	auto data = ScreenData::load(screenDir);

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

int analyze_ip(po::variables_map& vm, IPPAScreenData& screenData)
{
	if (vm.count("assembly") == 0 or
		vm.count("start") == 0 or vm.count("end") == 0 or
		(vm.count("overlap") != 0 and vm["overlap"].as<std::string>() != "both" and vm["overlap"].as<std::string>() != "neither"))
	{
		std::cerr << R"(
Mode longest-transcript means take the longest transcript for each gene,

Mode longest-exon means the longest expression region, which can be
different from the longest-transcript.

Mode collapse means, for each gene take the region between the first 
start and last end.

Start and end should be either 'cds' or 'tx' with an optional offset 
appended. Optionally you can also specify cdsStart, cdsEnd, txStart
or txEnd to have the start at the cdsEnd e.g.

Overlap: in case of both, all genes will be added, in case of neither
the parts with overlap will be left out.

Examples:

	--mode=longest-transcript --start=cds-100 --end=cds

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
	
	Mode mode = zeep::value_serializer<Mode>::from_string(vm["mode"].as<std::string>());

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

	std::cout << "gene" << '\t'
			  << "low" << '\t'
			  << "high" << '\t'
			  << "pv" << '\t'
			  << "fcpv" << '\t'
			  << "log2(mi)" << std::endl;

	for (auto& dp: screenData.dataPoints(transcripts, lowInsertions, highInsertions, direction))
	{
		std::cout << dp.gene << '\t'
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
		Mode mode = zeep::value_serializer<Mode>::from_string(vm["mode"].as<std::string>());

		bool cutOverlap = true;
		if (vm.count("overlap") and vm["overlap"].as<std::string>() == "both")
			cutOverlap = false;

		transcripts = loadTranscripts(assembly, mode, vm["start"].as<std::string>(), vm["end"].as<std::string>(), cutOverlap);
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

	unsigned groupSize = 500;
	if (vm.count("group-size"))
		groupSize = vm["group-size"].as<unsigned short>();

	float pvCutOff = vm["pv-cut-off"].as<float>();
	float binom_fdrCutOff = vm["binom-fdr-cut-off"].as<float>();
	float oddsRatio = vm["odds-ratio"].as<float>();
	if (oddsRatio < 0 or oddsRatio >= 1)
		throw std::runtime_error("Odds ratio should be between 0 and 1");

	// -----------------------------------------------------------------------

	auto r = screenData.dataPoints(assembly, trimLength, transcripts, controlData, groupSize);
	bool significantOnly = vm.count("significant");

	if (vm.count("no-header") == 0)
	{
		std::cout
				<< "gene" << '\t'
				<< "odds_ratio" << '\t';
		
		for (size_t i = 0; i < (r.empty() ? 0 :  r[0].replicates.size()); ++i)
		{
			std::cout
				<< "sense" << '\t'
				<< "antisense" << '\t'
				<< "binom_fdr" << '\t'
				<< "sense_normalized" << '\t'
				<< "antisense_normalized" << '\t'
				// << "fcpv_control_1 " << '\t'
				<< "pv_control_1" << '\t'
				// << "fcpv_control_2" << '\t'
				<< "pv_control_2" << '\t'
				// << "fcpv_control_3" << '\t'
				<< "pv_control_3" << '\t'
				// << "fcpv_control_4" << '\t'
				<< "pv_control_4" << '\t';
				// << "effect";
		}

		std::cout << std::endl;
	}

	for (auto& dp : r)
	{
		if (significantOnly)
		{
			if (dp.oddsRatio >= oddsRatio)
				continue;
			
			size_t n = 0;
			for (auto &c : dp.replicates)
			{
				if (c.binom_fdr >= binom_fdrCutOff)
					continue;
				
				if (c.ref_pv[0] >= pvCutOff or c.ref_pv[1] >= pvCutOff or c.ref_pv[2] >= pvCutOff or c.ref_pv[3] >= pvCutOff)
					continue;
				
				++n;
			}

			if (n != dp.replicates.size())
				continue;
		}
		
		std::cout
			<< dp.gene << '\t'
			<< dp.oddsRatio << '\t';
		
		for (auto &c : dp.replicates)
		{
			std::cout
				<< c.sense << '\t'
				<< c.antisense << '\t'
				<< c.binom_fdr << '\t'
				<< c.sense_normalized << '\t'
				<< c.antisense_normalized << '\t'
				//   << c.ref_fcpv[0] << '\t'
				<< c.ref_pv[0] << '\t'
				//   << c.ref_fcpv[1] << '\t'
				<< c.ref_pv[1] << '\t'
				//   << c.ref_fcpv[2] << '\t'
				<< c.ref_pv[2] << '\t'
				//   << c.ref_fcpv[3] << '\t'
				<< c.ref_pv[3] << '\t';
		}

		std::cout << std::endl;
	}

	return 0;
}

// int analyze_vb(int argc, char* const argv[])
// {
// 	int result = 0;

// 	auto vm = load_options(argc, argv, PACKAGE_NAME R"( analyze screen-name assembly [options])",
// 		{
// 			{ "screen-name",	po::value<std::string>(),					"The screen to analyze" },

// 			{ "group-size",		po::value<unsigned short>()->default_value(500),
// 																			"The group size to use for normalizing insertions counts in SL, default is 500"},
// 			{ "significant",	new po::untyped_value(true),				"The significant genes only, in case of synthetic lethal"},

// 			{ "pv-cut-off",		po::value<float>()->default_value(0.05f),	"P-value cut off"},
// 			{ "binom-fdr-cut-off",
// 								po::value<float>()->default_value(1.f),		"binom FDR cut off" },
// 			{ "odds-ratio",
// 								po::value<float>()->default_value(1.2f),	"Odds Ratio" },

// 			{ "no-header",		new po::untyped_value(true),				"Do not print a header line" },

// 			{ "output,o",		po::value<std::string>(),					"Output file" }
// 		}, { "screen-name" }, { "screen-name" });

// 	// fail early
// 	std::ofstream out;
// 	if (vm.count("output"))
// 	{
// 		out.open(vm["output"].as<std::string>());
// 		if (not out.is_open())
// 			throw std::runtime_error("Could not open output file");
// 	}

// 	std::streambuf* sb = nullptr;
// 	if (out.is_open())
// 		sb = std::cout.rdbuf(out.rdbuf());

// 	std::string control = "WT";
// 	if (vm.count("control"))
// 		control = vm["control"].as<std::string>();
	
// 	// -----------------------------------------------------------------------

// 	std::vector<Transcript> transcripts;

// 	// if (vm.count("gene-bed-file"))
// 	// 	transcripts = loadTranscripts(vm["gene-bed-file"].as<std::string>());
// 	// else
// 	// {
// 	// 	Mode mode = zeep::value_serializer<Mode>::from_string(vm["mode"].as<std::string>());

// 	// 	bool cutOverlap = true;
// 	// 	if (vm.count("overlap") and vm["overlap"].as<std::string>() == "both")
// 	// 		cutOverlap = false;

// 	// 	transcripts = loadTranscripts(assembly, mode, vm["start"].as<std::string>(), vm["end"].as<std::string>(), cutOverlap);
// 	// 	filterOutExons(transcripts);
// 	// }

// 	// // reorder transcripts based on chr > end-position, makes code easier and faster
// 	// std::sort(transcripts.begin(), transcripts.end(), [](auto& a, auto& b)
// 	// {
// 	// 	int d = a.chrom - b.chrom;
// 	// 	if (d == 0)
// 	// 		d = a.start() - b.start();
// 	// 	return d < 0;
// 	// });

// 	// --------------------------------------------------------------------

// 	unsigned groupSize = vm["group-size"].as<unsigned short>();

// 	float pvCutOff = vm["pv-cut-off"].as<float>();
// 	float binom_fdrCutOff = vm["binom-fdr-cut-off"].as<float>();
// 	float oddsRatio = vm["odds-ratio"].as<float>();

// 	// -----------------------------------------------------------------------

// 	std::array<std::vector<InsertionCount>, 3> insertions;

// 	std::ifstream file(vm["screen-name"].as<std::string>() + ".all");
// 	if (not file.is_open())
// 		throw std::runtime_error("Could not open file " + vm["screen-name"].as<std::string>() + ".all");
	
// 	std::string line;
// 	while (std::getline(file, line))
// 	{
// 		auto s = line.find('\t');
// 		transcripts.push_back({ line.substr(0, s) });
// 		transcripts.back().geneName = transcripts.back().name;

// 		char *p = const_cast<char*>(line.c_str() + s + 1);

// 		for (int i = 0; i < 3; ++i)
// 		{
// 			size_t sense = strtol(p, &p, 10);
// 			size_t antisense = strtol(p, &p, 10);

// 			insertions[i].push_back(InsertionCount{ sense, antisense });
// 		}
// 	}
// 	file.close();

// 	std::array<std::vector<InsertionCount>, 4> controlInsertions;

// 	file.open(control + ".all");
// 	if (not file.is_open())
// 		throw std::runtime_error("Could not open file " + control + ".all");
	
// 	while (std::getline(file, line))
// 	{
// 		auto s = line.find('\t');
		
// 		std::string gene = line.substr(0, s);
// 		if (transcripts[controlInsertions[0].size()].geneName != gene)
// 			throw std::runtime_error("Gene names do not match: " + gene + " != " + transcripts[controlInsertions[0].size()].geneName);

// 		char *p = const_cast<char*>(line.c_str() + s + 1);

// 		for (int i = 0; i < 4; ++i)
// 		{
// 			size_t sense = strtol(p, &p, 10);
// 			size_t antisense = strtol(p, &p, 10);

// 			controlInsertions[i].push_back(InsertionCount{ sense, antisense });
// 		}
// 	}
// 	file.close();

// 	auto r = SLdataPoints(transcripts, insertions, controlInsertions, groupSize, pvCutOff, binom_fdrCutOff, oddsRatio);
// 	bool significantOnly = vm.count("significant");

// 	if (vm.count("no-header") == 0)
// 	{
// 		std::cout
// 				<< "replicate" << '\t'
// 				<< "gene" << '\t'
// 				<< "sense" << '\t'
// 				<< "antisense" << '\t'
// 				<< "binom_fdr" << '\t'
// 				<< "sense_normalized" << '\t'
// 				<< "antisense_normalized" << '\t'
// 				// << "fcpv_control_1 " << '\t'
// 				<< "pv_control_1" << '\t'
// 				// << "fcpv_control_2" << '\t'
// 				<< "pv_control_2" << '\t'
// 				// << "fcpv_control_3" << '\t'
// 				<< "pv_control_3" << '\t'
// 				// << "fcpv_control_4" << '\t'
// 				<< "pv_control_4" << '\t'
// 				<< "effect"
// 				<< std::endl;
// 	}

// 	for (const auto& replicate: r.replicate)
// 	{
// 		for (size_t i = 0; i < transcripts.size(); ++i)
// 		{
// 			auto& dp = replicate.data[i];

// 			if (significantOnly and not r.significant.count(dp.gene))
// 				continue;
			
// 			std::cout
// 				<< replicate.name << '\t'
// 				<< dp.gene << '\t'
// 				<< dp.sense << '\t'
// 				<< dp.antisense << '\t'
// 				<< dp.binom_fdr << '\t'
// 				<< dp.sense_normalized << '\t'
// 				<< dp.antisense_normalized << '\t'
// 				//   << dp.ref_fcpv[0] << '\t'
// 				<< dp.ref_pv[0] << '\t'
// 				//   << dp.ref_fcpv[1] << '\t'
// 				<< dp.ref_pv[1] << '\t'
// 				//   << dp.ref_fcpv[2] << '\t'
// 				<< dp.ref_pv[2] << '\t'
// 				//   << dp.ref_fcpv[3] << '\t'
// 				<< dp.ref_pv[3] << '\t'
// 				<< dp.strength
// 				<< std::endl;
// 		}
// 	}

// 	return 0;
// }

int main_analyze(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( analyze screen-name assembly [options])",
		{
			{ "screen-name",	po::value<std::string>(),		"The screen to analyze" },

			{ "mode",		po::value<std::string>()->default_value("longest-exon"),	"Mode, should be either collapse, longest-exon or longest-transcript" },
			{ "start",		po::value<std::string>()->default_value("tx"),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "end",		po::value<std::string>()->default_value("cds"),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "overlap",	po::value<std::string>(),	"Supported values are both or neither." },
			{ "direction",	po::value<std::string>(),	"Direction for the counted integrations, can be 'sense', 'antisense' or 'both'" },

			{ "group-size",	po::value<unsigned short>(),"The group size to use for normalizing insertions counts in SL, default is 500"},
			{ "significant", new po::untyped_value(true),	"The significant genes only, in case of synthetic lethal"},

			{ "pv-cut-off",	po::value<float>()->default_value(0.05f),
														"P-value cut off"},
			{ "binom-fdr-cut-off",
							po::value<float>()->default_value(0.05f),
														"binom FDR cut off" },
			{ "odds-ratio",
							po::value<float>()->default_value(0.8f),
														"Odds Ratio" },

			{ "gene-bed-file", po::value<std::string>(),	"Optionally provide a gene BED file instead of calculating one" },

			{ "refseq",		po::value<std::string>(),		"Alternative refseq file to use" },
			
			{ "no-header",	new po::untyped_value(true),	"Do not print a header line" },

			{ "output,o",	po::value<std::string>(),	"Output file" }
		}, { "screen-name", "assembly" }, { "screen-name", "assembly" });

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

	// Load refseq, if specified
	if (vm.count("refseq"))
		init_refseq(vm["refseq"].as<std::string>());

	fs::path screenDir = vm["screen-dir"].as<std::string>();

	auto data = ScreenData::load(screenDir / vm["screen-name"].as<std::string>());

	switch (data->get_type())
	{
		case ScreenType::IntracellularPhenotype:
			result = analyze_ip(vm, *static_cast<IPScreenData*>(data.get()));
			break;

		case ScreenType::IntracellularPhenotypeActivation:
			result = analyze_ip(vm, *static_cast<PAScreenData*>(data.get()));
			break;

		case ScreenType::SyntheticLethal:
		{
			SLScreenData controlScreen(screenDir / vm["control"].as<std::string>());
			result = analyze_sl(vm, *static_cast<SLScreenData*>(data.get()), controlScreen);
			break;
		}

		case ScreenType::Unspecified:
			throw std::runtime_error("Unknown screen type");
			break;
	}

	if (sb)
		std::cout.rdbuf(sb);

	return result;
}

// --------------------------------------------------------------------

int main_server(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( command [options])",
		{
			{ "command", 		po::value<std::string>(),		"Server command" },
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

  dump      dump a screen's insertions
  analyze   analyze a screen
			 )" << std::endl;
		exit(vm.count("help") ? 0 : 1);
	}

	std::string command = vm["command"].as<std::string>();

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

#if DEBUG
	char exePath[PATH_MAX + 1];
	int r = readlink("/proc/self/exe", exePath, PATH_MAX);
	if (r > 0)
	{
		exePath[r] = 0;
		docroot = fs::weakly_canonical(exePath).parent_path() / "docroot";
	}
	
	if (not fs::exists(docroot))
		throw std::runtime_error("Could not locate docroot directory");
#endif

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
// Write out the resulting genes as a BED file

int main_refseq(int argc, char* const argv[])
{
	auto vm = load_options(argc, argv, PACKAGE_NAME R"( refseq [options])",
		{
			{ "mode",		po::value<std::string>()->default_value("longest-transcript"),	"Mode, should be either collapse, longest-transcript or longest-exon" },
			{ "start",		po::value<std::string>()->default_value("tx"),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "end",		po::value<std::string>()->default_value("cds"),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "overlap",	po::value<std::string>(),	"Supported values are both or neither." },
			// { "direction",	po::value<std::string>(),	"Direction for the counted integrations, can be 'sense', 'antisense' or 'both'" },
			{ "no-exons",	new po::untyped_value(true),"Leave out exons" },
			{ "sort",		po::value<std::string>(),	"Sort result by 'name' or 'position'"},
			{ "output",		po::value<std::string>(),	"Output file" }
		}, { "assembly" }, { "assembly" });

	if (vm.count("assembly") == 0 or
		vm.count("start") == 0 or vm.count("end") == 0 or
		(vm.count("overlap") != 0 and vm["overlap"].as<std::string>() != "both" and vm["overlap"].as<std::string>() != "neither"))
	{
		std::cerr << R"(
Mode longest-transcript means take the longest transcript for each gene,

Mode longest-exon means the longest expression region, which can be
different from the longest-transcript.

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

	Mode mode = zeep::value_serializer<Mode>::from_string(vm["mode"].as<std::string>());

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

	return 0;
}

// --------------------------------------------------------------------

int main_dump(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( dump screen-name assembly file [options])",
		{
			{ "screen-name",	po::value<std::string>(),	"The screen to dump" },
			{ "file",			po::value<std::string>(),	"The file to dump" }
		},
		{ "screen-name", "assembly", "file" },
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
		if (argc < 2)
		{
			usage();
			exit(-1);
		}

		std::string command = argv[1];
		// if (command == "create")
		// 	result = main_create(argc - 1, argv + 1);
		if (command == "map")
		 	result = main_map(argc - 1, argv + 1);
		else if (command == "analyze")
			result = main_analyze(argc - 1, argv + 1);
		// else if (command == "vb")
		// 	result = analyze_vb(argc - 1, argv + 1);
		else if (command == "refseq")
			result = main_refseq(argc - 1, argv + 1);
		else if (command == "server")
			result = main_server(argc - 1, argv + 1);
		// else if (command == "compress")
		// 	result = main_compress(argc - 1, argv + 1);
		else if (command == "dump")
			result = main_dump(argc - 1, argv + 1);
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
