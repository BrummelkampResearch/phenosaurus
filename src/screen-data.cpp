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

#include <zeep/value-serializer.hpp>
#include <zeep/json/parser.hpp>

#include "sq/squeeze.hpp"

#include "screen-data.hpp"
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

ScreenData::ScreenData(const fs::path& dir)
	: mDataDir(dir)
{
	if (not fs::exists(dir))
		throw std::runtime_error("Screen does not exist, directory not found: " + dir.string());
	
	fs::path manifest = dir / "manifest.json";
	if (not fs::exists(manifest))
		throw std::runtime_error("No manifest file, this is not a valid screen (" + dir.string() + ')');

	std::ifstream manifestFile(manifest);
	if (not manifestFile.is_open())
		throw std::runtime_error("Could not open manifest file (" + dir.string() + ')');

	zeep::json::element jInfo;
	zeep::json::parse_json(manifestFile, jInfo);

	zeep::json::from_element(jInfo, mInfo);
}

ScreenData::ScreenData(const fs::path& dir, const screen_info& info)
	: mDataDir(dir), mInfo(info)
{
	if (fs::exists(dir))
		throw std::runtime_error("Screen already exists");

	fs::create_directories(dir);

	write_manifest();
}

void ScreenData::write_manifest()
{
	std::ofstream manifest(mDataDir / "manifest.json");
	if (not manifest.is_open())
		throw std::runtime_error("Could not create manifest file in " + mDataDir.string());

	zeep::json::element jInfo;
	zeep::json::to_element(jInfo, mInfo);
	manifest << jInfo;
	manifest.close();
}

void ScreenData::addFile(const std::string& name, fs::path file)
{
	// follow links until we end up at the final destination
	while (fs::is_symlink(file))
		file = fs::read_symlink(file);
	
	// And then make these canonical/system_complete
	file = fs::weakly_canonical(file);

	checkIsFastQ(file);

	auto ext = file.extension();

	fs::path to;
	if (ext == ".gz" or ext == ".bz2")
		to = mDataDir / (name + ".fastq" + ext.string());
	else
		to = mDataDir / (name + ".fastq");
		
	fs::create_symlink(file, to);

	mInfo.files.push_back({ name, file });

	write_manifest();
}

void ScreenData::map(const std::string& assembly, unsigned trimLength,
	fs::path bowtie, fs::path bowtieIndex, unsigned threads)
{
	fs::path assemblyDataPath = mDataDir / assembly / std::to_string(trimLength);
	if (not fs::exists(assemblyDataPath))
		fs::create_directories(assemblyDataPath);
	
	for (auto fi = fs::directory_iterator(mDataDir); fi != fs::directory_iterator(); ++fi)
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

		auto hits = runBowtie(bowtie, bowtieIndex, p, threads, trimLength);
		std::cout << "Unique hits in " << name << " channel: " << hits.size() << std::endl;

		write_insertions(assembly, trimLength, name, hits);
	}

	std::string version = bowtieVersion(bowtie);
	if (version.empty())
		version = "(unknown, path is " + bowtie.string() + ')';

	bool isSet = false;

	for (auto& mi: mInfo.mappedInfo)
	{
		if (mi.assembly == assembly and mi.trimlength == trimLength)
		{
			mi.bowtie_version = version;
			mi.bowtie_index = bowtieIndex;
			mi.bowtie_params = "-m 1 --best";
			isSet = true;
			break;
		}
	}

	if (not isSet)
		mInfo.mappedInfo.push_back({ assembly, trimLength, version, "-m 1 --best", bowtieIndex });

	write_manifest();
}

// --------------------------------------------------------------------

