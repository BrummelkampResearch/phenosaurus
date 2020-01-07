#include <iostream>
#include <sstream>
#include <regex>
#include <numeric>
// #include <filesystem>

#include "mrsrc.h"

#include <boost/program_options.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------

int VERBOSE;

enum class Mode
{
	Collapse, Longest
};

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
	INVALID, CHR_1, CHR_2, CHR_3, CHR_4, CHR_5, CHR_6, CHR_7, CHR_8, CHR_9, CHR_10,
	CHR_11, CHR_12, CHR_13, CHR_14, CHR_15, CHR_16, CHR_17, CHR_18, CHR_19, CHR_20,
	CHR_21, CHR_22, CHR_23, CHR_X, CHR_Y
};

std::ostream& operator<<(std::ostream& os, CHROM chr)
{
	switch (chr)
	{
		case INVALID: os << "invalid"; break;
		case CHR_X: os << "chrX"; break;
		case CHR_Y: os << "chrY"; break;
		default:
			os << "chr" << std::to_string(static_cast<int>(chr));
			break;
	}
	return os;
}

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

	// used by algorithms
	bool longest = false;
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
					{
						// strip underscores...
						ts.geneName = f;

						auto b = ts.geneName.begin();
						for (auto a = b; a != ts.geneName.end(); ++a)
							if (*a != '_') *b++ = *a;
						
						if (b != ts.geneName.end())
						{
							ts.geneName.erase(b, ts.geneName.end());
							if (VERBOSE)
								std::cerr << "Replacing gene name " << f << " with " << ts.geneName << std::endl;
						}

						break;
					}
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

// --------------------------------------------------------------------

void onlyCompleteAndNoReadThrough(std::vector<Transcript>& transcripts)
{
	// First remove all incomplete transcripts
	transcripts.erase(
		std::remove_if(transcripts.begin(), transcripts.end(), [](auto& t) { return t.cds.stat != CDSStat::COMPLETE; }),
		transcripts.end());

	// the unique names
	std::set<std::string> geneNames;
	for (auto& t: transcripts)
		geneNames.insert(t.geneName);

	// Now remove transcripts for read-through proteins
	transcripts.erase(
		std::remove_if(transcripts.begin(), transcripts.end(), [&geneNames](auto& t)
		{
			auto s = t.geneName.find('-');
			bool result = s != std::string::npos and
				(geneNames.count(t.geneName.substr(0, s)) > 0 or geneNames.count(t.geneName.substr(s + 1)) > 0);

			if (result and VERBOSE)
				std::cerr << "Removing read-through gene " << t.geneName << std::endl;

			return result;
		}),
		transcripts.end());
}

// --------------------------------------------------------------------

