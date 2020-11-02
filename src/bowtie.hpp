// copyright 2020 M.L. Hekkelman, NKI/AVL
//
//	module to run bowtie and process results

#pragma once

#include <set>
#include <filesystem>

#include "refseq.hpp"

struct Insertions
{
	std::set<long> sense, antiSense;
};

// --------------------------------------------------------------------

struct Insertion
{
  public:
	CHROM		chr;
	char		strand;
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

	template<size_t N>
	decltype(auto) get() const
	{
		     if constexpr (N == 0) return chr;
		else if constexpr (N == 1) return strand;
		else if constexpr (N == 2) return pos;
	}

};

static_assert(sizeof(Insertion) == 8);

std::vector<Insertion> runBowtie(std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
	std::filesystem::path fastq, unsigned threads, unsigned trimLength);

std::string bowtieVersion(std::filesystem::path bowtie);

namespace std
{
template<std::size_t N>
struct tuple_element<N, Insertion> {
	using type = decltype(std::declval<Insertion>().get<N>());
};
}