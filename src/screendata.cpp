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
		
		fs::create_hard_link(p.second, to);
	}
}

void ScreenData::map(const std::string& assembly,
	fs::path bowtie, fs::path bowtieIndex, unsigned threads, unsigned readLength)
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
		std::sort(hits.begin(), hits.end());
		hits.erase(std::unique(hits.begin(), hits.end()), hits.end());

		std::cout << "Unique hits in " << ch << " channel: " << hits.size() << std::endl;

		std::ofstream out(assemblyDataPath / ch, std::ios::binary);
		const char* data = reinterpret_cast<const char*>(hits.data());
		size_t length = hits.size() * sizeof(Insertion);
		out.write(data, length);
	}
}

ScreenData* ScreenData::create(fs::path dir, fs::path lowFastQ, fs::path highFastQ)
{
	return new ScreenData(dir, lowFastQ, highFastQ);
}