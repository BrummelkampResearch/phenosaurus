// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <list>
#include <filesystem>

#include <zeep/nvp.hpp>
#include <zeep/json/element.hpp> 

#include "bowtie.hpp"
#include "job-scheduler.hpp"

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

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("name", name)
		   & zeep::name_value_pair("source", source);
	}
};

struct mapped_info
{
	std::string assembly;
	unsigned trimlength;
	std::string	bowtie_version;
	std::string bowtie_params;
	std::string bowtie_index;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("assembly", assembly)
		   & zeep::name_value_pair("trim-length", trimlength)
		   & zeep::name_value_pair("bowtie-version", bowtie_version)
		   & zeep::name_value_pair("bowtie-params", bowtie_params)
		   & zeep::name_value_pair("bowtie-index", bowtie_index);
	}
};

struct screen_info
{
	std::string name;
	std::string scientist;
	ScreenType type;
	std::string detected_signal;
	std::string genotype;
	std::optional<std::string> treatment;
	std::optional<std::string> treatment_details;
	std::string cell_line;
	std::optional<std::string> description;
	bool ignore;
	boost::posix_time::ptime created;
	std::vector<std::string> groups;
	std::vector<screen_file> files;
	std::vector<mapped_info> mappedInfo;
	std::optional<job_status> status;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("name", name)
		   & zeep::name_value_pair("scientist", scientist)
		   & zeep::name_value_pair("type", type)
		   & zeep::name_value_pair("detected_signal", detected_signal)
		   & zeep::name_value_pair("genotype", genotype)
		   & zeep::name_value_pair("treatment", treatment)
		   & zeep::name_value_pair("treatment_details", treatment_details)
		   & zeep::name_value_pair("cell_line", cell_line)
		   & zeep::name_value_pair("description", description)
		   & zeep::name_value_pair("ignore", ignore)
		   & zeep::name_value_pair("created", created)
		   & zeep::name_value_pair("groups", groups)
		   & zeep::name_value_pair("files", files)
		   & zeep::name_value_pair("mapped", mappedInfo)
		   & zeep::name_value_pair("status", status);
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

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("pv", pv)
		   & zeep::make_nvp("fcpv", fcpv)
		   & zeep::make_nvp("mi", mi)
		   & zeep::make_nvp("low", low)
		   & zeep::make_nvp("high", high);
	}
};

// --------------------------------------------------------------------

struct SLDataReplicate
{
	double binom_fdr;
	float ref_pv[4];
	float ref_fcpv[4];
	uint32_t sense, sense_normalized, antisense, antisense_normalized;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("binom_fdr", binom_fdr)
		   & zeep::make_nvp("ref_pv", ref_pv)
		   & zeep::make_nvp("ref_fcpv", ref_fcpv)
		   & zeep::make_nvp("sense", sense)
		   & zeep::make_nvp("antisense", antisense)
		   & zeep::make_nvp("sense_normalized", sense_normalized)
		   & zeep::make_nvp("antisense_normalized", antisense_normalized);
	}
};

struct SLDataPoint
{
	std::string gene;
	float oddsRatio;
	float senseRatio;
	float controlBinom;
	float controlSenseRatio;
	std::vector<SLDataReplicate> replicates;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("odds_ratio", oddsRatio)
		   & zeep::make_nvp("sense_ratio", senseRatio)
		   & zeep::make_nvp("control_binom", controlBinom)
		   & zeep::make_nvp("control_sense_ratio", controlSenseRatio)
		   & zeep::make_nvp("replicate", replicates);
	}
};

// --------------------------------------------------------------------

struct GeneExon
{
	uint32_t start, end;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("start", start)
		   & zeep::make_nvp("end", end);
	}	
};

// --------------------------------------------------------------------

struct Gene
{
	std::string geneName;
	std::string strand;
	uint32_t txStart, txEnd, cdsStart, cdsEnd;
	std::vector<GeneExon> utr3, exons, utr5;
	
	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("name", geneName)
		   & zeep::make_nvp("strand", strand)
		   & zeep::make_nvp("txStart", txStart)
		   & zeep::make_nvp("txEnd", txEnd)
		   & zeep::make_nvp("cdsStart", cdsStart)
		   & zeep::make_nvp("cdsEnd", cdsEnd)
		   & zeep::make_nvp("utr3", utr3)
		   & zeep::make_nvp("exons", exons)
		   & zeep::make_nvp("utr5", utr5);
	}	
};

// --------------------------------------------------------------------

struct InsertionInfo
{
	std::string strand;
	std::string name;
	std::vector<uint32_t> pos;

	InsertionInfo() = default;
	InsertionInfo(const InsertionInfo&) = default;
	InsertionInfo(InsertionInfo&&) = default;

	InsertionInfo& operator=(const InsertionInfo&) = default;
	InsertionInfo& operator=(InsertionInfo&&) = default;

	InsertionInfo(const std::string& strand, const std::string& name)
		: strand(strand), name(name) {}
	InsertionInfo(const std::string& strand, const std::string& name, std::vector<uint32_t>&& pos)
		: strand(strand), name(name), pos(std::move(pos)) {}

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("strand", strand)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("pos", pos);
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

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("chrom", chrom)
		   & zeep::make_nvp("start", start)
		   & zeep::make_nvp("end", end)
		   & zeep::make_nvp("geneStrand", geneStrand)
		   & zeep::make_nvp("genes", genes)
		   & zeep::make_nvp("area", area)
		   & zeep::make_nvp("insertions", insertions);
	}
};

