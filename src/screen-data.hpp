/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "bowtie.hpp"
#include "job-scheduler.hpp"

#include <filesystem>
#include <zeep/el/object.hpp>

// --------------------------------------------------------------------

void checkIsFastQ(std::filesystem::path infile);

// --------------------------------------------------------------------

enum class ScreenType
{
	Unspecified,

	IntracellularPhenotype,
	SyntheticLethal,
	IntracellularPhenotypeActivation,

	// abreviated synonyms
	IP = IntracellularPhenotype,
	SL = SyntheticLethal,
	PA = IntracellularPhenotypeActivation
};

// --------------------------------------------------------------------

struct screen_file
{
	std::string name;
	std::string source;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar &zeem::name_value_pair("name", name) //
			& zeem::name_value_pair("source", source);
	}
};

struct screen_insertion_count
{
	std::string file;
	uint32_t count;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar &zeem::name_value_pair("file", file) //
			& zeem::name_value_pair("count", count);
	}
};

struct screen_description
{
	std::string description;
	std::vector<screen_insertion_count> counts;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar &zeem::name_value_pair("description", description) //
			& zeem::name_value_pair("count", counts);
	}
};

struct mapped_info
{
	std::string assembly;
	unsigned trimlength;
	std::string bowtie_version;
	std::string bowtie_params;
	std::string bowtie_index;
	std::vector<screen_insertion_count> file;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar &zeem::name_value_pair("assembly", assembly)               //
			& zeem::name_value_pair("trim-length", trimlength)        //
			& zeem::name_value_pair("bowtie-version", bowtie_version) //
			& zeem::name_value_pair("bowtie-params", bowtie_params)   //
			& zeem::name_value_pair("bowtie-index", bowtie_index)     //
			& zeem::name_value_pair("insertion-counts", file);
	}
};

struct screen_info
{
	std::string name;
	std::optional<std::string> published_name;
	std::string scientist;
	ScreenType type;
	std::string detected_signal;
	std::string genotype;
	std::optional<std::string> treatment;
	std::optional<std::string> treatment_details;
	std::string cell_line;
	std::optional<std::string> description;
	bool ignore;
	std::chrono::time_point<std::chrono::system_clock> created;
	std::vector<std::string> groups;
	std::vector<screen_file> files;
	std::vector<mapped_info> mappedInfo;
	std::optional<job_status> status;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar &zeem::name_value_pair("name", name)                             //
			& zeem::name_value_pair("published_name", published_name)       //
			& zeem::name_value_pair("scientist", scientist)                 //
			& zeem::name_value_pair("type", type)                           //
			& zeem::name_value_pair("detected_signal", detected_signal)     //
			& zeem::name_value_pair("genotype", genotype)                   //
			& zeem::name_value_pair("treatment", treatment)                 //
			& zeem::name_value_pair("treatment_details", treatment_details) //
			& zeem::name_value_pair("cell_line", cell_line)                 //
			& zeem::name_value_pair("description", description)             //
			& zeem::name_value_pair("ignore", ignore)                       //
			& zeem::name_value_pair("created", created)                     //
			& zeem::name_value_pair("groups", groups)                       //
			& zeem::name_value_pair("files", files)                         //
			& zeem::name_value_pair("mapped", mappedInfo)                   //
			& zeem::name_value_pair("status", status);
	}
};

// --------------------------------------------------------------------

struct IPDataPoint
{
	std::string gene;
	float pv;
	float fcpv;
	float mi;
	int low;
	int high;
};

// --------------------------------------------------------------------

struct SLDataReplicate
{
	float binom_fdr;
	float ref_pv[4];
	uint32_t sense, sense_normalized, antisense, antisense_normalized;
};

struct SLDataPoint
{
	std::string gene;
	float oddsRatio;
	float senseRatio;
	float controlBinom;
	float controlSenseRatio;
	bool consistent;
	std::vector<SLDataReplicate> replicates;
};

// --------------------------------------------------------------------

struct GeneExon
{
	uint32_t start, end;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar &zeem::name_value_pair("start", start) //
			& zeem::name_value_pair("end", end);
	}
};

// --------------------------------------------------------------------

