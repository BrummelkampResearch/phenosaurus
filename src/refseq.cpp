#include <iostream>
#include <sstream>
#include <regex>
#include <numeric>

#include <boost/program_options.hpp>

#include "mrsrc.h"
#include "refseq.hpp"

namespace po = boost::program_options;

extern int VERBOSE;

// --------------------------------------------------------------------

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

// --------------------------------------------------------------------

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

std::vector<Transcript> loadGenes(const std::string& assembly, bool completeOnly)
{
	mrsrc::rsrc refseq("ncbi-genes-" + assembly + ".txt");

	if (not refseq)
		throw std::runtime_error("Invalid assembly specified, could not find genes");

	struct membuf : public std::streambuf
	{
		membuf(char* data, size_t length)		{ this->setg(data, data, data + length); }
	} buffer(const_cast<char*>(refseq.data()), refseq.size());
	
	std::istream in(&buffer);
	
	std::string line;
	if (not std::getline(in, line) or line.empty())
		throw std::runtime_error("Invalid gene file");
	
	std::vector<int> index;

	std::vector<Transcript> transcripts;
	if (assembly == "hg19")
		transcripts.reserve(70000);
	else if (assembly == "hg38")
		transcripts.reserve(170000);

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
			throw;
		}
		
		if (ts.chrom == INVALID)
			continue;
		
		if (completeOnly and ts.cds.stat != CDSStat::COMPLETE)
			continue;

		// initially we take the whole transcription region
		ts.r = ts.tx;

		transcripts.push_back(std::move(ts));
	}

	return transcripts;
}

// --------------------------------------------------------------------

void selectTranscripts(std::vector<Transcript>& transcripts, uint32_t maxGap, Mode mode)
{
	using std::vector;

	// Start by removing transcripts for which r.end < r.start, yes, that's possible...
	transcripts.erase(
		std::remove_if(transcripts.begin(), transcripts.end(), [](auto& t) { return t.r.end <= t.r.start; }),
		transcripts.end());

	// build an index based on gene name and position

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
			d = a.r.start - b.r.start;
		
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

	switch (mode)
	{
		case Mode::Longest:
			// Find the longest transcript for each gene
			for (size_t i = 0; i + 1 < transcripts.size(); ++i)
			{
				auto ix_a = index[i];
				auto& a = transcripts[ix_a];

				auto l = ix_a;

				assert(a.r.end > a.r.start);

				auto len_a = a.r.end - a.r.start;

				for (size_t j = i + 1; j < transcripts.size(); ++j)
				{
					auto ix_b = index[j];
					auto& b = transcripts[ix_b];

					assert(b.r.end > b.r.start);

					if (b.chrom != a.chrom or b.geneName != a.geneName or a.strand != b.strand)
						break;

					++i;

					auto len_b = b.r.end - b.r.start;
					if (len_b > len_a)
						l = ix_b;
				}

				transcripts[l].longest = true;
			}

			transcripts.erase(
				std::remove_if(transcripts.begin(), transcripts.end(), [](auto& t) { return not t.longest; }),
				transcripts.end()
			);			
			break;
		

		case Mode::Collapse:
			// Find the longest possible span for each gene, i.e. min start - max end
			for (size_t i = 0; i + 1 < transcripts.size(); ++i)
			{
				auto ix_a = index[i];
				auto& a = transcripts[ix_a];
				a.longest = true;

				for (size_t j = i + 1; j < transcripts.size(); ++j)
				{
					auto ix_b = index[j];
					auto& b = transcripts[ix_b];

					if (b.chrom != a.chrom)
						break;

					if (b.geneName != a.geneName or a.strand != b.strand)
						continue;

					++i;

					if (a.r.start > b.r.start)
						a.r.start = b.r.start;
					
					if (a.r.end < b.r.end)
						a.r.end = b.r.end;
				}
			}

			transcripts.erase(
				std::remove_if(transcripts.begin(), transcripts.end(), [](auto& t) { return not t.longest; }),
				transcripts.end()
			);			
			break;

		default:
			break;
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
			d = a.r.start - b.r.start;
		
		return d < 0;
	});

	// designate overlapping genes

	for (size_t i = 0; i + 1 < transcripts.size(); ++i)
	{
		auto ix_a = index[i];
		auto& a = transcripts[ix_a];

		for (size_t j = i + 1; j < transcripts.size(); ++j)
		{
			auto ix_b = index[j];
			auto& b = transcripts[ix_b];

			if (b.chrom != a.chrom or b.r.start > a.r.end)
				break;

			if (a.r.start <= b.r.start and a.r.end >= b.r.end)
			{
				if (VERBOSE)
					std::cerr << "Gene " << a.geneName << " overlaps " << b.geneName << std::endl;
				b.overlapped = true;
			}
			else if (b.r.start <= a.r.start and b.r.end >= a.r.end)
			{
				if (VERBOSE)
					std::cerr << "Gene " << b.geneName << " overlaps " << a.geneName << std::endl;

				a.overlapped = true;
			}
		}
	}
}

