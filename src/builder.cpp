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

// -----------------------------------------------------------------------

struct Range
{
	uint32_t start, end;
};

enum class CDSStat : uint8_t
{
	NONE, UNKNOWN, INCOMPLETE, COMPLETE
};

struct CDS : public Range
{
	CDSStat stat;
};

struct Exon : public Range
{
	int8_t frame;
};

enum CHROM : int8_t
{
	INVALID,
	CHR_1,
	CHR_2,
	CHR_3,
	CHR_4,
	CHR_5,
	CHR_6,
	CHR_7,
	CHR_8,
	CHR_9,
	CHR_10,
	CHR_11,
	CHR_12,
	CHR_13,
	CHR_14,
	CHR_15,
	CHR_16,
	CHR_17,
	CHR_18,
	CHR_19,
	CHR_20,
	CHR_21,
	CHR_22,
	CHR_23,
	CHR_X,
	CHR_Y
};

struct Transcript
{
	std::string	name;
	CHROM chrom;
	char strand;
	CDS cds;
	Range tx;
	std::vector<Exon> exons;
	float score;
	std::string geneName;
};

struct splitted_range
{
	splitted_range(const std::string& s, char delim)
		: m_s(s), m_d(delim) {}
	
	struct iterator
	{
		using iterator_category = std::forward_iterator_tag;
		using value_type = std::string;
		using difference_type = std::string::size_type;
		using pointer = std::string*;
		using reference = std::string&;

		iterator(const std::string& s, char delim, std::string::size_type pos)
			: m_s(s), m_d(delim), m_pos(pos), m_end(std::string::npos)
		{
			if (m_pos != std::string::npos)
			{
				m_end = m_s.find(m_d);
				if (m_end == std::string::npos)
					m_sv = m_s.substr(m_pos);
				else
					m_sv = m_s.substr(m_pos, m_end - m_pos);
			}
		}

		iterator(const iterator& i) = default;
		iterator& operator=(const iterator& i) = default;

		pointer operator->() { return &m_sv; }
		reference operator*() { return m_sv; }

		bool operator!=(const iterator& rhs) const
		{
			return m_pos != rhs.m_pos;
		}

		bool operator==(const iterator& rhs) const
		{
			return m_pos == rhs.m_pos;
		}

		iterator& operator++()
		{
			if (m_end == std::string::npos)
			{
				m_sv = {};
				m_pos = m_end;
			}
			else
			{
				m_pos = m_end + 1;
				m_end = m_s.find(m_d, m_pos);

				if (m_end == std::string::npos)
					m_sv = m_s.substr(m_pos);
				else
					m_sv = m_s.substr(m_pos, m_end - m_pos);
			}

			return *this;
		}

		iterator operator++(int)
		{
			iterator tmp(*this);
			operator++();
			return tmp;
		}

	  private:
		const std::string& m_s;
		std::string m_sv;
		char m_d;
		std::string::size_type m_pos, m_end;
	};

	iterator begin() { return iterator(m_s, m_d, 0); }
	iterator end() { return iterator(m_s, m_d, std::string::npos); }

  private:
	const std::string& m_s;
	char m_d;
};

