#include <iostream>
#include <fstream>
#include <regex>
#include <filesystem>

#include <boost/program_options.hpp>

#include "refann.hpp"
#include "fisher.hpp"
#include "bowtie.hpp"

#include "mrsrc.h"

namespace po = boost::program_options;
namespace fs = std::filesystem;

#define APP_NAME "adjust"

int VERBOSE = 0;

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

// --------------------------------------------------------------------

int main(int argc, const char* argv[])
{
	int result = 0;

	po::options_description visible_options(APP_NAME R"( [options] --mode=[mode] --start=[startpos] --end=[endpos] inputfile [outputfile])");
	visible_options.add_options()
		("help,h",								"Display help message")
		("version",								"Print version")

		("reference", po::value<std::string>(),	"Reference gene file")

		("mode", po::value<std::string>(),		"Mode, should be either collapse, longest")

		("start", po::value<std::string>(),		"cds or tx with optional offset (e.g. +100 or -500)")
		("end", po::value<std::string>(),		"cds or tx with optional offset (e.g. +100 or -500)")

		("overlap", po::value<std::string>(),	"Supported values are both or neither.")

		("low", po::value<std::string>(),		"FastQ file containing the hits for the low scoring cells")
		("high", po::value<std::string>(),		"FastQ file containing the hits for the high scoring cells")

		("bowtie", po::value<std::string>(), 	"The bowtie executable to use")
		("bowtie-index", po::value<std::string>(),
												"Stem of the filenames for the bowtie index")

		("verbose,v",							"Verbose output")
		;

	po::options_description hidden_options("hidden options");
	hidden_options.add_options()
		("output,o", po::value<std::string>(),	"Output file")
		("debug,d", po::value<int>(),			"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible_options).add(hidden_options);

	po::positional_options_description p;
	p.add("output", 1);
	
	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	const std::regex kPosRx(R"((cds|tx)((?:\+|-)[0-9]+)?)");

	fs::path configFile = APP_NAME ".conf";
	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / APP_NAME ".conf";
	
	if (fs::exists(configFile))
	{
		std::ifstream cfgFile(configFile);
		if (cfgFile.is_open())
			po::store(po::parse_config_file(cfgFile, visible_options), vm);
	}
	
	po::notify(vm);

	if (vm.count("version"))
	{
		showVersionInfo();
		exit(0);
	}

	if (vm.count("help") or vm.count("reference") == 0 or //vm.count("low") == 0 or vm.count("high") == 0 or
		vm.count("mode") == 0 or (vm["mode"].as<std::string>() != "collapse" and vm["mode"].as<std::string>() != "longest") or
		vm.count("start") == 0 or vm.count("end") == 0 or
		(vm.count("overlap") != 0 and vm["overlap"].as<std::string>() != "both" and vm["overlap"].as<std::string>() != "neither"))
	{
		std::cerr << visible_options << std::endl
				  << R"(
Mode longest means take the longest transcript for each gene

Mode collapse means, for each gene take the region between the first 
start and last end.

Start and end should be either 'cds' or 'tx' with an optional offset 
appended.

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

	Mode mode;
	if (vm["mode"].as<std::string>() == "collapse")
		mode = Mode::Collapse;
	else // if (vm["mode"].as<std::string>() == "longest")
		mode = Mode::Longest;

	VERBOSE = vm.count("verbose") != 0;
	if (vm.count("debug"))
		VERBOSE = vm["debug"].as<int>();

	bool cutOverlap = true;
	if (vm.count("overlapped") and vm["overlapped"].as<std::string>() == "both")
		cutOverlap = false;

	auto transcripts = loadTranscripts(vm["reference"].as<std::string>(),
		mode, vm["start"].as<std::string>(), vm["end"].as<std::string>(),
		cutOverlap);

	if (vm.count("low"))
	{
		std::ifstream in(vm["low"].as<std::string>());
		if (in.is_open())
		{
			auto ins = assignInsertions(in, transcripts);

			std::cout << "hits: " << ins.size() << std::endl;
		}
	}
	else
	{
		auto ins = assignInsertions(std::cin, transcripts);
		std::cout << "hits: " << ins.size() << std::endl;
	}

	return result;
}