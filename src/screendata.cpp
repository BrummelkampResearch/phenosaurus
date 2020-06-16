// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <fstream>
#include <regex>
#include <iostream>
#include <future>
#include <exception>
#include <stdexcept>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <boost/thread.hpp>

#include "screendata.hpp"
#include "bowtie.hpp"
#include "fisher.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;
namespace io = boost::iostreams;
using namespace std::literals;

extern int VERBOSE;

// --------------------------------------------------------------------

void checkIsFastQ(fs::path infile)
{
	if (not fs::exists(infile))
		throw std::runtime_error("FastQ file " + infile.string() + " does not seem to exist");

	fs::path p = infile;
	std::ifstream file(p, std::ios::binary);

	if (not file.is_open())
		throw std::runtime_error("Could not open file " + infile.string());

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

	// just check the first four lines

	std::string line[4];
	if (not std::getline(in, line[0]) or 
		not std::getline(in, line[1]) or 
		not std::getline(in, line[2]) or 
		not std::getline(in, line[3]))
	{
		throw std::runtime_error("Could not read from " + infile.string() + ", invalid file?");
	}

	if (line[0].length() < 2 or line[0][0] != '@')
		throw std::runtime_error("Invalid FastQ file " + infile.string() + ", first line not valid");

	if (line[2].empty() or line[2][0] != '+')
		throw std::runtime_error("Invalid FastQ file " + infile.string() + ", third line not valid");

	if (line[1].length() != line[3].length() or line[1].empty())
		throw std::runtime_error("Invalid FastQ file " + infile.string() + ", no valid sequence data");
}

// --------------------------------------------------------------------

ScreenData::ScreenData(std::filesystem::path dir)
	: mDataDir(dir)
{
	if (not fs::exists(dir))
		throw std::runtime_error("Screen does not exist, directory not found: " + dir.string());
}

ScreenData::ScreenData(std::filesystem::path dir, std::filesystem::path low, std::filesystem::path high)
	: mDataDir(dir)
{
	// follow links until we end up at the final destination
	while (fs::is_symlink(low))
		low = fs::read_symlink(low);
	
	while (fs::is_symlink(high))
		low = fs::read_symlink(high);
	
	// And then make these canonical/system_complete
	low = fs::weakly_canonical(low);
	high = fs::weakly_canonical(high);

	checkIsFastQ(low);
	checkIsFastQ(high);

	fs::create_directory(dir);

	for (auto p: { std::make_pair("low.fastq", low), std::make_pair("high.fastq", high) })
	{
		fs::path to;
		if (p.second.extension() == ".gz" or p.second.extension() == ".bz2")
			to = dir / (p.first + p.second.extension().string());
		else
			to = dir / p.first;
		
		fs::create_symlink(p.second, to);
	}
}

void ScreenData::map(const std::string& assembly, unsigned readLength,
	fs::path bowtie, fs::path bowtieIndex, unsigned threads)
{
	fs::path assemblyDataPath = mDataDir / assembly / std::to_string(readLength);
	if (not fs::exists(assemblyDataPath))
		fs::create_directories(assemblyDataPath);
	
	for (std::string ch: { "low", "high" })
	{
		fs::path p = mDataDir / (ch + ".fastq");
		if (not fs::exists(p))
			p = mDataDir / (ch + ".fastq.gz");

		auto hits = runBowtie(bowtie, bowtieIndex, p, threads, readLength);
		std::cout << "Unique hits in " << ch << " channel: " << hits.size() << std::endl;

		std::ofstream out(assemblyDataPath / ch, std::ios::binary);
		const char* data = reinterpret_cast<const char*>(hits.data());
		size_t length = hits.size() * sizeof(Insertion);
		out.write(data, length);
	}
}

void ScreenData::analyze(const std::string& assembly, unsigned readLength, std::vector<Transcript>& transcripts,
	std::vector<Insertions>& lowInsertions, std::vector<Insertions>& highInsertions)
{
	// reorder transcripts based on chr > end-position, makes code easier and faster

	// reorder transcripts
	std::sort(transcripts.begin(), transcripts.end(), [](auto& a, auto& b)
	{
		int d = a.chrom - b.chrom;
		if (d == 0)
			d = a.start() - b.start();
		return d < 0;
	});

	boost::thread_group t;
	std::exception_ptr eptr;

	for (std::string s: { "low", "high" })
	{
#ifndef DEBUG
		t.create_thread([&, lh = s]()
		{
#else
		auto lh = s;
#endif
			try
			{
				auto infile = mDataDir / assembly / std::to_string(readLength) / lh;
				if (not fs::exists(infile))
					throw std::runtime_error("Missing " + lh + " file, did you run map already?");

				std::vector<Insertions> insertions(transcripts.size());

				auto size = fs::file_size(infile);
				auto N = size / sizeof(Insertion);

				std::vector<Insertion> bwt(N);

				std::ifstream file(infile, std::ios::binary);
				if (not file.is_open())
					throw std::runtime_error("Could not open " + lh + " file, did you run map already?");
				
				file.read(reinterpret_cast<char*>(bwt.data()), size);
			
				auto ts = transcripts.begin();

				for (auto&& [chr, strand, pos]: bwt)
				{
					assert(chr != CHROM::INVALID);

					// we have a valid hit at chr:pos, see if it matches a transcript

					// skip all that are before the current position
					while (ts != transcripts.end() and (ts->chrom < chr or (ts->chrom == chr and ts->end() <= pos)))
						++ts;

					auto t = ts;
					while (t != transcripts.end() and t->chrom == chr and t->start() <= pos)
					{
						if (VERBOSE >= 3)
							std::cerr << "hit " << t->geneName << " " << lh << " " << (strand == t->strand ? "sense" : "anti-sense") << std::endl;

						for (auto& r: t->ranges)
						{
							if (pos >= r.start and pos < r.end)
							{
								// insertions.push_back({ pos, t, strand == t->strand });
								if (strand == t->strand)
									insertions[t - transcripts.begin()].sense.insert(pos);
								else
									insertions[t - transcripts.begin()].antiSense.insert(pos);
							}
						}

						++t;
					}
				}
			
				if (lh == "low")
					std::swap(insertions, lowInsertions);
				else
					std::swap(insertions, highInsertions);
			}
			catch (...)
			{
				eptr = std::current_exception();
			}
#ifndef DEBUG
		});
#endif
	}

	t.join_all();

	if (eptr)
		std::rethrow_exception(eptr);

	if (lowInsertions.size() != transcripts.size() or highInsertions.size() != transcripts.size())
		throw std::runtime_error("Failed to calculate analysis");
}