std::vector<Insertion> ScreenData::read_insertions(std::filesystem::path file)
{
	bool compressed = file.extension() == ".sq";
	if (not compressed)	// see if a compressed version exists
	{
		auto psq = file.parent_path() / (file.filename().string() + ".sq");
		if (fs::exists(psq))
		{
			compressed = true;
			file = psq;
		}
	}

	if (not fs::exists(file))
		throw std::runtime_error("File does not exist: " + file.string());

	std::ifstream infile(file, std::ios::binary);
	if (not infile.is_open())
		throw std::runtime_error("Could not open " + file.string() + " file");

	auto size = fs::file_size(file);

	std::vector<Insertion> result;

	if (compressed)
	{
		std::vector<uint8_t> bits(size);

		infile.read(reinterpret_cast<char*>(bits.data()), size);

		sq::ibitstream ibs(bits);
		size_t N = read_gamma(ibs);

		result.reserve(N);

		for (auto chr = CHROM::CHR_1; chr <= CHR_Y; chr = static_cast<CHROM>(static_cast<uint8_t>(chr) + 1))
		{
			// plus and minus are stored separatedly, but we don't want to sort everything, so be smart

			std::vector<uint32_t> pos_plus, pos_negative;

			if (ibs())
				pos_plus = sq::read_array(ibs);
			
			if (ibs())
				pos_negative = sq::read_array(ibs);
			
			auto pi = pos_plus.begin(),		epi = pos_plus.end();
			auto ni = pos_negative.begin(),	eni = pos_negative.end();

			while (pi != epi or ni != eni)
			{
				if (ni == eni)
				{
					result.push_back(Insertion{ chr, '+', *pi++ });
					continue;
				}

				if (pi == epi)
				{
					result.push_back(Insertion{ chr, '-', *ni++ });
					continue;
				}

				if (*pi <= *ni)
					result.push_back(Insertion{ chr, '+', *pi++ });
				else
					result.push_back(Insertion{ chr, '-', *ni++ });
			}
		}
	}
	else
	{
		auto N = size / sizeof(Insertion);

		result.resize(N);
		infile.read(reinterpret_cast<char*>(result.data()), size);
	}

	infile.close();

	return result;
}

// --------------------------------------------------------------------

std::vector<Insertion> ScreenData::read_insertions(const std::string& assembly, unsigned readLength, const std::string& file) const
{
	return read_insertions(mDataDir / assembly / std::to_string(readLength) / file);
}

void ScreenData::write_insertions(const std::string& assembly, unsigned readLength, const std::string& file,
	std::vector<Insertion>& insertions)
{
	std::sort(insertions.begin(), insertions.end(), [](const Insertion& a, const Insertion& b)
	{
		int d = a.chr - b.chr;
		if (d == 0)
			d = a.strand - b.strand;
		if (d == 0)
			d = a.pos - b.pos;
		return d < 0;
	});

	std::vector<uint8_t> bits;
	sq::obitstream obs(bits);

	write_gamma(obs, insertions.size());

	size_t i = 0;

	for (auto chr = CHROM::CHR_1; chr <= CHR_Y; chr = static_cast<CHROM>(static_cast<uint8_t>(chr) + 1))
	{
		for (char str: { '+', '-' })
		{
			std::vector<uint32_t> pos;
			
			while (i < insertions.size())
			{
				auto& ins = insertions[i];
				if (ins.chr != chr or ins.strand != str)
					break;

				pos.push_back(ins.pos);

				++i;
			}

			obs << not pos.empty();
			if (not pos.empty())
				sq::write_array(obs, pos);
		}
	}

	assert(i == insertions.size());

	obs.sync();

	fs::path p = mDataDir / assembly / std::to_string(readLength) / (file + ".sq");
	std::ofstream outfile(p, std::ios::binary | std::ios::trunc);
	if (not outfile.is_open())
		throw std::runtime_error("Could not open " + p.string() + " file");
	
	outfile.write(reinterpret_cast<char*>(bits.data()), bits.size());
	outfile.close();
}

// --------------------------------------------------------------------

void ScreenData::dump_map(const std::string& assembly, unsigned readLength, const std::string& file)
{
	auto bwt = read_insertions(assembly, readLength, file);

	for (auto&& [chr, strand, pos]: bwt)
	{
		assert(chr != CHROM::INVALID);
		std::cout << zeep::value_serializer<CHROM>::to_string(chr) << "\t" << strand << "\t" << pos << std::endl;
	}
}

// --------------------------------------------------------------------