// --------------------------------------------------------------------

enum class Direction
{
	Sense, AntiSense, Both
};

// --------------------------------------------------------------------

class ScreenData
{
  public:
	ScreenData(const ScreenData&) = delete;
	ScreenData& operator=(const ScreenData&) = delete;
	virtual ~ScreenData() = default;

	static std::unique_ptr<ScreenData> load(const std::filesystem::path& dir);

	virtual void map(const std::string& assembly, unsigned readLength,
		std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
		unsigned threads);

	virtual void map(const std::string& assembly);

	void dump_map(const std::string& assembly, unsigned readLength, const std::string& file);
	void compress_map(const std::string& assembly, unsigned readLength, const std::string& file);

	const std::string& name() const				{ return mInfo.name; }
	ScreenType get_type() const					{ return mInfo.type; }

	virtual void addFile(const std::string& name, std::filesystem::path file);

	// convenience, should probably moved elsewhere
	static std::vector<Insertion> read_insertions(std::filesystem::path file);

	// load and save screen_info from the manifest file
	static screen_info loadManifest(const std::filesystem::path& dir);
	static void saveManifest(const screen_info& info, const std::filesystem::path& dir);

	std::istream *get_bed_file_for_insertions(const std::string& assembly, unsigned readLength, const std::string& file) const;

  protected:

	std::vector<Insertion> read_insertions(const std::string& assembly, unsigned readLength, const std::string& file) const;
	void write_insertions(const std::string& assembly, unsigned readLength, const std::string& file,
		std::vector<Insertion>& insertions);

	ScreenData(const std::filesystem::path& dir);
	ScreenData(const std::filesystem::path& dir, const screen_info& info);

	std::filesystem::path mDataDir;
	screen_info mInfo;
};

// --------------------------------------------------------------------

class IPPAScreenData : public ScreenData
{
  public:
	static std::unique_ptr<IPPAScreenData> create(const screen_info& info, const std::filesystem::path& dir);
	static std::unique_ptr<IPPAScreenData> load(const std::filesystem::path& dir)
	{
		auto result = ScreenData::load(dir);
		if (result->get_type() != ScreenType::IntracellularPhenotype and result->get_type() != ScreenType::IntracellularPhenotypeActivation)
			throw std::runtime_error("Invalid type in screen data manifest");
		return std::unique_ptr<IPPAScreenData>(static_cast<IPPAScreenData*>(result.release()));
	}

	// note: will reorder transcripts!
	void analyze(const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts,
		std::vector<Insertions>& lowInsertions, std::vector<Insertions>& highInsertions);

	std::tuple<std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>>
		insertions(const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end);

	std::vector<IPDataPoint> dataPoints(const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	std::vector<IPDataPoint> dataPoints(const std::vector<Transcript>& transcripts,
		const std::vector<Insertions>& lowInsertions, const std::vector<Insertions>& highInsertions,
		Direction direction);

  protected:

	IPPAScreenData(ScreenType type, const std::filesystem::path& dir);
	IPPAScreenData(ScreenType type, const std::filesystem::path& dir, const screen_info& info);

	ScreenType mType;
};

class IPScreenData : public IPPAScreenData
{
  public:
	static constexpr ScreenType screen_type = ScreenType::IntracellularPhenotype;

	IPScreenData(const std::filesystem::path& dir)
		: IPPAScreenData(ScreenType::IntracellularPhenotype, dir) {}
	IPScreenData(const std::filesystem::path& dir, const screen_info& info)
		: IPPAScreenData(ScreenType::IntracellularPhenotype, dir, info) {}
};

class PAScreenData : public IPPAScreenData
{
  public:
	static constexpr ScreenType screen_type = ScreenType::IntracellularPhenotypeActivation;

	PAScreenData(const std::filesystem::path& dir)
		: IPPAScreenData(ScreenType::IntracellularPhenotypeActivation, dir) {}
	PAScreenData(const std::filesystem::path& dir, const screen_info& info)
		: IPPAScreenData(ScreenType::IntracellularPhenotypeActivation, dir, info) {}
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

	SLScreenData(const std::filesystem::path& dir);
	SLScreenData(const std::filesystem::path& dir, const screen_info& info);

	static std::unique_ptr<IPPAScreenData> create(const screen_info& info, const std::filesystem::path& dir);

	std::vector<SLDataPoint> dataPoints(const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts, const SLScreenData& controlData, unsigned groupSize);

	std::vector<std::string> getReplicateNames() const;
	std::tuple<std::vector<uint32_t>,std::vector<uint32_t>> getInsertionsForReplicate(
		const std::string& replicate, const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end) const;

  private:

	std::vector<InsertionCount> normalize(const std::vector<InsertionCount>& counts,
		const std::array<std::vector<InsertionCount>,4>& controlInsertions, unsigned groupSize);

	void count_insertions(const std::string& replicate, const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts, std::vector<InsertionCount>& insertions) const;

	std::vector<SLDataReplicate> dataPoints(const std::vector<Transcript>& transcripts,
		const std::vector<InsertionCount>& insertions,
		const std::array<std::vector<InsertionCount>,4>& controlInsertions, unsigned groupSize);

	// std::vector<SLDataPoint> dataPoints(const std::vector<Transcript>& transcripts,
	// 	const std::vector<InsertionCount>& insertions,
	// 	const std::array<std::vector<InsertionCount>,4>& controlInsertions, unsigned groupSize);
};