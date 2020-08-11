// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <list>
#include <filesystem>

#include <zeep/nvp.hpp>

#include "screen-service.hpp"
#include "bowtie.hpp"

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

	template<typename Type>
	static Type* create(std::filesystem::path dir)
	{
		return static_cast<Type*>(create(Type::screen_type, dir));
	}

	static ScreenData* create(ScreenType type, std::filesystem::path dir);

	static std::tuple<std::unique_ptr<ScreenData>,ScreenType> create(std::filesystem::path dir);

	virtual void map(const std::string& assembly, unsigned readLength,
		std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
		unsigned threads);

	void dump_map(const std::string& assembly, unsigned readLength, const std::string& file);
	void compress_map(const std::string& assembly, unsigned readLength, const std::string& file);

  protected:

	std::vector<Insertion> read_insertions(const std::string& assembly, unsigned readLength, const std::string& file) const;
	void write_insertions(const std::string& assembly, unsigned readLength, const std::string& file,
		std::vector<Insertion>& insertions);

	ScreenData(std::filesystem::path dir);

	std::filesystem::path	mDataDir;
};

// --------------------------------------------------------------------

class IPScreenData : public ScreenData
{
  public:
	static constexpr ScreenType screen_type = ScreenType::IntracellularPhenotype;

	IPScreenData(std::filesystem::path dir);

	void addFiles(std::filesystem::path low, std::filesystem::path high);

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

	SLScreenData(std::filesystem::path dir);

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