void ScreenData::compress_map(const std::string& assembly, unsigned readLength, const std::string& file)
{
	fs::path p = mDataDir / assembly / std::to_string(readLength) / file;

	if (not fs::exists(p))
		throw std::runtime_error("File does not exist: " + p.string());

	auto size = fs::file_size(p);
	auto N = size / sizeof(Insertion);

	std::vector<Insertion> bwt(N);

	std::ifstream infile(p, std::ios::binary);
	if (not infile.is_open())
		throw std::runtime_error("Could not open " + p.string() + " file");
	
	infile.read(reinterpret_cast<char*>(bwt.data()), size);
	infile.close();

	std::sort(bwt.begin(), bwt.end(), [](const Insertion& a, const Insertion& b)
	{
		int d = a.chr - b.chr;
		if (d == 0)
			d = a.strand - b.strand;
		if (d == 0)
			d = a.pos - b.pos;
		return d < 0;
	});

	std::vector<uint8_t> bits;
	sq::obitstream obs(bits);

	write_gamma(obs, bwt.size());

	size_t i = 0;

	for (auto chr = CHROM::CHR_1; chr <= CHR_Y; chr = static_cast<CHROM>(static_cast<uint8_t>(chr) + 1))
	{
		for (char str: { '+', '-' })
		{
			std::vector<uint32_t> pos;
			
			while (i < bwt.size())
			{
				auto& ins = bwt[i];
				if (ins.chr != chr or ins.strand != str)
					break;

				pos.push_back(ins.pos);

				++i;
			}

			obs << not pos.empty();
			if (not pos.empty())
				sq::write_array(obs, pos);
		}
	}

	assert(i == bwt.size());

	obs.sync();

	p = p.parent_path() / (p.filename().string() + ".sq");
	std::ofstream outfile(p, std::ios::binary | std::ios::trunc);
	if (not outfile.is_open())
		throw std::runtime_error("Could not open " + p.string() + " file");
	
	outfile.write(reinterpret_cast<char*>(bits.data()), bits.size());
	outfile.close();
}

// --------------------------------------------------------------------

IPPAScreenData::IPPAScreenData(ScreenType type, const fs::path& dir)
	: ScreenData(dir), mType(type)
{
	if (mInfo.type != mType)
		throw std::runtime_error("This screen is not of the specified type");
}

IPPAScreenData::IPPAScreenData(ScreenType type, const fs::path& dir, const screen_info& info, fs::path low, fs::path high)
	: ScreenData(dir, info), mType(type)
{
	addFile("low", low);
	addFile("high", high);
}