std::vector<Transcript> loadGenes(const std::string& file)
{
	std::ifstream in(file);

	if (not in.is_open())
		throw std::runtime_error("Could not open gene file");
	
	std::string line;
	if (not std::getline(in, line) or line.empty())
		throw std::runtime_error("Invalid gene file");
	
	std::vector<int> index;

	std::vector<Transcript> transcripts;

	for (auto f: splitted_range(line, '\t'))
	{
			 if (f == "name")			index.push_back(1);
		else if (f == "chrom")			index.push_back(2);
		else if (f == "strand")			index.push_back(3);
		else if (f == "txStart")		index.push_back(4);
		else if (f == "txEnd")			index.push_back(5);
		else if (f == "cdsStart")		index.push_back(6);
		else if (f == "cdsEnd")			index.push_back(7);
		else if (f == "exonCount")		index.push_back(8);
		else if (f == "exonStarts")		index.push_back(9);
		else if (f == "exonEnds")		index.push_back(10);
		else if (f == "score")			index.push_back(11);
		else if (f == "name2")			index.push_back(12);
		else if (f == "cdsStartStat")	index.push_back(13);
		else if (f == "cdsEndStat")		index.push_back(14);
		else if (f == "exonFrames")		index.push_back(15);
		else							index.push_back(-1);
	}

	size_t lineNr = 1;
	const std::regex kChromRx(R"(^chr([1-9]|1[0-9]|2[0-3]|X|Y)$)");

	while (std::getline(in, line))
	{
		Transcript ts = {};
		int ix = -1;

		try
		{
			for (auto f: splitted_range(line, '\t'))
			{
				++ix;

				switch (index[ix])
				{
					case 1: // name
						ts.name = f;
						break;
					case 2:	// chrom
					{
						std::smatch m;
						if (std::regex_match(f, m, kChromRx))
						{
							if (m[1] == "X")
								ts.chrom = CHR_X;
							else if (m[1] == "Y")
								ts.chrom = CHR_Y;
							else
								ts.chrom = static_cast<CHROM>(stoi(m[1]));
						}
						break;
					}
					case 3:	// strand
						ts.strand = f[0];
						break;
					case 4:	// txStart
						ts.tx.start = stoul(f);
						break;
					case 5:	// txEnd
						ts.tx.end = stoul(f);
						break;
					case 6:	// cdsStart
						ts.cds.start = stoul(f);
						break;
					case 7:	// cdsEndstd::cerr << e.what() << '\n';
						ts.cds.end = stoul(f);
						break;
					case 8:	// exonCount
						ts.exons.reserve(stoi(f));
						break;
					case 9:	// exonStarts
					case 10:// exonEnds
					case 15:// exonFrames
					{
						size_t ei = 0;
						for (std::string::size_type e = 0, ee = line.find(',', e + 1); e != std::string::npos; e = ee, ee = line.find('\t', e + 1), ++ei)
						{
							if (ts.exons.size() < ei + 1)
								ts.exons.push_back({});
							switch (index[ix])
							{
								case 9:	// exonStarts
									ts.exons[ei].start = stoul(f);
									break;
								case 10:// exonEnds
									ts.exons[ei].end = stoul(f);
									break;
								case 15:// exonFrames
									ts.exons[ei].frame = stoi(f);
									break;
							}
						}
						break;
					}

					case 11:// score
						ts.score = stof(f);
						break;
					case 12:// name2
						ts.geneName = f;
						break;
					case 13:// cdsStartStat
						if (f == "cmpl")
							ts.cds.stat = CDSStat::COMPLETE;
						break;
					case 14:// cdsEndStat
						if (f == "cmpl")
							ts.cds.stat = CDSStat::COMPLETE;
						break;
				}
			}
		}
		catch(const std::exception& ex)
		{
			std::cerr << "Parse error at line " << lineNr << ": " << ex.what() << '\n';
		}
		
		if (ts.chrom == INVALID)
			continue;

		transcripts.push_back(std::move(ts));
	}

	return transcripts;
}

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

// -----------------------------------------------------------------------


int main(int argc, const char* argv[])
{
	int result = 0;

	po::options_description visible_options("reference-annotation-builder [options]" );
	visible_options.add_options()
		("help,h",								"Display help message")
		("version",								"Print version")
		
		("genes", po::value<std::string>(),		"Input gene file")

		("verbose,v",							"Verbose output")
		;

	po::options_description hidden_options("hidden options");
	hidden_options.add_options()
		("debug,d",		po::value<int>(),				"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible_options).add(hidden_options);

	po::positional_options_description p;
	p.add("genes", 1);
	
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

	if (vm.count("help") or vm.count("genes") == 0)
	{
		std::cerr << visible_options << std::endl;
		exit(1);
	}

	VERBOSE = vm.count("verbose") != 0;
	if (vm.count("debug"))
		VERBOSE = vm["debug"].as<int>();


	auto transcripts = loadGenes(vm["genes"].as<std::string>());

	if (VERBOSE)
		std::cout << "Loaded " << transcripts.size() << " genes" << std::endl;

	return result;
}
