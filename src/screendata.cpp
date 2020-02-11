// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <fstream>
#include <regex>
#include <iostream>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "screendata.hpp"
#include "bowtie.hpp"

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
			d = a.r.end - b.r.end;
		return d < 0;
	});

	for (std::string lh: { "low", "high" })
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
			while (ts != transcripts.end() and (ts->chrom < chr or (ts->chrom == chr and ts->r.end <= pos)))
				++ts;

			auto t = ts;
			while (t != transcripts.end() and t->chrom == chr and t->r.start <= pos)
			{
				assert(t->r.end > pos);

				if (VERBOSE >= 3)
					std::cerr << "hit " << t->geneName << " " << (strand == t->strand ? "sense" : "anti-sense") << std::endl;

				// insertions.push_back({ pos, t, strand == t->strand });
				if (strand == t->strand)
					insertions[t - transcripts.begin()].sense.insert(pos);
				else
					insertions[t - transcripts.begin()].antiSense.insert(pos);
				
				++t;
			}


		// for (auto&& [chr, strand, pos]: bwt)
		// {
		// 	// we have a valid hit at chr:pos, see if it matches a transcript

		// 	long L = 0, R = transcripts.size() - 1;
		// 	auto t = transcripts.data();

		// 	while (L <= R)
		// 	{
		// 		auto i = (L + R) / 2;
		// 		auto ti = t + i;
		// 		long d = ti->chrom - chr;
		// 		if (d == 0)
		// 			d = static_cast<long>(ti->r.start) - pos;
		// 		if (d >= 0)
		// 			R = i - 1;
		// 		else
		// 			L = i + 1;
		// 	}

		// 	auto e = t + transcripts.size();
		// 	t += L > 0 ? L - 1 : L;
		// 	while (t < e and t->chrom == chr and t->r.start <= pos)
		// 	{
		// 		if (t->r.end > pos)
		// 		{
		// 			if (VERBOSE >= 3)
		// 				std::cerr << "hit " << t->geneName << " " << (strand == t->strand ? "sense" : "anti-sense") << std::endl;

		// 			// insertions.push_back({ pos, t, strand == t->strand });
		// 			if (strand == t->strand)
		// 				insertions[t - transcripts.data()].sense.insert(pos);
		// 			else
		// 				insertions[t - transcripts.data()].antiSense.insert(pos);
		// 		}
				
		// 		++t;
		// 	}
		}
	
		if (lh == "low")
			std::swap(insertions, lowInsertions);
		else
			std::swap(insertions, highInsertions);
	}

}

ScreenData* ScreenData::create(fs::path dir, fs::path lowFastQ, fs::path highFastQ)
{
	return new ScreenData(dir, lowFastQ, highFastQ);
}