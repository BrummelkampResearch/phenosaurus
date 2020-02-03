// copyright 2020 M.L. Hekkelman, NKI/AVL
//
//	module to run bowtie and process results

#pragma once

#include "refann.hpp"

struct Insertion
{
	long pos;
	const Transcript* transcript;
	bool sense;
};

struct GeneInsertions
{
	size_t transcriptIndex;
	std::vector<long> sense, antiSense;
	float pValue;
	float fdrCorrectedPValue;
};


std::vector<Insertion> assignInsertions(std::istream& data, const std::vector<Transcript>& transcripts);

std::vector<Insertion> assignInsertions(const std::string& bowtie,
	const std::string& index, const std::string& fastq,
	const std::vector<Transcript>& transcripts);