// --------------------------------------------------------------------

std::vector<Transcript> loadTranscripts(const std::string& assembly, Mode mode,
	const std::string& startPos, const std::string& endPos, bool cutOverlap)
{
	enum class POS { TX_START, CDS_START, CDS_END, TX_END };

	const std::regex kPosRx(R"((cds|tx)(Start|End)?((?:\+|-)[0-9]+)?)");

	auto transcripts = loadGenes(assembly, true);

	if (VERBOSE)
		std::cerr << "Loaded " << transcripts.size() << " transcripts" << std::endl;

	// reassign start and end

	int64_t startOffset = 0, endOffset = 0;

	std::smatch m;
	if (not regex_match(startPos, m, kPosRx))
		throw std::runtime_error("Invalid start specification");
	
	POS start;
	if (m[1] == "cds")
	{
		if (not m[2].matched or m[2] == "Start")
			start = POS::CDS_START;
		else
			start = POS::CDS_END;
	}
	else
	{
		if (not m[2].matched or m[2] == "Start")
			start = POS::TX_START;
		else
			start = POS::TX_END;
	}
	
	if (m[3].matched)
		startOffset = std::stol(m[3]);
	
	if (not regex_match(endPos, m, kPosRx))
		throw std::runtime_error("Invalid end specification");

	POS end;
	if (m[1] == "cds")
	{
		if (not m[2].matched or m[2] == "End")
			end = POS::CDS_END;
		else
			end = POS::CDS_START;
	}
	else
	{
		if (not m[2].matched or m[2] == "End")
			end = POS::TX_END;
		else
			end = POS::TX_START;
	}
	
	if (m[3].matched)
		endOffset = std::stol(m[3]);

	for (auto& t: transcripts)
	{
		if (t.strand == '+')
		{
			switch (start)
			{
				case POS::TX_START:		t.r.start = t.tx.start + startOffset; break;
				case POS::CDS_START:	t.r.start = t.cds.start + startOffset; break;
				case POS::CDS_END:		t.r.start = t.cds.end + startOffset; break;
				case POS::TX_END:		t.r.start = t.tx.end + startOffset; break;
			}

			switch (end)
			{
				case POS::TX_START:		t.r.end = t.tx.start + endOffset; break;
				case POS::CDS_START:	t.r.end = t.cds.start + endOffset; break;
				case POS::CDS_END:		t.r.end = t.cds.end + endOffset; break;
				case POS::TX_END:		t.r.end = t.tx.end + endOffset; break;
			}
		}
		else
		{
			switch (start)
			{
				case POS::TX_START:		t.r.end = t.tx.end - startOffset; break;
				case POS::CDS_START:	t.r.end = t.cds.end - startOffset; break;
				case POS::CDS_END:		t.r.end = t.cds.start - startOffset; break;
				case POS::TX_END:		t.r.end = t.tx.start - startOffset; break;
			}

			switch (end)
			{
				case POS::TX_START:		t.r.start = t.tx.end - endOffset; break;
				case POS::CDS_START:	t.r.start = t.cds.end - endOffset; break;
				case POS::CDS_END:		t.r.start = t.cds.start - endOffset; break;
				case POS::TX_END:		t.r.start = t.tx.start - endOffset; break;
			}
		}
	}

	// Find longest or collapsed transcripts
	selectTranscripts(transcripts, 0, mode);

	auto cmp = [](auto& a, auto& b)
	{
		int d = a.chrom - b.chrom;
		if (d == 0)
			d = a.r.start - b.r.start;
		return d < 0;
	};

	// reorder transcripts
	std::sort(transcripts.begin(), transcripts.end(), cmp);

	// cut out overlapping regions, if requested
	if (cutOverlap)
	{
		for (size_t i = 0; i + 1 < transcripts.size(); ++i)
		{
			size_t j = i + 1;

			if (transcripts[i].chrom != transcripts[j].chrom)
				continue;
			
			if (transcripts[i].r.end <= transcripts[j].r.start)
				continue;
			
			auto e = transcripts[i].r.end;
			transcripts[i].r.end = transcripts[j].r.start;

			// check to see if first overlaps second completely with extra
			if (e > transcripts[j].r.end)
			{
				// need to insert copy

				auto t = transcripts[i];
				t.r.start = transcripts[j].r.end;
				t.r.end = e;

				auto k = std::upper_bound(transcripts.begin() + i, transcripts.end(), t, cmp);
				transcripts.insert(k, t);
			}
		}
	}

	transcripts.erase(std::remove_if(transcripts.begin(), transcripts.end(),
		[](auto& ts) { return ts.r.end <= ts.r.start; }),
		transcripts.end());

	return transcripts;
}

