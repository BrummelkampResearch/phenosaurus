// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

#include <pwd.h>

#include <filesystem>
#include <fstream>

#include <boost/algorithm/string.hpp>

#include <zeep/json/element.hpp>

#include "utils.hpp"

#include "screen-analyzer.hpp"
#include "screen-data.hpp"
#include "screen-creator.hpp"
#include "screen-analyzer.hpp"
#include "user-service.hpp"

namespace ba = boost::algorithm;
namespace fs = std::filesystem;
namespace po = boost::program_options;

using json = zeep::json::element;	

// --------------------------------------------------------------------

void SetStdinEcho(bool inEnable)
{
	struct termios tty;
	::tcgetattr(STDIN_FILENO, &tty);
	if(not inEnable)
		tty.c_lflag &= ~ECHO;
	else
		tty.c_lflag |= ECHO;

	(void)::tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

bool askYesNo(const std::string& msg, bool defaultYes)
{
	std::string yesno;
	std::cout << msg << (defaultYes ? " [Y/n]: " : " [y/N]: "); std::cout.flush();
	std::getline(std::cin, yesno);

	return yesno.empty() ? defaultYes : ba::iequals(yesno, "y") or ba::iequals(yesno, "yes");
}

std::string ask(const std::string& msg, std::string defaultAnswer = {})
{
	if (defaultAnswer.empty())
		std::cout << msg << ": ";
	else
		std::cout << msg << " [" << defaultAnswer << "]: ";
	std::cout.flush();

	std::string answer;
	std::getline(std::cin, answer);
	return answer.empty() ? defaultAnswer : answer;
}

std::string ask_mandatory(const std::string& msg, std::string defaultAnswer = {})
{
	std::string answer;
	for (;;)
	{
		answer = ask(msg, defaultAnswer);
		if (answer.empty())
		{
			std::cout << "This information is required" << std::endl;
			continue;
		}
		break;
	}
	return answer;
}

std::string askPasswordSimple()
{
	std::string password;

	std::cout << "Password: "; std::cout.flush(); SetStdinEcho(false);
	std::getline(std::cin, password);
	std::cout << std::endl;

	SetStdinEcho(true);

	return password;
}

// --------------------------------------------------------------------

int main_create(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( create screen-name --type <screen-type> [options])",
		{
			{ "scientist", po::value<std::string>(),		"The scientist who did the experiment" },

			{ "type", po::value<std::string>(),				"The screen type to create, should be one of 'ip', 'pa' or 'sl'" },
			{ "low", po::value<std::string>(),				"The path to the LOW FastQ file" },
			{ "high", po::value<std::string>(),				"The path to the HIGH FastQ file" },

			{ "cell-line", po::value<std::string>(),		"The cell line used in the experiment" },
			{ "description", po::value<std::string>(),		"A description of the experiment" },
			{ "long-description", po::value<std::string>(),	"A longer, extra description of the experiment" },
			{ "induced", new po::untyped_value(true),		"Flag indication an induced experiment" },
			{ "knockout", new po::untyped_value(true),		"Flag indication a knockout experiment" },
			{ "ignore", new po::untyped_value(true),		"Flag indication this screen should not participate in uniqueness scoring and other aggregated analysis" },

			{ "replicate", po::value<std::vector<std::string>>(),
													"The replicate file, can be specified multiple times"},
			{ "force", new po::untyped_value(true),	"By default a screen is only created if it not already exists, use this flag to delete the old screen before creating a new." }
		},
		{ "screen-dir" });

	screen_info screen;

	screen.created = boost::posix_time::second_clock().local_time();

	if (vm.count("screen-name"))
		screen.name = vm["screen-name"].as<std::string>();
	else
		screen.name = ask_mandatory("Screen name");

	fs::path screenDir = vm["screen-dir"].as<std::string>();
	screenDir /= screen.name;

	if (fs::exists(screenDir))
	{
		if (vm.count("force"))
			fs::remove_all(screenDir);
		else
			throw std::runtime_error("Screen already exists, use --force to delete old screen");
	}

	for (;;)
	{
		std::string s;
		if (vm.count("type"))
			s = vm["type"].as<std::string>();
		else
			s = ask("Screen type [ip/sl/pa]");

		screen.type = zeep::value_serializer<ScreenType>::from_string(s);
		if (screen.type == ScreenType::Unspecified)
		{
			std::cout << "Unknown screen type" << std::endl;
			continue;
		}
		break;
	}

	if (vm.count("creator"))
		screen.scientist = vm["creator"].as<std::string>();
	else
		screen.scientist = ask_mandatory("Scientist username", get_user_name());

	if (vm.count("cell-line"))
		screen.cell_line = vm["cell-line"].as<std::string>();
	else
		screen.cell_line = ask_mandatory("Cell line", "HAP1");

	if (vm.count("description"))
		screen.description = vm["description"].as<std::string>();
	else
		screen.description = ask_mandatory("Description");

	if (vm.count("long-description"))
		screen.description = vm["long-description"].as<std::string>();
	else
		screen.description = ask("Long description");

	if (vm.count("induced"))
		screen.induced = true;
	else
		screen.induced = askYesNo("Induced", false);

	if (vm.count("knockout"))
		screen.knockout = true;
	else
		screen.knockout = askYesNo("Knockout", false);

	screen.ignore = true;

	switch (screen.type)
	{
		case ScreenType::IntracellularPhenotype:
		{
			if (not (vm.count("low") and vm.count("high")))
				throw std::runtime_error("For IP screens you should provide both low and high fastq files");
			
			auto low = vm["low"].as<std::string>();
			auto high = vm["high"].as<std::string>();
			auto data = std::make_unique<IPScreenData>(screenDir, screen, low, high);
			break;
		}

		case ScreenType::IntracellularPhenotypeActivation:
		{
			if (not (vm.count("low") and vm.count("high")))
				throw std::runtime_error("For IP screens you should provide both low and high fastq files");
			
			auto low = vm["low"].as<std::string>();
			auto high = vm["high"].as<std::string>();
			auto data = std::make_unique<PAScreenData>(screenDir, screen, low, high);
			break;
		}

		case ScreenType::SyntheticLethal:
		{
			if (vm.count("replicate") < 1)
				throw std::runtime_error("For IP screens you should provide at least one replicate fastq file");

			auto data = std::make_unique<SLScreenData>(screenDir, screen);
			int nr = 1;
			for (auto& replicate: vm["replicate"].as<std::vector<std::string>>())
				data->addFile("replicate-" + std::to_string(nr++), replicate);
			break;
		}

		case ScreenType::Unspecified:
			throw std::runtime_error("Unknown screen type");
			break;
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

		transcripts = loadTranscripts(assembly, mode, vm["start"].as<std::string>(), vm["end"].as<std::string>(), true);
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

#warning "make options"
	float pvCutOff = 0.05f;
	float binom_fdrCutOff = 0.05f;
	float effectSize = 0.2f;

	// -----------------------------------------------------------------------

	std::cout << "gene" << '\t'
			  << "sense" << '\t'
			  << "antisense" << '\t'
			  << "binom_fdr" << '\t'
			  << "sense_normalized" << '\t'
			  << "antisense_normalized" << '\t'
			  << "fcpv_control_1 " << '\t'
			  << "pv_control_1" << '\t'
			  << "fcpv_control_2" << '\t'
			  << "pv_control_2" << '\t'
			  << "fcpv_control_3" << '\t'
			  << "pv_control_3" << '\t'
			  << "fcpv_control_4" << '\t'
			  << "pv_control_4" << '\t'
			  << "sense + antisense" << '\t'
			  << "(sense_normalized + 1) / (sense_normalized + antisense_normalized + 2)"
			  << std::endl;

	auto r = screenData.dataPoints(assembly, trimLength, transcripts, controlData, groupSize, pvCutOff, binom_fdrCutOff, effectSize);

	if (replicate > r.replicate.size())
		throw std::runtime_error("replicate number too high");

	bool significantOnly = vm.count("significant");

	for (auto& dp: r.replicate[replicate - 1].data)
	{
		if (significantOnly and not r.significant.count(dp.gene))
			continue;

		std::cout << dp.gene << '\t'
				  << dp.sense << '\t'
				  << dp.antisense << '\t'
				  << dp.binom_fdr << '\t'
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
				  << ((dp.sense_normalized + 1.0f) / (dp.sense_normalized + dp.antisense_normalized + 2.0f))
				  << std::endl;
	}

	return 0;
}

int main_analyze(int argc, char* const argv[])
{
	int result = 0;

	auto vm = load_options(argc, argv, PACKAGE_NAME R"( analyze screen-name assembly [options])",
		{
			{ "mode",		po::value<std::string>()->default_value("longest-exon"),	"Mode, should be either collapse, longest-exon or longest-transcript" },
			{ "start",		po::value<std::string>()->default_value("tx"),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "end",		po::value<std::string>()->default_value("cds"),	"cds or tx with optional offset (e.g. +100 or -500)" },
			{ "overlap",	po::value<std::string>(),	"Supported values are both or neither." },
			{ "direction",	po::value<std::string>(),	"Direction for the counted integrations, can be 'sense', 'antisense' or 'both'" },

			{ "replicate",	po::value<unsigned short>(),"The replicate to use, in case of synthetic lethal"},
			{ "group-size",	po::value<unsigned short>(),"The group size to use for normalizing insertions counts in SL, default is 500"},
			{ "significant", new po::untyped_value(true),	"The significant genes only, in case of synthetic lethal"},

			{ "gene-bed-file", po::value<std::string>(),	"Optionally provide a gene BED file instead of calculating one" },
			
			{ "output,o",	po::value<std::string>(),	"Output file" }
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
// Write out the resulting genes as a BED file

int main_refseq(int argc, char* const argv[])
{
	int result = 0;

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
		}, { "assembly" });

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

	return result;

	return 0;
}