void IPPAScreenData::analyze(const std::string& assembly, unsigned readLength, const std::vector<Transcript>& transcripts,
	std::vector<Insertions>& lowInsertions, std::vector<Insertions>& highInsertions)
{
	// reorder transcripts based on chr > end-position, makes code easier and faster
#if DEBUG
	auto tc = transcripts;
	std::stable_sort(tc.begin(), tc.end());
	assert(tc == transcripts);
#endif

	std::list<std::thread> t;
	std::exception_ptr eptr;

	for (std::string s: { "low", "high" })
	{
#ifndef DEBUG
		t.emplace_back([&, lh = s]()
		{
#else
		auto lh = s;
#endif
			try
			{
				auto bwt = read_insertions(assembly, readLength, lh);

				std::vector<Insertions> insertions(transcripts.size());

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

	for (auto& ti: t)
		ti.join();

	if (eptr)
		std::rethrow_exception(eptr);

	if (lowInsertions.size() != transcripts.size() or highInsertions.size() != transcripts.size())
		throw std::runtime_error("Failed to calculate analysis");
}

// --------------------------------------------------------------------

std::tuple<std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>>
IPPAScreenData::insertions(const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end)
{
	const unsigned readLength = 50;

	std::vector<uint32_t> lowP, lowM, highP, highM;

	std::list<std::thread> t;
	std::exception_ptr eptr;

	for (std::string s: { "low", "high" })
	{
		t.emplace_back([&, lh = s]()
		{
			try
			{
				std::vector<uint32_t>& insP = lh == "low" ? lowP : highP;
				std::vector<uint32_t>& insM = lh == "low" ? lowM : highM;

				auto bwt = read_insertions(assembly, readLength, lh);
			
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

	for (auto& ti: t)
		ti.join();

	if (eptr)
		std::rethrow_exception(eptr);

	return std::make_tuple(std::move(highP), std::move(highM), std::move(lowP), std::move(lowM));
}

// --------------------------------------------------------------------

std::vector<IPDataPoint> IPPAScreenData::dataPoints(const std::string& assembly,
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

std::vector<IPDataPoint> IPPAScreenData::dataPoints(const std::vector<Transcript>& transcripts,
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
	std::vector<IPDataPoint> result(transcripts.size());

	parallel_for(transcripts.size(), [&](size_t i)
	{
		auto& t = transcripts[i];
		auto& p = result[i];

		std::tie(p.low, p.high) = countLowHigh(i);

		double miL = p.low, miH = p.high, miLT = lowCount - p.low, miHT = highCount - p.high;
		if (p.low == 0)
		{
			miL = 1;
			miLT -= 1;
		}

		if (p.high == 0)
		{
			miH = 1;
			miHT -= 1;
		}

		long v[2][2] = {
			{ p.low, p.high },
			{ lowCount - p.low, highCount - p.high }
		};

		pvalues[i] = fisherTest2x2(v);

		p.gene = t.geneName;
		p.pv = pvalues[i];
		p.mi = ((miH / miHT) / (miL / miLT));
	});

	auto fcpv = adjustFDR_BH(pvalues);

	for (size_t i = 0; i < transcripts.size(); ++i)
		result[i].fcpv = fcpv[i];
	
	return result;
}

// --------------------------------------------------------------------

SLScreenData::SLScreenData(const fs::path& dir)
	: ScreenData(dir)
{
	if (mInfo.type != screen_type)
		throw std::runtime_error("This screen is not of the specified type");
}

SLScreenData::SLScreenData(const fs::path& dir, const screen_info& info)
	: ScreenData(dir, info)
{
}

std::vector<std::string> SLScreenData::getReplicateNames() const
{
	std::vector<std::string> result;

	for (auto& f: mInfo.files)
		result.push_back(f.name);

	return result;
}

void SLScreenData::addFile(const std::string& name, fs::path file)
{
	if (mInfo.files.size() >= 4)
		throw std::runtime_error("Screen already contains 4 fastq files");

	ScreenData::addFile(name, file);
}

SLDataResult SLScreenData::dataPoints(const std::string& assembly, unsigned trimLength,
	const std::vector<Transcript>& transcripts, const SLScreenData& controlData, unsigned groupSize,
	float pvCutOff, float binomCutOff, float effectSize)
{
	// First load the control data
	std::array<std::vector<InsertionCount>,4> controlInsertions;

	std::exception_ptr eptr;

	parallel_for(4, [&](size_t i) {
		try
		{
			controlData.count_insertions("replicate-" + std::to_string(i + 1), assembly, trimLength, transcripts, controlInsertions[i]);
		}
		catch(const std::exception& e)
		{
			eptr = std::current_exception();
		}
	});

	if (eptr)
		std::rethrow_exception(eptr);

	std::array<std::vector<InsertionCount>,4> normalizedControlInsertions;
	parallel_for(4, [&](size_t i) {
		try
		{
			normalizedControlInsertions[i] = normalize(controlInsertions[i], controlInsertions, groupSize);
		}
		catch (const std::exception& e)
		{
			eptr = std::current_exception();
		}
	});

	if (eptr)
		std::rethrow_exception(eptr);

	SLDataResult result;

	for (auto& f: mInfo.files)
		result.replicate.push_back({ f.name });
	
	parallel_for(result.replicate.size(), [&](size_t i) {
		try
		{
			auto& replicate = result.replicate[i];

			// Then load the screen data
			std::vector<InsertionCount> insertions;
			count_insertions(replicate.name, assembly, trimLength, transcripts, insertions);

			// And now analyse this
			replicate.data = dataPoints(transcripts, insertions, normalizedControlInsertions, groupSize);

			// // remove redundant datapoints
			// replicate.data.erase(
			// 	std::remove_if(replicate.data.begin(), replicate.data.end(), [](auto& dp) { return dp.sense == 0 or dp.antisense == 0; }),
			// 	replicate.data.end());
		}
		catch (const std::exception& e)
		{
			eptr = std::current_exception();
		}
	});

	if (eptr)
		std::rethrow_exception(eptr);
	
	// find out which genes are significant

	std::mutex m;
	parallel_for(transcripts.size(), [&](size_t i) {
	// for (size_t i = 0; i < transcripts.size(); ++ i) {

		double minSenseRatio = std::numeric_limits<double>::max();
		for (size_t j = 0; j < 4; ++j)
		{
			auto& nc = normalizedControlInsertions[j][i];
			if (minSenseRatio > (nc.sense + 1.0) / (nc.sense + nc.antiSense + 2))
				minSenseRatio = (nc.sense + 1.0) / (nc.sense + nc.antiSense + 2);
		}

		auto maxSenseRatio = std::numeric_limits<double>::lowest();
		size_t n = 0;
		size_t s_g = 0, a_g = 0;

		for (auto& r: result.replicate)
		{
			auto& nc = r.data[i];

			if (nc.binom_fdr > binomCutOff)
				continue;
			
			// if (nc.ref_fcpv[0] > pvCutOff or nc.ref_fcpv[1] > pvCutOff or nc.ref_fcpv[2] > pvCutOff or nc.ref_fcpv[3] > pvCutOff)
			// 	continue;

			if (nc.ref_pv[0] > pvCutOff or nc.ref_pv[1] > pvCutOff or nc.ref_pv[2] > pvCutOff or nc.ref_pv[3] > pvCutOff)
				continue;
			
			double senseRatio = (nc.sense + 1.0) / (nc.sense + nc.antisense + 2);
			if (senseRatio >= 0.5)
				continue;

			++n;

			s_g += nc.sense_normalized;
			a_g += nc.antisense_normalized;

			if (maxSenseRatio < senseRatio)
				maxSenseRatio = senseRatio;
		}

		if (n == result.replicate.size())
		{
			size_t s_wt = 0, a_wt = 0;

			for (auto& nc: normalizedControlInsertions)
			{
				s_wt += nc[i].sense;
				a_wt += nc[i].antiSense;
			}

			if ((1.0f * s_wt) / a_wt >= (effectSize * s_g) / a_g)
			{
				std::unique_lock lock(m);
				result.significant.insert(transcripts[i].geneName);
			}
		}

		// if (maxSenseRatio > 0 and maxSenseRatio < minSenseRatio and (minSenseRatio - maxSenseRatio) >= effectSize and minSenseRatio != 0.5)
		// {
		// 	std::unique_lock lock(m);
		// 	result.significant.insert(transcripts[i].geneName);
		// }
	// }
	});

	for (auto& r: result.replicate)
	{
		// remove redundant datapoints
		r.data.erase(
			std::remove_if(r.data.begin(), r.data.end(), [](auto& dp) { return dp.sense == 0 or dp.antisense == 0; }),
			r.data.end());
	}
	
	return result;
}

// --------------------------------------------------------------------

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
	if (not result.empty())
		std::get<1>(result.back()) = listsize;

	return result;
}

// --------------------------------------------------------------------

std::vector<InsertionCount> SLScreenData::normalize(const std::vector<InsertionCount>& insertions,
	const std::array<std::vector<InsertionCount>,4>& controlInsertions, unsigned groupSize)
{
	std::vector<double> senseRatio(insertions.size()), refSenseRatio(insertions.size());
	std::vector<InsertionCount> result(insertions);

	parallel_for(insertions.size(), [&](size_t i)
	{
		int sense = insertions[i].sense;
		int antisense = insertions[i].antiSense;

		if (sense + antisense >= 20 and
			((controlInsertions[0][i].sense + controlInsertions[0][i].antiSense) >= 20) and
			((controlInsertions[1][i].sense + controlInsertions[1][i].antiSense) >= 20) and
			((controlInsertions[2][i].sense + controlInsertions[2][i].antiSense) >= 20) and
			((controlInsertions[3][i].sense + controlInsertions[3][i].antiSense) >= 20))
		{
			int ref_sense = 
				controlInsertions[0][i].sense +
				controlInsertions[1][i].sense +
				controlInsertions[2][i].sense +
				controlInsertions[3][i].sense;

			int ref_antisense = 
				controlInsertions[0][i].antiSense +
				controlInsertions[1][i].antiSense +
				controlInsertions[2][i].antiSense +
				controlInsertions[3][i].antiSense;

			senseRatio[i] = (sense + 1.0f) / (sense + antisense + 2);
			refSenseRatio[i] = (ref_sense + 1.0f) / (ref_sense + ref_antisense + 2);
		}
	});

	// collect the datapoints with both counts in sample and in reference

	std::vector<size_t> index;
	index.reserve(insertions.size());
	for (size_t i = 0; i < insertions.size(); ++i)
	{
		if (senseRatio[i] <= 0 or refSenseRatio[i] <= 0)
			continue;
		
		index.push_back(i);
	}

	// sort datapoints based on ref_ratio	
	std::sort(index.begin(), index.end(),
		[&refSenseRatio](size_t a, size_t b) { return refSenseRatio[a] < refSenseRatio[b]; });

	auto groups = divide(index.size(), groupSize);

	parallel_for(groups.size(), [&](size_t i)
	{
		const auto& [b, e] = groups[i];
		auto l = e - b;

		// calculate median ratio for sample and reference in this group
		// The median for ref can be picked up immediately since the
		// group is already sorted on this value
		double ref_median;

		if (l & 1)
		{
			auto ix = (e + b) / 2 + 1;
			ref_median = refSenseRatio[index[ix]];
		}
		else
		{
			auto ix = (e + b) / 2;
			ref_median = (refSenseRatio[index[ix]] + refSenseRatio[index[ix + 1]]) / 2.0;
		}

		// median for sample needs to be calculated
		std::vector<double> srs;
		for (auto ix = b; ix < e; ++ix)
			srs.push_back(senseRatio[index[ix]]);
		std::sort(srs.begin(), srs.end());
		double sample_median = l & 1
			? srs[l / 2 + 1]
			: (srs[l / 2] + srs[l / 2 + 1]) / 2.0;
		
		// adjust counts
		for (size_t ix = b; ix < e; ++ix)
		{
			auto iix = index[ix];
			assert(iix < insertions.size());

			auto iSenseRatio = senseRatio[iix];

			double f = iSenseRatio < sample_median
				? (ref_median * iSenseRatio) / sample_median
				: 1 - ((1 - ref_median) * (1 - iSenseRatio)) / (1 - sample_median);

			if (f > 1)
				f = 1;

			auto total = insertions[iix].sense + insertions[iix].antiSense;
			result[iix].sense = static_cast<int>(std::round(f * (total)));
			result[iix].antiSense = total - result[iix].sense;
		}
	});

	return result;
}

void SLScreenData::count_insertions(const std::string& replicate, const std::string& assembly, unsigned trimLength,
	const std::vector<Transcript>& transcripts, std::vector<InsertionCount>& insertions) const
{
	insertions.resize(transcripts.size());

	auto bwt = read_insertions(assembly, trimLength, replicate);
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
			for (auto& r: t->ranges)
			{
				if (pos >= r.start and pos < r.end)
				{
					if (VERBOSE >= 3)
						std::cerr << "hit\t" << t->geneName << "\t" << pos << "\t" << (strand == t->strand ? "sense" : "anti-sense") << std::endl;

					if (strand == t->strand)
						insertions[t - transcripts.begin()].sense += 1;
					else
						insertions[t - transcripts.begin()].antiSense += 1;
				}
			}

			++t;
		}
	}
}

std::tuple<std::vector<uint32_t>,std::vector<uint32_t>> SLScreenData::getInsertionsForReplicate(const std::string& replicate,
	const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end) const
{
	const unsigned readLength = 50;

	std::vector<uint32_t> insP;
	std::vector<uint32_t> insM;

	auto bwt = read_insertions(assembly, readLength, replicate);

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

	return std::make_tuple(std::move(insP), std::move(insM));
}

// --------------------------------------------------------------------

std::vector<SLDataPoint> SLScreenData::dataPoints(const std::vector<Transcript>& transcripts,
	const std::vector<InsertionCount>& insertions, const std::array<std::vector<InsertionCount>,4>& controlInsertions,
	unsigned groupSize)
{
	auto normalized = normalize(insertions, controlInsertions, groupSize);

	const size_t N = transcripts.size();
	std::vector<SLDataPoint> datapoints(N, SLDataPoint{});

	std::vector<size_t> index;
	index.reserve(N);

	for (size_t i = 0; i < N; ++i)
	{
		if (insertions[i].sense + insertions[i].antiSense > 0)
			index.push_back(i);
	}

	const size_t M = index.size();

	std::vector<double> pvalues[5];

	for (auto& pv: pvalues)
		pv.resize(M);

	// Calculate the minimal sense ratio per gene in the controls
	std::vector<double> minSenseRatio(N);
	for (auto& cdi: controlInsertions)
	{
		for (auto i: index)
		{
			auto& cd = cdi[i];
			double r = (cd.sense + 1.0f) / (cd.sense + cd.antiSense + 2);
			if (minSenseRatio[i] > r or minSenseRatio[i] == 0)
				minSenseRatio[i] = r;
		}
	}

	parallel_for(M, [&](size_t ix)
	{
		size_t i = index[ix];

		SLDataPoint& dp = datapoints[i];

		dp.gene = transcripts[i].geneName;
		dp.sense = insertions[i].sense;
		dp.antisense = insertions[i].antiSense;
		dp.sense_normalized = normalized[i].sense;
		dp.antisense_normalized = normalized[i].antiSense;

		// calculate p-value for insertion
		pvalues[0][ix] = binom_test(dp.sense_normalized, dp.sense_normalized + dp.antisense_normalized);

		// and calculate p-values for the screen vs controls
		for (int j = 0; j < 4; ++j)
		{
			long v[2][2] = {
				{ dp.sense_normalized, dp.antisense_normalized },
				{ static_cast<long>(controlInsertions[j][i].sense), static_cast<long>(controlInsertions[j][i].antiSense) }
			};

			if (v[0][0] + v[0][1] == 0 or v[1][0] + v[1][1] == 0)
				dp.ref_pv[j] = -1;
			else
				dp.ref_pv[j] = fisherTest2x2(v);
			
			pvalues[j + 1][ix] = dp.ref_pv[j];
		}
	});

	std::vector<double> fcpv[5];
	parallel_for(5, [&](size_t i)
	{
		fcpv[i] = adjustFDR_BH(pvalues[i]);
	});

	parallel_for(M, [&](size_t ix)
	{
		size_t i = index[ix];

		auto& dp = datapoints[i];

		// dp.pv = pvalues[0][ix];
		dp.binom_fdr = fcpv[0][ix];

		dp.ref_fcpv[0]	= fcpv[1][ix];
		dp.ref_fcpv[1]	= fcpv[2][ix];
		dp.ref_fcpv[2]	= fcpv[3][ix];
		dp.ref_fcpv[3]	= fcpv[4][ix];
	});

	return datapoints;
}

// --------------------------------------------------------------------

std::unique_ptr<ScreenData> ScreenData::load(const fs::path& dir)
{
	if (not fs::exists(dir))
		throw std::runtime_error("Screen does not exist, directory not found: " + dir.string());
	
	fs::path manifest = dir / "manifest.json";
	if (not fs::exists(manifest))
		throw std::runtime_error("No manifest file, this is not a valid screen (" + dir.string() + ')');

	std::ifstream manifestFile(manifest);
	if (not manifestFile.is_open())
		throw std::runtime_error("Could not open manifest file (" + dir.string() + ')');

	zeep::json::element jInfo;
	zeep::json::parse_json(manifestFile, jInfo);

	screen_info info;
	zeep::json::from_element(jInfo, info);

	switch (info.type)
	{
		case ScreenType::IntracellularPhenotype:
			return std::make_unique<IPScreenData>(dir);

		case ScreenType::IntracellularPhenotypeActivation:
			return std::make_unique<PAScreenData>(dir);
				
		case ScreenType::SyntheticLethal:
			return std::make_unique<SLScreenData>(dir);
		
		default:
			throw std::logic_error("should not be called with unspecified");
	}
}
