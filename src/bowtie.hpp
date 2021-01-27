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

// --------------------------------------------------------------------

class bowtie_parameters
{
  public:

	static bowtie_parameters& instance()
	{
		if (not s_instance)
			throw std::logic_error("You should initialize the bowtie parameters before using them");
		return *s_instance;
	}

	static void init(std::filesystem::path bowtie, unsigned threads, unsigned trimLength,
		const std::map<std::string,std::filesystem::path>& assemblyIndices)
	{
		s_instance.reset(new bowtie_parameters(bowtie, threads, trimLength, assemblyIndices));
	}

	std::filesystem::path	bowtie() const				{ return m_bowtie; }
	std::filesystem::path	bowtieIndex(const std::string& assembly) const
														{ return m_assemblyIndices.at(assembly); }
	unsigned				threads() const				{ return m_threads; }
	unsigned				trimLength() const			{ return m_trimLength; }

  private:

	bowtie_parameters(std::filesystem::path bowtie, unsigned threads, unsigned trimLength,
		const std::map<std::string,std::filesystem::path>& assemblyIndices)
		: m_bowtie(bowtie), m_threads(threads), m_trimLength(trimLength), m_assemblyIndices(assemblyIndices) {}

	static std::unique_ptr<bowtie_parameters> s_instance;

	std::filesystem::path m_bowtie;
	unsigned m_threads;
	unsigned m_trimLength;
	std::map<std::string,std::filesystem::path> m_assemblyIndices;
};

// --------------------------------------------------------------------

/// \brief Return the version string of the specified bowtie executable
std::string bowtieVersion(std::filesystem::path bowtie);

/// \brief First version of runBowtie, with all the possible parameters
std::vector<Insertion> runBowtie(const std::filesystem::path& bowtie,
	const std::filesystem::path& bowtieIndex, const std::filesystem::path& fastq,
	const std::filesystem::path& logFile, unsigned threads, unsigned trimLength);

// /// \brief Alternative for runBowtie, using predefined parameters
// std::vector<Insertion> runBowtie(const std::string& assembly, std::filesystem::path fastq);

// --------------------------------------------------------------------

namespace std
{
template<std::size_t N>
struct tuple_element<N, Insertion> {
	using type = decltype(std::declval<Insertion>().get<N>());
};
}