// copyright 2020 M.L. Hekkelman, NKI/AVL

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

#include "mrsrc.h"

namespace po = boost::program_options;
namespace fs = std::filesystem;
namespace io = boost::iostreams;
using namespace std::literals;

#define APP_NAME "adjust"

int VERBOSE = 0;

// -----------------------------------------------------------------------

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

		("threads", po::value<size_t>(),		"Nr of threads")

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

	// fail early
	std::ofstream out;
	if (vm.count("output"))
	{
		out.open(vm["output"].as<std::string>());
		if (not out.is_open())
			throw std::runtime_error("Could not open output file");
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

	std::vector<Insertions> lowInsertions, highInsertions;

	for (std::string lh: { "low", "high" })
	{
		if (not vm.count(lh))
			throw std::runtime_error("Missing "s + lh + " parameter");

		auto infile = vm[lh].as<std::string>();

		std::vector<Insertions> insertions;

		if (infile.find(".bwt") != std::string::npos)	// bowtie output file
		{
			fs::path p = infile;
			std::ifstream file(p, std::ios::binary);

			if (not file.is_open())
				throw std::runtime_error("Could not open file " + infile);

			io::filtering_stream<io::input> in;
			std::string ext = p.extension().string();
			
			if (p.extension() == ".bz2")
			{
				in.push(io::bzip2_decompressor());
				ext = p.stem().extension().string();
			}
			else if (p.extension() == ".gz")
			{
				in.push(io::gzip_decompressor());
				ext = p.stem().extension().string();
			}
			
			in.push(file);

			insertions = assignInsertions(in, transcripts);
		}
		else if (infile.find(".fastq") != std::string::npos or vm.count("bowtie"))
		{
			std::string bowtie("/usr/bin/bowtie");
			if (vm.count("bowtie"))
				bowtie = vm["bowtie"].as<std::string>();

			size_t proc = 4;
			if (vm.count("threads"))
				proc = vm["threads"].as<size_t>();
			if (proc < 1)
				proc = 1;

			insertions = assignInsertions(bowtie, vm["bowtie-index"].as<std::string>(), infile, transcripts, proc);
		}
		
		if (lh == "low")
			std::swap(insertions, lowInsertions);
		else
			std::swap(insertions, highInsertions);
	}

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

	std::vector<double> pvalues(transcripts.size(), 0);

	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		long low = lowInsertions[i].sense.size();
		long high = highInsertions[i].sense.size();
	
		long v[2][2] = {
			{ low, high },
			{ lowSenseCount - low, highSenseCount - high }
		};

		pvalues[i] = fisherTest2x2(v);
	}

	auto fcpv = adjustFDR_BH(pvalues);

	std::streambuf* sb = nullptr;
	if (out.is_open())
		sb = std::cout.rdbuf(out.rdbuf());

	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		auto& t = transcripts[i];
		auto low = lowInsertions[i].sense.size();
		auto high = highInsertions[i].sense.size();

		double miL = low, miH = high, miLT = lowSenseCount - low, miHT = highSenseCount - high;
		if (low == 0)
		{
			miL = 1;
			miLT -= 1;
		}

		if (high == 0)
		{
			miH = 1;
			miHT -= 1;
		}

		double mi = ((miH / miHT) / (miL / miLT));

		std::cout << t.geneName << '\t'
				  << low << '\t'
				  << high << '\t'
				  << pvalues[i] << '\t'
				  << fcpv[i] << '\t'
				  << std::log2(mi) << std::endl;
	}

	if (sb)
		std::cout.rdbuf(sb);

	return result;
}