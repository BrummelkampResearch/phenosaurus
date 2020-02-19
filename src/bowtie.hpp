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
	int32_t		pos;

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

	template<size_t N>
	decltype(auto) get() const
	{
		     if constexpr (N == 0) return chr;
		else if constexpr (N == 1) return strand;
		else if constexpr (N == 2) return pos;
	}

};

std::vector<Insertion> runBowtie(std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
	std::filesystem::path fastq, unsigned threads, unsigned trimLength);

namespace std
{

template<> struct tuple_size<::Insertion>
            : public std::integral_constant<std::size_t, 3> {};

template<> struct tuple_element<0, ::Insertion>
{
	using type = decltype(std::declval<::Insertion>().chr);
};

template<> struct tuple_element<1, ::Insertion>
{
	using type = decltype(std::declval<::Insertion>().strand);
};

template<> struct tuple_element<2, ::Insertion>
{
	using type = decltype(std::declval<::Insertion>().pos);
};

}