// --------------------------------------------------------------------

std::tuple<std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>>
ScreenData::insertions(const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end)
{
	const unsigned readLength = 50;

	std::vector<uint32_t> lowP, lowM, highP, highM;

	boost::thread_group t;
	std::exception_ptr eptr;

	for (std::string s: { "low", "high" })
	{
		t.create_thread([&, lh = s]()
		{
			try
			{
				auto infile = mDataDir / assembly / std::to_string(readLength) / lh;
				if (not fs::exists(infile))
					throw std::runtime_error("Missing " + lh + " file, did you run map already?");

				std::vector<uint32_t>& insP = lh == "low" ? lowP : highP;
				std::vector<uint32_t>& insM = lh == "low" ? lowM : highM;

				auto size = fs::file_size(infile);
				auto N = size / sizeof(Insertion);

				std::vector<Insertion> bwt(N);

				std::ifstream file(infile, std::ios::binary);
				if (not file.is_open())
					throw std::runtime_error("Could not open " + lh + " file, did you run map already?");
				
				file.read(reinterpret_cast<char*>(bwt.data()), size);
			
				for (auto&& [chr, strand, pos]: bwt)
				{
					assert(chr != CHROM::INVALID);

					if (chr == chrom and pos >= start and pos < end)
					{
						if (strand == '+')
							insP.push_back(pos);
						else
							insM.push_back(pos);
					}
				}
			}
			catch (...)
			{
				eptr = std::current_exception();
			}
		});
	}

	t.join_all();

	return std::make_tuple(std::move(highP), std::move(highM), std::move(lowP), std::move(lowM));
}

// --------------------------------------------------------------------

std::vector<DataPoint> ScreenData::dataPoints(const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction)
{
	const unsigned readLength = 50;

	auto transcripts = loadTranscripts(assembly, mode, geneStart, geneEnd, cutOverlap);

	// -----------------------------------------------------------------------
	
	std::vector<Insertions> lowInsertions, highInsertions;

	analyze(assembly, readLength, transcripts, lowInsertions, highInsertions);

	return dataPoints(transcripts, lowInsertions, highInsertions, direction);
}

std::vector<DataPoint> ScreenData::dataPoints(const std::vector<Transcript>& transcripts,
	const std::vector<Insertions>& lowInsertions, const std::vector<Insertions>& highInsertions,
		Direction direction)
{
	auto countLowHigh = [direction,&lowInsertions,&highInsertions](size_t i) -> std::tuple<long,long>
	{
		long low, high;
		switch (direction)
		{
			case Direction::Sense:
				low = lowInsertions[i].sense.size();
				high = highInsertions[i].sense.size();
				break;

			case Direction::AntiSense:
				low = lowInsertions[i].antiSense.size();
				high = highInsertions[i].antiSense.size();
				break;

			case Direction::Both:
				low = lowInsertions[i].sense.size() + lowInsertions[i].antiSense.size();
				high = highInsertions[i].sense.size() + highInsertions[i].antiSense.size();
				break;
		}
		return { low, high };
	};

	long lowCount = 0, highCount = 0;
	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		auto&& [low, high] = countLowHigh(i);;
		lowCount += low;
		highCount += high;
	}

	std::vector<double> pvalues(transcripts.size(), 0);

	parallel_for(transcripts.size(), [&](size_t i)
	{
		auto&& [low, high] = countLowHigh(i);

		long v[2][2] = {
			{ low, high },
			{ lowCount - low, highCount - high }
		};

		pvalues[i] = fisherTest2x2(v);
	});

	auto fcpv = adjustFDR_BH(pvalues);

	std::vector<DataPoint> result;

	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		auto& t = transcripts[i];
		auto&& [low, high] = countLowHigh(i);

		if (low == 0 and high == 0)
			continue;

		float miL = low, miH = high, miLT = lowCount - low, miHT = highCount - high;
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

		DataPoint p;
		p.geneName = t.geneName;
		p.geneID = i;
		p.pv = pvalues[i];
		p.fcpv = fcpv[i];
		p.mi = ((miH / miHT) / (miL / miLT));
		p.high = high;
		p.low = low;
		result.push_back(std::move(p));
	}
	
	return result;
}

// --------------------------------------------------------------------

ScreenData* ScreenData::create(fs::path dir, fs::path lowFastQ, fs::path highFastQ)
{
	return new ScreenData(dir, lowFastQ, highFastQ);
}