struct Gene
{
	std::string geneName;
	std::string strand;
	uint32_t txStart, txEnd, cdsStart, cdsEnd;
	std::vector<GeneExon> utr3, exons, utr5;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar &zeem::name_value_pair("name", geneName)       //
			& zeem::name_value_pair("strand", strand)     //
			& zeem::name_value_pair("txStart", txStart)   //
			& zeem::name_value_pair("txEnd", txEnd)       //
			& zeem::name_value_pair("cdsStart", cdsStart) //
			& zeem::name_value_pair("cdsEnd", cdsEnd)     //
			& zeem::name_value_pair("utr3", utr3)         //
			& zeem::name_value_pair("exons", exons)       //
			& zeem::name_value_pair("utr5", utr5);
	}
};

// --------------------------------------------------------------------

struct InsertionInfo
{
	std::string strand;
	std::string name;
	std::vector<uint32_t> pos;

	InsertionInfo() = default;
	InsertionInfo(const InsertionInfo &) = default;
	InsertionInfo(InsertionInfo &&) = default;

	InsertionInfo &operator=(const InsertionInfo &) = default;
	InsertionInfo &operator=(InsertionInfo &&) = default;

	InsertionInfo(const std::string &strand, const std::string &name)
		: strand(strand)
		, name(name)
	{
	}
	InsertionInfo(const std::string &strand, const std::string &name, std::vector<uint32_t> &&pos)
		: strand(strand)
		, name(name)
		, pos(std::move(pos))
	{
	}

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar &zeem::name_value_pair("strand", strand) //
			& zeem::name_value_pair("name", name)   //
			& zeem::name_value_pair("pos", pos);
	}
};

struct Region
{
	CHROM chrom;
	uint32_t start, end;
	std::string geneStrand;
	std::vector<GeneExon> area;
	std::vector<Gene> genes;
	std::vector<InsertionInfo> insertions;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar &zeem::name_value_pair("chrom", chrom)             //
			& zeem::name_value_pair("start", start)           //
			& zeem::name_value_pair("end", end)               //
			& zeem::name_value_pair("geneStrand", geneStrand) //
			& zeem::name_value_pair("genes", genes)           //
			& zeem::name_value_pair("area", area)             //
			& zeem::name_value_pair("insertions", insertions);
	}
};

// --------------------------------------------------------------------

enum class Direction
{
	Sense,
	AntiSense,
	Both
};

// --------------------------------------------------------------------

class ScreenData
{
  public:
	ScreenData(const ScreenData &) = delete;
	ScreenData &operator=(const ScreenData &) = delete;
	virtual ~ScreenData() = default;

	static std::unique_ptr<ScreenData> load(const std::filesystem::path &dir);

	virtual void map(const std::string &assembly, unsigned readLength,
		std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
		unsigned threads);

	virtual void map(const std::string &assembly);

	void dump_map(const std::string &assembly, unsigned readLength, const std::string &file);
	void compress_map(const std::string &assembly, unsigned readLength, const std::string &file);

	const std::string &name() const { return mInfo.name; }
	ScreenType get_type() const { return mInfo.type; }

	virtual void addFile(const std::string &name, std::filesystem::path file);

	// convenience, should probably moved elsewhere
	static std::vector<Insertion> read_insertions(std::filesystem::path file);
	static uint32_t count_insertions(std::filesystem::path file);
	static uint32_t count_insertions(const std::string &assembly, unsigned readLength, const std::string &file);

	// load and save screen_info from the manifest file
	static screen_info loadManifest(const std::filesystem::path &dir);
	static void saveManifest(const screen_info &info, const std::filesystem::path &dir);
	static void refreshManifest(screen_info &info, const std::filesystem::path &dir);

	std::unique_ptr<std::istream> get_bed_file_for_insertions(const std::string &assembly, unsigned readLength, const std::string &file) const;

  protected:
	std::vector<Insertion> read_insertions(const std::string &assembly, unsigned readLength, const std::string &file) const;
	void write_insertions(const std::string &assembly, unsigned readLength, const std::string &file,
		std::vector<Insertion> &insertions);

	ScreenData(const std::filesystem::path &dir);
	ScreenData(const std::filesystem::path &dir, const screen_info &info);

	std::filesystem::path mDataDir;
	screen_info mInfo;
};

// --------------------------------------------------------------------

class IPPAScreenData : public ScreenData
{
  public:
	static std::unique_ptr<IPPAScreenData> create(const screen_info &info, const std::filesystem::path &dir);
	static std::unique_ptr<IPPAScreenData> load(const std::filesystem::path &dir)
	{
		auto result = ScreenData::load(dir);
		if (result->get_type() != ScreenType::IntracellularPhenotype and result->get_type() != ScreenType::IntracellularPhenotypeActivation)
			throw std::runtime_error("Invalid type in screen data manifest");
		return std::unique_ptr<IPPAScreenData>(static_cast<IPPAScreenData *>(result.release()));
	}

