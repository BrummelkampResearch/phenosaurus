/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bowtie.hpp"
#include "db-connection.hpp"
#include "screen-data.hpp"
#include "screen-server.hpp"
#include "user-service.hpp"
#include "utils.hpp"

#include "revision.hpp"

#include <zeep/crypto.hpp>
#include <zeep/http/daemon.hpp>

#include <mcfp.hpp>
#include <gxrio.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace zh = zeep::http;

using namespace std::literals;

int VERBOSE = 0;

// --------------------------------------------------------------------

// recursively print exception whats:
void print_what(const std::exception &e)
{
	std::cerr << e.what() << std::endl;
	try
	{
		std::rethrow_if_nested(e);
	}
	catch (const std::exception &nested)
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

template <typename... Options>
mcfp::config &init_config(int argc, char *const argv[], const char *msg, Options... opts)
{
	auto &config = mcfp::config::instance();

	config.init(msg,

		mcfp::make_option("help,h", "Display help message"),
		mcfp::make_option("version", "Print version"),
		mcfp::make_option("verbose,v", "verbose output"),

		mcfp::make_option<std::string>("config", "Config file to use"),

		mcfp::make_option<std::string>("bowtie", "Bowtie executable"),
		mcfp::make_option<std::string>("assembly", "hg38", "Default assembly to use, currently one of hg19 or hg38"),
		mcfp::make_option<unsigned>("trim-length", "Trim reads to this length, default is 50"),
		mcfp::make_option<unsigned>("threads", "Nr of threads to use"),
		mcfp::make_option<std::string>("screen-dir", "Directory containing the screen data"),
		mcfp::make_option<std::string>("transcripts-dir", "Directory containing the transcript files"),
		mcfp::make_option<std::string>("bowtie-index-hg19", "Bowtie index parameter for HG19"),
		mcfp::make_option<std::string>("bowtie-index-hg38", "Bowtie index parameter for HG38"),
		mcfp::make_option<std::string>("control", "Name of the screen that contains the four control data replicates for synthetic lethal analysis"),
		mcfp::make_option<std::string>("db-host", "Database host"),
		mcfp::make_option<std::string>("db-port", "5432", "Database port"),
		mcfp::make_option<std::string>("db-dbname", "Database name"),
		mcfp::make_option<std::string>("db-user", "Database user name"),
		mcfp::make_option<std::string>("db-password", "Database password"),
		mcfp::make_option<std::string>("address", "localhost", "External address"),
		mcfp::make_option<uint16_t>("port", 10336, "Port to listen to"),
		mcfp::make_option("no-daemon,F", "Do not fork into background"),
		mcfp::make_option<std::string>("user,u", "www-data", "User to run the daemon"),
		mcfp::make_option<std::string>("secret", "Secret hashed used in user authentication"),
		mcfp::make_option<std::string>("context", "Context name of this server (used e.g. in a reverse proxy setup)"),
		mcfp::make_option<std::string>("smtp-server", "SMTP server address for sending out new passwords"),
		mcfp::make_option<uint16_t>("smtp-port", "SMTP server port for sending out new passwords"),
		mcfp::make_option<std::string>("smtp-user", "SMTP server user name for sending out new passwords"),
		mcfp::make_option<std::string>("smtp-password", "SMTP server password name for sending out new passwords"),
		mcfp::make_option("public", "Public version (limited functionality)"),
		opts...);

	std::error_code ec;

	config.parse(argc, argv, ec);
	if (ec)
	{
		std::cerr << "Error parsing command line arguments: " << ec.message() << std::endl
				  << std::endl
				  << config << std::endl;
		exit(1);
	}

	config.parse_config_file("config", "screen-analyzer.conf", { ".", "/etc" }, ec);
	if (ec)
	{
		std::cerr << "Error parsing config file: " << ec.message() << std::endl;
		exit(1);
	}

	// --------------------------------------------------------------------

	if (config.has("version"))
	{
		write_version_string(std::cout, config.has("verbose"));
		exit(0);
	}

	if (config.has("help"))
	{
		std::cout << config << std::endl;
		exit(0);
	}

	VERBOSE = config.count("verbose");

	return config;
}

// --------------------------------------------------------------------

int main_map(int argc, char *const argv[])
{
	int result = 0;

	auto &config = init_config(argc, argv, "screen-analyzer"
										   R"( map screen-name [options])",
		mcfp::make_option<std::string>("bowtie-index", "Bowtie index filename stem for the assembly"));

	if (config.operands().size() != 1)
	{
		std::cerr << "Missing screen-name command line argument" << std::endl
				  << std::endl
				  << config << std::endl;
		exit(1);
	}

	fs::path screenDir = config.get("screen-dir");
	screenDir /= config.operands().front();

	auto data = ScreenData::load(screenDir);

	if (not config.has("bowtie"))
		throw std::runtime_error("Bowtie executable not specified");
	fs::path bowtie = config.get("bowtie");

	std::string assembly = config.get("assembly");

	fs::path bowtieIndex;
	if (config.has("bowtie-index"))
		bowtieIndex = config.get("bowtie-index");
	else
	{
		if (not config.has("bowtie-index-" + assembly))
			throw std::runtime_error("Bowtie index for assembly " + assembly + " not known and bowtie-index parameter not specified");
		bowtieIndex = config.get("bowtie-index-" + assembly);
	}

	unsigned trimLength = 50;
	if (config.has("trim-length"))
		trimLength = config.get<unsigned>("trim-length");

	unsigned threads = 1;
	if (config.has("threads"))
		threads = config.get<unsigned>("threads");

	data->map(assembly, trimLength, bowtie, bowtieIndex, threads);

	return result;
}

// --------------------------------------------------------------------

int analyze_ip(IPPAScreenData &screenData)
{
	auto &config = mcfp::config::instance();

	if (not config.has("start") or not config.count("end") or
		(config.has("overlap") and config.get("overlap") != "both" and config.get("overlap") != "neither"))
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

)" << std::endl;
		exit(config.has("help") ? 0 : 1);
	}

	std::string assembly = config.get("assembly");

	unsigned trimLength = 0;
	if (config.has("trim-length"))
		trimLength = config.get<unsigned>("trim-length");

	// -----------------------------------------------------------------------

	bool cutOverlap = true;
	if (config.has("overlap") and config.get("overlap") == "both")
		cutOverlap = false;

	Mode mode = zeep::value_serializer<Mode>::from_string(config.get("mode"));

	auto transcripts = loadTranscripts(assembly, "default", mode, config.get("start"), config.get("end"), cutOverlap);

	// -----------------------------------------------------------------------

	std::vector<Insertions> lowInsertions, highInsertions;

	screenData.analyze(assembly, trimLength, transcripts, lowInsertions, highInsertions);

	long lowSenseCount = 0, lowAntiSenseCount = 0;
	for (auto &i : lowInsertions)
	{
		lowSenseCount += i.sense.size();
		lowAntiSenseCount += i.antiSense.size();
	}

	long highSenseCount = 0, highAntiSenseCount = 0;
	for (auto &i : highInsertions)
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
	if (config.has("direction"))
	{
		if (config.get("direction") == "sense")
			direction = Direction::Sense;
		else if (config.get("direction") == "antisense" or config.get("direction") == "anti-sense")
			direction = Direction::AntiSense;
		else if (config.get("direction") == "both")
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

	for (auto &dp : screenData.dataPoints(transcripts, lowInsertions, highInsertions, direction))
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

int analyze_sl(SLScreenData &screenData, SLScreenData &controlData)
{
	auto &config = mcfp::config::instance();

	if (not (config.has("control") and
		((config.has("start") and config.has("end")) or config.has("gene-bed-file"))))
	{
		std::cerr << R"(
Start and end should be either 'cds' or 'tx' with an optional offset 
appended. Optionally you can also specify cdsStart, cdsEnd, txStart
or txEnd to have the start at the cdsEnd e.g.
)" << std::endl;
		exit(config.count("help") ? 0 : 1);
	}

	std::string assembly = config.get("assembly");

	unsigned trimLength = 0;
	if (config.has("trim-length"))
		trimLength = config.get<unsigned>("trim-length");

	// -----------------------------------------------------------------------

	std::vector<Transcript> transcripts;

	if (config.has("gene-bed-file"))
		transcripts = loadTranscripts(config.get("gene-bed-file"));
	else
	{
		Mode mode = zeep::value_serializer<Mode>::from_string(config.get("mode"));

		bool cutOverlap = true;
		if (config.has("overlap") and config.get("overlap") == "both")
			cutOverlap = false;

		transcripts = loadTranscripts(assembly, "default", mode, config.get("start"), config.get("end"), cutOverlap);
		filterOutExons(transcripts);
	}

	// reorder transcripts based on chr > end-position, makes code easier and faster
	std::sort(transcripts.begin(), transcripts.end(), [](auto &a, auto &b)
		{
		int d = a.chrom - b.chrom;
		if (d == 0)
			d = a.start() - b.start();
		return d < 0; });

	// --------------------------------------------------------------------

	unsigned groupSize = config.get<unsigned short>("group-size");

	float pvCutOff = config.get<float>("pv-cut-off");
	float binom_fdrCutOff = config.get<float>("binom-fdr-cut-off");
	float oddsRatio = config.get<float>("odds-ratio");
	if (oddsRatio < 0 or oddsRatio >= 1)
		throw std::runtime_error("Odds ratio should be between 0 and 1");

	// -----------------------------------------------------------------------

	auto r = screenData.dataPoints(assembly, trimLength, transcripts, controlData, groupSize);
	bool significantOnly = config.count("significant");

	if (not config.has("no-header"))
	{
		std::cout
			<< "gene" << '\t'
			<< "odds_ratio" << '\t';

		for (size_t i = 0; i < (r.empty() ? 0 : r[0].replicates.size()); ++i)
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

	for (auto &dp : r)
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

int main_analyze(int argc, char *const argv[])
{
	int result = 0;

	auto &config = init_config(argc, argv, "screen-analyzer"
										   R"( analyze screen-name [outputfile] [options])",

		mcfp::make_option<std::string>("mode", "longest-exon", "Mode, should be either collapse, longest-exon or longest-transcript"),
		mcfp::make_option<std::string>("start", "tx", "cds or tx with optional offset (e.g. +100 or -500)"),
		mcfp::make_option<std::string>("end", "cds", "cds or tx with optional offset (e.g. +100 or -500)"),
		mcfp::make_option<std::string>("overlap", "Supported values are both or neither."),
		mcfp::make_option<std::string>("direction", "Direction for the counted integrations, can be 'sense', 'antisense' or 'both'"),

		mcfp::make_option<unsigned short>("group-size", 500, "The group size to use for normalizing insertions counts in SL"),
		mcfp::make_option("significant", "The significant genes only, in case of synthetic lethal"),

		mcfp::make_option<float>("pv-cut-off", 0.05f, "P-value cut off"),
		mcfp::make_option<float>("binom-fdr-cut-off", 0.05f, "binom FDR cut off"),
		mcfp::make_option<float>("odds-ratio", 0.8f, "Odds Ratio"),

		mcfp::make_option<std::string>("gene-bed-file", "Optionally provide a gene BED file instead of calculating one"),

		mcfp::make_option<std::string>("refseq", "Alternative refseq file to use"),

		mcfp::make_option("no-header", "Do not print a header line"));

	if (config.operands().size() != 1 and config.operands().size() != 2)
	{
		std::cerr << "Missing screen-name command line argument" << std::endl
				  << std::endl
				  << config << std::endl;
		exit(1);
	}

	// fail early
	std::ofstream out;
	if (config.operands().size() == 2)
	{
		out.open(config.operands().back());
		if (not out.is_open())
			throw std::runtime_error("Could not open output file");
	}

	std::streambuf *sb = nullptr;
	if (out.is_open())
		sb = std::cout.rdbuf(out.rdbuf());

	// Load refseq, if specified
	if (config.has("refseq"))
		init_refseq(config.get("refseq"));

	fs::path screenDir = config.get("screen-dir");

	auto data = ScreenData::load(screenDir / config.operands().front());

	switch (data->get_type())
	{
		case ScreenType::IntracellularPhenotype:
			result = analyze_ip(*static_cast<IPScreenData *>(data.get()));
			break;

		case ScreenType::IntracellularPhenotypeActivation:
			result = analyze_ip(*static_cast<PAScreenData *>(data.get()));
			break;

		case ScreenType::SyntheticLethal:
		{
			SLScreenData controlScreen(screenDir / config.get("control"));
			result = analyze_sl(*static_cast<SLScreenData *>(data.get()), controlScreen);
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

int main_server(int argc, char *const argv[])
{
	int result = 0;

	auto &config = init_config(argc, argv, "screen-analyzer"
										   R"( command [options])");

	// --------------------------------------------------------------------

	if (config.operands().empty())
	{
		std::cerr << R"(
Command should be either:

  start     start a new server
  stop      start a running server
  status    get the status of a running server
  reload    restart a running server with new options
			 )" << std::endl;
		exit(config.count("help") ? 0 : 1);
	}

	std::string command = config.operands().front();

	// --------------------------------------------------------------------

	std::vector<std::string> vConn;
	for (std::string opt : { "db-host", "db-port", "db-dbname", "db-user", "db-password" })
	{
		if (not config.has(opt))
			continue;

		vConn.push_back(opt.substr(3) + "=" + config.get(opt));
	}

	db_connection::init(zeep::join(vConn, " "));

	// --------------------------------------------------------------------

	std::string smtpServer = config.get("smtp-server");
	uint16_t smtpPort = config.get<uint16_t>("smtp-port");
	std::string smtpUser, smtpPassword;
	if (config.has("smtp-user"))
		smtpUser = config.get("smtp-user");
	if (config.has("smtp-password"))
		smtpPassword = config.get("smtp-password");

	user_service::init(smtpServer, smtpPort, smtpUser, smtpPassword);

	// --------------------------------------------------------------------

	if (not config.has("bowtie"))
		throw std::runtime_error("Bowtie executable not specified");
	fs::path bowtie = config.get("bowtie");

	std::string assembly = config.get("assembly");

	std::map<std::string, fs::path> assemblyIndices;
	for (auto assembly : { "hg19", "hg38" })
	{
		if (not config.has("bowtie-index-"s + assembly))
			continue;
		assemblyIndices[assembly] = config.get("bowtie-index-"s + assembly);
	}

	unsigned trimLength = 50;
	if (config.has("trim-length"))
		trimLength = config.get<unsigned>("trim-length");

	unsigned threads = std::thread::hardware_concurrency();
	if (config.has("threads"))
		threads = config.get<unsigned>("threads");

	bowtie_parameters::init(bowtie, threads, trimLength, assembly, assemblyIndices);

	// --------------------------------------------------------------------

	fs::path docroot;

// #ifndef NDEBUG
// 	char exePath[PATH_MAX + 1];
// 	int r = readlink("/proc/self/exe", exePath, PATH_MAX);
// 	if (r > 0)
// 	{
// 		exePath[r] = 0;
// 		docroot = fs::weakly_canonical(exePath).parent_path() / "docroot";
// 	}

// 	if (not fs::exists(docroot))
// 		throw std::runtime_error("Could not locate docroot directory");
// #endif

	std::string secret;
	if (config.has("secret"))
		secret = config.get("secret");
	else
	{
		secret = zeep::encode_base64(zeep::random_hash());
		std::cerr << "starting with created secret " << secret << std::endl;
	}

	std::string context_name;
	if (config.has("context"))
		context_name = config.get("context");

	std::string user = "www-data";
	if (config.has("user"))
		user = config.get("user");

	std::string address = "127.0.0.1";
	if (config.has("address"))
		address = config.get("address");

	uint16_t port = 10338;
	if (config.has("port"))
		port = config.get<uint16_t>("port");

	std::string access_log = "/var/log/screen-analyzer/access";
	std::string error_log = "/var/log/screen-analyzer/error";
	std::string pid_file = "/var/run/screen-analyzer";

	if (config.has("public"))
	{
		access_log += "-public";
		error_log += "-public";
		pid_file += "-public";
	}

	access_log += ".log";
	error_log += ".log";

	// if (not context_name.empty())
	// {
	// 	access_log += "-" + context_name + ".log";
	// 	error_log += "-" + context_name + ".log";
	// }
	// else
	// {
	// 	access_log += ".log";
	// 	error_log += ".log";
	// }

	zh::daemon server([secret, docroot,
						  screenDir = config.get("screen-dir"),
						  transcriptsDir = config.get("transcripts-dir"),
						  is_public = config.has("public"),
						  context_name]()
		{
		if (is_public)
			return createPublicServer(docroot, screenDir, transcriptsDir, context_name);
		else
			return createServer(docroot, screenDir, transcriptsDir, secret, context_name); },
		pid_file, access_log, error_log);

	if (command == "start")
	{
		std::cout << "starting server at http://" << address << ':' << port << '/' << std::endl;

		if (config.has("no-daemon"))
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

int main_refseq(int argc, char *const argv[])
{
	auto &config = init_config(argc, argv, "screen-analyzer"
										   R"( refseq [<outputfile>] [options])",
		mcfp::make_option<std::string>("mode", "longest-transcript", "Mode, should be either collapse, longest-transcript or longest-exon"),
		mcfp::make_option<std::string>("start", "tx", "cds or tx with optional offset (e.g. +100 or -500)"),
		mcfp::make_option<std::string>("end", "cds", "cds or tx with optional offset (e.g. +100 or -500)"),
		mcfp::make_option<std::string>("overlap", "Supported values are both or neither."),
		// mcfp::<std::string>make_option("direction",	(),	"Direction for the counted integrations, can be 'sense', 'antisense' or 'both'" ),
		mcfp::make_option("no-exons", "Leave out exons"),
		mcfp::make_option<std::string>("sort", "Sort result by 'name' or 'position'"));

	if (config.has("start") == 0 or config.has("end") or
		(config.has("overlap") != 0 and config.get("overlap") != "both" and config.get("overlap") != "neither"))
	{
		std::cerr << R"(
Mode longest-transcript means take the longest transcript for each gene,

Mode longest-exon means the longest expression region, which can be
different from the longest-transcript.

Mode collapse means, for each gene take the region between the first 
start and last end.

Overlap: in case of both, all genes will be added, in case of neither
the parts with overlap will be left out.
)" << std::endl;
		exit(config.count("help") ? 0 : 1);
	}

	// fail early
	std::ofstream out;
	if (not config.operands().empty())
	{
		out.open(config.operands().back());
		if (not out.is_open())
			throw std::runtime_error("Could not open output file");
	}

	bool cutOverlap = true;
	if (config.has("overlap") and config.get("overlap") == "both")
		cutOverlap = false;

	std::string assembly = config.get("assembly");

	Mode mode = zeep::value_serializer<Mode>::from_string(config.get("mode"));

	auto transcripts = loadTranscripts(assembly, "default", mode, config.get("start"), config.get("end"), cutOverlap);
	if (config.has("no-exons"))
		filterOutExons(transcripts);

	if (config.has("sort") and config.get("sort") == "name")
		std::sort(transcripts.begin(), transcripts.end(), [](auto &a, auto &b)
			{ return a.name < b.name; });

	std::streambuf *sb = nullptr;
	if (out.is_open())
		sb = std::cout.rdbuf(out.rdbuf());

	for (auto &transcript : transcripts)
	{
		for (auto &range : transcript.ranges)
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

int main_dump(int argc, char *const argv[])
{
	int result = 0;

	auto &config = init_config(argc, argv, "screen-analyzer"
									   R"( dump screen-name file [options])",
			mcfp::make_option<std::string>("file", "The file to dump" ));

	if (config.operands().size() != 2)
	{
		std::cerr << "Missing screen-name command line arguments" << std::endl
				  << std::endl
				  << config << std::endl;
		exit(1);
	}

	fs::path screenDir = config.get("screen-dir");
	screenDir /= config.operands().front();

	auto data = ScreenData::load(screenDir);

	std::string assembly = config.get("assembly");

	unsigned trimLength = 50;
	if (config.has("trim-length"))
		trimLength = config.get<unsigned>("trim-length");

	auto file = config.operands().back();

	data->dump_map(assembly, trimLength, file);

	return result;
}

int main_refresh(int argc, char *const argv[])
{
	int result = 0;

	auto &config = init_config(argc, argv, "screen-analyzer refresh screen-name");

	if (config.operands().size() != 1)
	{
		std::cerr << "Missing screen-name command line argument" << std::endl
				  << std::endl
				  << config << std::endl;
		exit(1);
	}

	fs::path screenDir = config.get("screen-dir");
	screenDir /= config.operands().front();

	auto info = ScreenData::loadManifest(screenDir);
	ScreenData::refreshManifest(info, screenDir);

	return result;
}

// --------------------------------------------------------------------

int main(int argc, char *const argv[])
{
	int result = 0;

	std::set_terminate([]()
		{
		std::cerr << "Unhandled exception" << std::endl;
		std::abort(); });

	// initialize enums

	zeep::value_serializer<ScreenType>::init("screen-type", { { ScreenType::IntracellularPhenotype, "ip" },
																{ ScreenType::IntracellularPhenotypeActivation, "pa" },
																{ ScreenType::SyntheticLethal, "sl" } });

	zeep::value_serializer<Mode>::init("mode", { { Mode::Collapse, "collapse" },
												   { Mode::LongestTranscript, "longest-transcript" },
												   { Mode::LongestExon, "longest-exon" } });

	zeep::value_serializer<Direction>::init("direction", { { Direction::Sense, "sense" },
															 { Direction::AntiSense, "antisense" },
															 { Direction::Both, "both" } });

	zeep::value_serializer<CHROM>::init({ { INVALID, "unk" },
		{ CHR_1, "chr1" },
		{ CHR_2, "chr2" },
		{ CHR_3, "chr3" },
		{ CHR_4, "chr4" },
		{ CHR_5, "chr5" },
		{ CHR_6, "chr6" },
		{ CHR_7, "chr7" },
		{ CHR_8, "chr8" },
		{ CHR_9, "chr9" },
		{ CHR_10, "chr10" },
		{ CHR_11, "chr11" },
		{ CHR_12, "chr12" },
		{ CHR_13, "chr13" },
		{ CHR_14, "chr14" },
		{ CHR_15, "chr15" },
		{ CHR_16, "chr16" },
		{ CHR_17, "chr17" },
		{ CHR_18, "chr18" },
		{ CHR_19, "chr19" },
		{ CHR_20, "chr20" },
		{ CHR_21, "chr21" },
		{ CHR_22, "chr22" },
		{ CHR_23, "chr23" },
		{ CHR_X, "chrX" },
		{ CHR_Y, "chrY" } });

	try
	{
		if (argc < 2)
		{
			usage();
			exit(-1);
		}

		std::string command = argv[1];
		if (command == "map")
			result = main_map(argc - 1, argv + 1);
		else if (command == "analyze")
			result = main_analyze(argc - 1, argv + 1);
		else if (command == "refseq")
			result = main_refseq(argc - 1, argv + 1);
		else if (command == "server")
			result = main_server(argc - 1, argv + 1);
		else if (command == "refresh")
			result = main_refresh(argc - 1, argv + 1);
		else if (command == "dump")
			result = main_dump(argc - 1, argv + 1);
		else if (command == "test")
		{

			gxrio::ifstream file("/srv/data/raw_data/4486_1_HAP1_IR_screen_FF_NoIndex_S33_L005_R1_001.fastq.gz");

			if (not file.is_open())
				throw std::runtime_error("Could not open file");

			size_t n = 0;
			std::string line;

			while (getline(file, line))
				++n;

			if (file.bad())
				std::cout << "File is bad" << std::endl;

			std::cout << "line count: " << n << std::endl;
		}
		else if (command == "help" or command == "--help" or command == "-h" or command == "-?")
			usage();
		else if (command == "version" or command == "-v" or command == "--version")
			write_version_string(std::cout, false);
		else
			result = usage();
	}
	catch (const std::exception &ex)
	{
		std::cerr << std::endl
				  << "Fatal exception" << std::endl;

		print_what(ex);
		result = 1;
	}

	return result;
}
