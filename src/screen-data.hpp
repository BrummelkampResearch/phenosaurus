// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <list>
#include <filesystem>

#include <zeep/nvp.hpp>
#include <zeep/json/element.hpp> 

#include "bowtie.hpp"

// --------------------------------------------------------------------

enum class ScreenType
{
	Unspecified,

	IntracellularPhenotype,
	SyntheticLethal,

	// abreviated synonyms
	IP = IntracellularPhenotype,
	SL = SyntheticLethal
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

struct screen_info
{
	std::string name;
	ScreenType type;
	std::string cell_line;
	std::string description;
	std::string long_description;
	bool induced, knockout, ignore;
	std::string scientist;
	boost::posix_time::ptime created;
	std::vector<std::string> groups;
	std::vector<screen_file> files;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("name", name)
		   & zeep::name_value_pair("type", type)
		   & zeep::name_value_pair("cell_line", cell_line)
		   & zeep::name_value_pair("description", description)
		   & zeep::name_value_pair("long_description", long_description)
		   & zeep::name_value_pair("induced", induced)
		   & zeep::name_value_pair("knockout", knockout)
		   & zeep::name_value_pair("ignore", ignore)
		   & zeep::name_value_pair("scientist", scientist)
		   & zeep::name_value_pair("groups", groups)
		   & zeep::name_value_pair("created", created)
		   & zeep::name_value_pair("files", files);
	}
};

// --------------------------------------------------------------------

struct IPDataPoint
{
	int geneID;
	std::string geneName;
	float pv;
	float fcpv;
	float mi;
	int low;
	int high;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("geneID", geneID)
		   & zeep::make_nvp("geneName", geneName)
		   & zeep::make_nvp("pv", pv)
		   & zeep::make_nvp("fcpv", fcpv)
		   & zeep::make_nvp("mi", mi)
		   & zeep::make_nvp("low", low)
		   & zeep::make_nvp("high", high);
	}
};

// --------------------------------------------------------------------

struct SLDataPoint
{
	int geneID;
	std::string geneName;
	float pv;
	float fcpv;
	float ref_pv[4];
	float ref_fcpv[4];
	int sense, sense_normalized;
	int antisense, antisense_normalized;

	float senseratio;
	int insertions;

	bool significant;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("geneID", geneID)
		   & zeep::make_nvp("geneName", geneName)
		   & zeep::make_nvp("pv", pv)
		   & zeep::make_nvp("binom_fdr", fcpv)
		   & zeep::make_nvp("ref_pv", ref_pv)
		   & zeep::make_nvp("ref_fcpv", ref_fcpv)
		   & zeep::make_nvp("sense", sense_normalized)
		   & zeep::make_nvp("antisense", antisense_normalized)
		   & zeep::make_nvp("senseratio", senseratio)
		   & zeep::make_nvp("insertions", insertions)
		   & zeep::make_nvp("significant", significant);
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
	int start, end;
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

	void dump_map(const std::string& assembly, unsigned readLength, const std::string& file);
	void compress_map(const std::string& assembly, unsigned readLength, const std::string& file);

	ScreenType get_type() const					{ return mInfo.type; }

  protected:

	std::vector<Insertion> read_insertions(const std::string& assembly, unsigned readLength, const std::string& file) const;
	void write_insertions(const std::string& assembly, unsigned readLength, const std::string& file,
		std::vector<Insertion>& insertions);

	ScreenData(const std::filesystem::path& dir);
	ScreenData(const std::filesystem::path& dir, const screen_info& info);

	std::filesystem::path	mDataDir;
	screen_info mInfo;
};

// --------------------------------------------------------------------

class IPScreenData : public ScreenData
{
  public:
	static constexpr ScreenType screen_type = ScreenType::IntracellularPhenotype;

	IPScreenData(const std::filesystem::path& dir);
	IPScreenData(const std::filesystem::path& dir, const screen_info& info,
		std::filesystem::path low, std::filesystem::path high);

	static std::unique_ptr<IPScreenData> create(const screen_info& info, const std::filesystem::path& dir);

	// note: will reorder transcripts!
	void analyze(const std::string& assembly, unsigned readLength,
		std::vector<Transcript>& transcripts,
		std::vector<Insertions>& lowInsertions, std::vector<Insertions>& highInsertions);

	std::tuple<std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>, std::vector<uint32_t>>
		insertions(const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end);

	std::vector<IPDataPoint> dataPoints(const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	std::vector<IPDataPoint> dataPoints(const std::vector<Transcript>& transcripts,
		const std::vector<Insertions>& lowInsertions, const std::vector<Insertions>& highInsertions,
		Direction direction);
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

	static std::unique_ptr<IPScreenData> create(const screen_info& info, const std::filesystem::path& dir);

	void addFile(std::filesystem::path file);

	std::vector<SLDataPoint> dataPoints(int replicate, const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts, const SLScreenData& controlData, unsigned groupSize,
		float pvCutOff, float binomCutOff, float effectSize);

  private:

	std::vector<InsertionCount> normalize(const std::vector<InsertionCount>& counts,
		const std::array<std::vector<InsertionCount>,4>& controlInsertions, unsigned groupSize);

	void count_insertions(int replicate, const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts, std::vector<InsertionCount>& insertions) const;

	std::vector<SLDataPoint> dataPoints(const std::vector<Transcript>& transcripts,
		const std::vector<InsertionCount>& insertions,
		const std::array<std::vector<InsertionCount>,4>& controlInsertions, unsigned groupSize,
		float pvCutOff, float binomCutOff, float effectSize);
};