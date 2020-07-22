// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

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
#include "binom.hpp"
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

void ScreenData::map(const std::string& assembly, unsigned readLength,
	fs::path bowtie, fs::path bowtieIndex, unsigned threads)
{
	fs::path assemblyDataPath = mDataDir / assembly / std::to_string(readLength);
	if (not fs::exists(assemblyDataPath))
		fs::create_directories(assemblyDataPath);
	
	for (auto fi = std::filesystem::directory_iterator(mDataDir); fi != std::filesystem::directory_iterator(); ++fi)
	{
		if (fi->is_directory())
			continue;
		
		fs::path p = *fi;

		auto name = p.filename();
		if (name.extension() == ".gz")
			name = name.stem();
		if (name.extension() != ".fastq")
			continue;
		name = name.stem();

		auto hits = runBowtie(bowtie, bowtieIndex, p, threads, readLength);
		std::cout << "Unique hits in " << name << " channel: " << hits.size() << std::endl;

		std::ofstream out(assemblyDataPath / name, std::ios::binary);
		const char* data = reinterpret_cast<const char*>(hits.data());
		size_t length = hits.size() * sizeof(Insertion);
		out.write(data, length);
	}
}

// --------------------------------------------------------------------

IPScreenData::IPScreenData(std::filesystem::path dir)
	: ScreenData(dir)
{
}

void IPScreenData::addFiles(std::filesystem::path low, std::filesystem::path high)
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

	for (auto p: { std::make_pair("low.fastq", low), std::make_pair("high.fastq", high) })
	{
		fs::path to;
		if (p.second.extension() == ".gz" or p.second.extension() == ".bz2")
			to = mDataDir / (p.first + p.second.extension().string());
		else
			to = mDataDir / p.first;
		
		fs::create_symlink(p.second, to);
	}	
}