void reduceToLongestTranscripts(std::vector<Transcript>& transcripts, uint32_t maxGap, Mode mode)
{
	using std::vector;

	// Now build an index based on gene name and position

	vector<size_t> index(transcripts.size());
	std::iota(index.begin(), index.end(), 0);

	std::sort(index.begin(), index.end(), [&transcripts](size_t ix_a, size_t ix_b)
	{
		auto& a = transcripts[ix_a];
		auto& b = transcripts[ix_b];

		int d = a.geneName.compare(b.geneName);
		if (d == 0)
			d = a.chrom - b.chrom;
		if (d == 0)
			d = a.tx.start - b.tx.start;
		
		return d < 0;
	});

	// rename genes that are found on both strands or multiple chromosomes
	for (size_t i = 0; i + 1 < transcripts.size(); ++i)
	{
		auto ix_a = index[i];
		auto& a = transcripts[ix_a];

		size_t j = i;
		bool rename = false;

		while (j + 1 < transcripts.size())
		{
			auto ix_b = index[j + 1];
			auto& b = transcripts[ix_b];

			if (b.geneName != a.geneName)
				break;

			rename = rename or a.chrom != b.chrom or a.strand != b.strand;
			++j;
		}
		
		if (not rename)
		{
			i = j;
			continue;
		}

		for (size_t k = i; k <= j; ++k)
		{
			auto ix_b = index[k];
			auto& b = transcripts[ix_b];

			std::stringstream s;
			s << b.geneName << '@' << b.chrom << b.strand;
			b.geneName = s.str();
		}

		i = j;
	}

	if (mode == Mode::Longest)
	{
		// Find the longest transcript for each gene
		for (size_t i = 0; i + 1 < transcripts.size(); ++i)
		{
			auto ix_a = index[i];
			auto& a = transcripts[ix_a];

			auto l = ix_a;

			assert(a.tx.end > a.tx.start);

			auto len_a = a.tx.end - a.tx.start;

			for (size_t j = i + 1; j < transcripts.size(); ++j)
			{
				auto ix_b = index[j];
				auto& b = transcripts[ix_b];

				assert(b.tx.end > b.tx.start);

				if (b.chrom != a.chrom or b.geneName != a.geneName or a.strand != b.strand)
					break;

				++i;

				auto len_b = b.tx.end - b.tx.start;
				if (len_b > len_a)
					l = ix_b;
			}

			transcripts[l].longest = true;
		}

		transcripts.erase(
			std::remove_if(transcripts.begin(), transcripts.end(), [](auto& t) { return not t.longest; }),
			transcripts.end()
		);
	}

	// reorder index, now only on position
	index.resize(transcripts.size());
	std::iota(index.begin(), index.end(), 0);

	std::sort(index.begin(), index.end(), [&transcripts](size_t ix_a, size_t ix_b)
	{
		auto& a = transcripts[ix_a];
		auto& b = transcripts[ix_b];

		int d = a.chrom - b.chrom;
		if (d == 0)
			d = a.tx.start - b.tx.start;
		
		return d < 0;
	});

	// designate overlapping genes

	vector<bool> overlapped(transcripts.size(), false);

	for (size_t i = 0; i + 1 < transcripts.size(); ++i)
	{
		auto ix_a = index[i];
		auto& a = transcripts[ix_a];

		for (size_t j = i + 1; j < transcripts.size(); ++j)
		{
			auto ix_b = index[j];
			auto& b = transcripts[ix_b];

			if (b.chrom != a.chrom or b.tx.start > a.tx.end)
				break;

			if (a.tx.start <= b.tx.start and a.tx.end >= b.tx.end)
			{
				if (VERBOSE)
					std::cerr << "Gene " << a.geneName << " overlaps " << b.geneName << std::endl;
				overlapped[ix_b] = true;
			}
			else if (b.tx.start <= a.tx.start and b.tx.end >= a.tx.end)
			{
				if (VERBOSE)
					std::cerr << "Gene " << b.geneName << " overlaps " << a.geneName << std::endl;

				overlapped[ix_a] = true;
			}
		}
	}

	// print the list
	for (size_t i: index)
	{
		auto& t = transcripts[i];

		if (overlapped[i])
			continue;

		// auto& t = transcripts[i];
		std::cout << t.chrom << '\t'
				  << t.tx.start << '\t'
				  << t.tx.end << '\t'
				  << t.geneName << '\t'
				  << 0 << '\t'
				  << t.strand << std::endl;
	}


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

		("mode", po::value<std::string>(),		"Mode, should be either collapse or longest")

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

	if (vm.count("help") or vm.count("genes") == 0 or vm.count("mode") == 0)
	{
		std::cerr << visible_options << std::endl;
		exit(1);
	}

	Mode mode;
	if (vm["mode"].as<std::string>() == "collapse")
		mode = Mode::Collapse;
	else if (vm["mode"].as<std::string>() == "longest")
		mode = Mode::Longest;
	else
	{
		std::cerr << visible_options << std::endl;
		exit(1);
	}

	VERBOSE = vm.count("verbose") != 0;
	if (vm.count("debug"))
		VERBOSE = vm["debug"].as<int>();

	auto transcripts = loadGenes(vm["genes"].as<std::string>());

	onlyCompleteAndNoReadThrough(transcripts);

	// Find longest transcripts
	reduceToLongestTranscripts(transcripts, 0, mode);

	if (VERBOSE)
		std::cerr << "Loaded " << transcripts.size() << " genes" << std::endl;

	return result;
}
