// copyright 2020 M.L. Hekkelman, NKI/AVL
//
//	module to run bowtie and process results

#pragma once

#include <set>
#include <filesystem>

#include "refann.hpp"

// struct Insertion
// {
// 	long pos;
// 	const Transcript* transcript;
// 	bool sense;
// };

struct Insertions
{
	std::set<long> sense, antiSense;
};

std::vector<Insertions> assignInsertions(std::istream& data, const std::vector<Transcript>& transcripts);

std::vector<Insertions> assignInsertions(const std::string& bowtie,
	const std::string& index, const std::string& fastq,
	const std::vector<Transcript>& transcripts, size_t nrOfThreads);

// --------------------------------------------------------------------

struct Insertion
{
	CHROM		chr;
	char		strand;
	uint16_t	filler = 0;
	uint32_t	pos;

	bool operator<(const Insertion& rhs) const
	{
		int d = chr - rhs.chr;
		if (d == 0)
			d = pos - rhs.pos;
		if (d == 0)
			d = strand - rhs.strand;
		return d < 0;
	}

	bool operator==(const Insertion& rhs) const
	{
		return chr == rhs.chr and pos == rhs.pos and strand == rhs.strand;
	}
};

std::vector<Insertion> runBowtie(std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
	std::filesystem::path fastq, unsigned threads, unsigned readLength);