void IPScreenData::analyze(const std::string& assembly, unsigned readLength, std::vector<Transcript>& transcripts,
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

				for (const auto& [chr, strand, pos]: bwt)
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
IPScreenData::insertions(const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end)
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

std::vector<IPDataPoint> IPScreenData::dataPoints(const std::string& assembly,
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

std::vector<IPDataPoint> IPScreenData::dataPoints(const std::vector<Transcript>& transcripts,
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

	std::vector<IPDataPoint> result;

	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		auto& t = transcripts[i];
		const auto& [low, high] = countLowHigh(i);

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

		IPDataPoint p;
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

SLScreenData::SLScreenData(std::filesystem::path dir)
	: ScreenData(dir)
{
}

void SLScreenData::addFile(std::filesystem::path file)
{
	// follow links until we end up at the final destination
	while (fs::is_symlink(file))
		file = fs::read_symlink(file);
	
	// And then make these canonical/system_complete
	file = fs::weakly_canonical(file);

	checkIsFastQ(file);

	auto ext = file.extension();

	int r = 1;
	for (; r <= 4; ++r)
	{
		auto name = "replicate-" + std::to_string(r) + ".fastq";

		if (fs::exists(mDataDir / name) or
			fs::exists(mDataDir / (name + ".gz")) or
			fs::exists(mDataDir / (name + ".bz2")))
		{
			continue;
		}

		std::filesystem::path to;
		if (ext == ".gz" or ext == ".bz2")
			to = mDataDir / (name + ext.string());
		else
			to = mDataDir / name;
		
		fs::create_symlink(file, to);
		break;
	}

	if (r > 4)
		throw std::runtime_error("Screen already contains 4 fastq files");
}

void SLScreenData::count_insertions(int replicate, const std::string& assembly, unsigned readLength,
	const std::vector<Transcript>& transcripts, std::vector<Insertions>& insertions)
{
	boost::thread_group t;
	std::exception_ptr eptr;

	auto rn = "replicate-" + std::to_string(replicate);

	auto infile = mDataDir / assembly / std::to_string(readLength) / rn;
	if (not fs::exists(infile))
		throw std::runtime_error("Missing " + rn + " file, did you run map already?");

	insertions.resize(transcripts.size());

	auto size = fs::file_size(infile);
	auto N = size / sizeof(Insertion);

	std::vector<Insertion> bwt(N);

	std::ifstream file(infile, std::ios::binary);
	if (not file.is_open())
		throw std::runtime_error("Could not open " + rn + " file, did you run map already?");
	
	file.read(reinterpret_cast<char*>(bwt.data()), size);

	auto ts = transcripts.begin();

	for (const auto& [chr, strand, pos]: bwt)
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
				std::cerr << "hit " << t->geneName << " " << (strand == t->strand ? "sense" : "anti-sense") << std::endl;

			for (auto& r: t->ranges)
			{
				if (pos >= r.start and pos < r.end)
				{
					if (strand == t->strand)
						insertions[t - transcripts.begin()].sense.insert(pos);
					else
						insertions[t - transcripts.begin()].antiSense.insert(pos);
				}
			}

			++t;
		}
	}
}

// // std::tuple<std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>>
// // 	insertions(const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end);

// std::vector<SLDataPoint> SLScreenData::dataPoints(int replicate, const std::string& assembly,
// 	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
// {
// 	const unsigned readLength = 50;

// 	auto transcripts = loadTranscripts(assembly, mode, geneStart, geneEnd, cutOverlap);

// 	// -----------------------------------------------------------------------
	
// 	std::vector<Insertions> insertions;

// 	count_insertions(replicate, assembly, readLength, transcripts, insertions);

// 	return dataPoints(transcripts, insertions);
// }

std::vector<std::tuple<size_t,size_t>> divide(size_t listsize, size_t suggested_groupsize)
{
	size_t nrOfGroups = std::round(static_cast<float>(listsize) / suggested_groupsize);
	float groupsize = static_cast<float>(listsize) / nrOfGroups;

	std::vector<std::tuple<size_t,size_t>> result;

	size_t b = 0;
	for (float i = groupsize; i < listsize; i += groupsize)
	{
		size_t e = static_cast<size_t>(std::floor(i));
		result.emplace_back(b, e);
		b = e;
	}

	// due to rounding errors, the last may be incorrect
	std::get<1>(result.back()) = listsize;

	return result;
}

std::vector<SLDataPoint> SLScreenData::dataPoints(const std::vector<Transcript>& transcripts,
	const std::vector<Insertions>& insertions, const std::array<std::vector<Insertions>,4>& controlInsertions)
{
	std::vector<double> pvalues[5];
	for (auto& pv: pvalues)
		pv.resize(transcripts.size());

	parallel_for(transcripts.size(), [&](size_t i)
	{
		int x = static_cast<int>(insertions[i].sense.size());
		int n = x + static_cast<int>(insertions[i].antiSense.size());
		pvalues[0][i] = binom_test(x, n);

		for (int j = 0; j < 4; ++j)
		{
			x = static_cast<int>(controlInsertions[j][i].sense.size());
			n = x + static_cast<int>(controlInsertions[j][i].antiSense.size());
			pvalues[j + 1][i] = binom_test(x, n);
		}
	});

	std::vector<double> fcpv[5];
	parallel_for(5, [&](size_t i)
	{
		fcpv[i] = adjustFDR_BH(pvalues[i]);
	});

	std::vector<SLDataPoint> datapoints(transcripts.size(), SLDataPoint{});

	parallel_for(transcripts.size(), [&](size_t i)
	{
		auto& t = transcripts[i];

		int sense = insertions[i].sense.size();
		int antisense = insertions[i].antiSense.size();

		int ref_sense = 
			controlInsertions[0][i].sense.size() +
			controlInsertions[1][i].sense.size() +
			controlInsertions[2][i].sense.size() +
			controlInsertions[3][i].sense.size();

		int ref_antisense = 
			controlInsertions[0][i].antiSense.size() +
			controlInsertions[1][i].antiSense.size() +
			controlInsertions[2][i].antiSense.size() +
			controlInsertions[3][i].antiSense.size();

		SLDataPoint& p = datapoints[i];

		p.geneName = t.geneName;
		p.geneID = i;
		p.pv = pvalues[0][i];
		p.fcpv = fcpv[0][i];
		
		for (int j = 0; j < 4; ++j)
		{
			p.ref_pv[j] = pvalues[j + 1][i];
			p.ref_fcpv[j] = fcpv[j + 1][i];
		}

		p.sense = sense;
		p.antisense = antisense;
		if (sense or antisense)
			p.sense_ratio = (sense + 1.0f) / (sense + antisense + 2);
		else
			p.sense_ratio = -1;
		
		if (ref_sense or ref_antisense)
			p.ref_sense_ratio = (ref_sense + 1.0f) / (ref_sense + ref_antisense + 2);
		else
			p.ref_sense_ratio = -1;
	});

	// collect the datapoints with both counts in sample and in reference

	std::vector<size_t> index;
	index.reserve(transcripts.size());
	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		if (datapoints[i].sense_ratio <= 0 or datapoints[i].ref_sense_ratio <= 0)
			continue;
		
		index.push_back(i);
	}

	// sort datapoints based on ref_ratio	
	std::sort(index.begin(), index.end(),
		[&datapoints](size_t a, size_t b) { return datapoints[a].ref_sense_ratio < datapoints[b].ref_sense_ratio; });

#warning "should be parameter"
const size_t kGroupSize = 500;

	auto groups = divide(index.size(), kGroupSize);

	std::v


	parallel_for(groups.size(), [&](size_t i)
	{
		const auto& [b, e] = groups[i];
		auto l = e - b;

		// calculate median ratio for sample and reference in this group
		float sample_median, ref_median;

		if (l & 1)
		{
			auto ix = (e + b) / 2 + 1;
			sample_median = datapoints[index[ix]].sense_ratio;
			ref_median = datapoints[index[ix]].ref_sense_ratio;
		}
		else
		{
			auto ix = (e + b) / 2;
			sample_median = (datapoints[index[ix]].sense_ratio + datapoints[index[ix + 1]].sense_ratio) / 2.0;
			ref_median = (datapoints[index[ix]].ref_sense_ratio + datapoints[index[ix + 1]].ref_sense_ratio) / 2.0;
		}
		
		// adjust counts
		for (size_t ix = b; ix < e; ++ix)
		{
			auto& dp = datapoints[index[ix]];

			float f = dp.sense_ratio <= sample_median
				? (ref_median * dp.sense_ratio) / sample_median
				: 1 - ((1 - ref_median) * (1 - dp.sense_ratio)) / (1 - sample_median);

			if (f > 1)
				f = 1;

			dp.sense_normalized = static_cast<int>(std::round(f * (dp.sense + dp.antisense)));
			dp.antisense_normalized = dp.sense + dp.antisense - dp.sense_normalized;
		}

		// 

	});

	// remove redundant datapoints
	datapoints.erase(
		std::remove_if(datapoints.begin(), datapoints.end(), [](auto& dp) { return dp.sense_ratio < 0; }),
		datapoints.end());
	
	return datapoints;
}