	// note: will reorder transcripts!
	void analyze(const std::string &assembly, unsigned readLength,
		const std::vector<Transcript> &transcripts,
		std::vector<Insertions> &lowInsertions, std::vector<Insertions> &highInsertions);

	std::tuple<std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>>
	insertions(const std::string &assembly, CHROM chrom, uint32_t start, uint32_t end);

	std::vector<IPDataPoint> dataPoints(const std::string &assembly, const std::string &transcripts, Mode mode,
		bool cutOverlap, const std::string &geneStart, const std::string &geneEnd,
		Direction direction);

	std::vector<IPDataPoint> dataPoints(const std::vector<Transcript> &transcripts,
		const std::vector<Insertions> &lowInsertions, const std::vector<Insertions> &highInsertions,
		Direction direction);

  protected:
	IPPAScreenData(ScreenType type, const std::filesystem::path &dir);
	IPPAScreenData(ScreenType type, const std::filesystem::path &dir, const screen_info &info);

	ScreenType mType;
};

class IPScreenData : public IPPAScreenData
{
  public:
	static constexpr ScreenType screen_type = ScreenType::IntracellularPhenotype;

	IPScreenData(const std::filesystem::path &dir)
		: IPPAScreenData(ScreenType::IntracellularPhenotype, dir)
	{
	}
	IPScreenData(const std::filesystem::path &dir, const screen_info &info)
		: IPPAScreenData(ScreenType::IntracellularPhenotype, dir, info)
	{
	}
};

class PAScreenData : public IPPAScreenData
{
  public:
	static constexpr ScreenType screen_type = ScreenType::IntracellularPhenotypeActivation;

	PAScreenData(const std::filesystem::path &dir)
		: IPPAScreenData(ScreenType::IntracellularPhenotypeActivation, dir)
	{
	}
	PAScreenData(const std::filesystem::path &dir, const screen_info &info)
		: IPPAScreenData(ScreenType::IntracellularPhenotypeActivation, dir, info)
	{
	}
};

// --------------------------------------------------------------------

struct InsertionCount
{
	size_t sense, antiSense;
};

class SLScreenData : public ScreenData
{
  public:
	static constexpr ScreenType screen_type = ScreenType::SyntheticLethal;

	SLScreenData(const std::filesystem::path &dir);
	SLScreenData(const std::filesystem::path &dir, const screen_info &info);

	static std::unique_ptr<IPPAScreenData> create(const screen_info &info, const std::filesystem::path &dir);

	std::array<std::vector<InsertionCount>, 4> loadNormalizedInsertions(const std::string &assembly, unsigned readLength,
		const std::vector<Transcript> &transcripts, unsigned groupSize) const;

	std::vector<SLDataPoint> dataPoints(const std::string &assembly, unsigned readLength,
		const std::vector<Transcript> &transcripts, const std::array<std::vector<InsertionCount>, 4> &controlInsertions, unsigned groupSize,
		bool normalize_counts);

	std::vector<SLDataPoint> dataPoints(const std::string &assembly, unsigned readLength,
		const std::vector<Transcript> &transcripts, const SLScreenData &controlData, unsigned groupSize, bool normalize_counts);

	std::vector<std::string> getReplicateNames() const;
	std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getInsertionsForReplicate(
		const std::string &replicate, const std::string &assembly, CHROM chrom, uint32_t start, uint32_t end) const;

  private:
	static std::vector<InsertionCount> normalize(const std::vector<InsertionCount> &counts,
		const std::array<std::vector<InsertionCount>, 4> &controlInsertions, unsigned groupSize);

	void count_insertions(const std::string &replicate, const std::string &assembly, unsigned readLength,
		const std::vector<Transcript> &transcripts, std::vector<InsertionCount> &insertions) const;

	std::vector<SLDataReplicate> dataPoints(const std::vector<Transcript> &transcripts,
		const std::vector<InsertionCount> &insertions,
		const std::array<std::vector<InsertionCount>, 4> &controlInsertions, unsigned groupSize,
		bool normalize_counts);

	// std::vector<SLDataPoint> dataPoints(const std::vector<Transcript>& transcripts,
	// 	const std::vector<InsertionCount>& insertions,
	// 	const std::array<std::vector<InsertionCount>,4>& controlInsertions, unsigned groupSize);
};
