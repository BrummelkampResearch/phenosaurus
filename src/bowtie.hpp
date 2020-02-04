// copyright 2020 M.L. Hekkelman, NKI/AVL
//
//	module to run bowtie and process results

#pragma once

#include <set>

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