// --------------------------------------------------------------------

ScreenData* ScreenData::create(ScreenType type, std::filesystem::path dir)
{
	if (fs::exists(dir))
		throw std::runtime_error("Screen already exists");

	fs::create_directories(dir);

	switch (type)
	{
		case ScreenType::IntracellularPhenotype:
			return new IPScreenData(dir);
		
		case ScreenType::SyntheticLethal:
			return new SLScreenData(dir);
	}
}

std::tuple<std::unique_ptr<ScreenData>,ScreenType> ScreenData::create(std::filesystem::path dir)
{
	// perhaps we should improve this...

	bool hasLow = false, hasHigh = false, hasRepl[4] = {};

	for (std::filesystem::directory_iterator iter(dir); iter != std::filesystem::directory_iterator(); ++iter)
	{
		if (iter->is_directory())
			continue;
		
		auto name = iter->path().filename();
		if (name.extension() == ".gz")
			name = name.stem();
		if (name.extension() != ".fastq")
			continue;
		
		name = name.stem();
		
		if (name == "low")
			hasLow = true;
		else if (name == "high")
			hasHigh = true;
		else if (name.string().length() == 11 and name.string().compare(0, 10, "replicate-") == 0)
		{
			char d = name.string().back();
			if (d >= '1' and d <= '4')
				hasRepl[d - '1'] = true;
		}
	}

	if (hasLow and hasHigh)
		return { std::unique_ptr<ScreenData>(new IPScreenData(dir)), ScreenType::IntracellularPhenotype };
	
	if (hasRepl[0])
		return { std::unique_ptr<ScreenData>(new SLScreenData(dir)), ScreenType::SyntheticLethal };
	
	throw std::runtime_error("Incomplete screen, missing data files");
}
