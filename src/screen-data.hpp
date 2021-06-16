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

struct SLDataPoint
{
	std::string gene;
	double binom_fdr;
	float ref_pv[4];
	float ref_fcpv[4];
	uint32_t sense, sense_normalized, antisense, antisense_normalized;
	float strength = 0;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("binom_fdr", binom_fdr)
		   & zeep::make_nvp("ref_pv", ref_pv)
		   & zeep::make_nvp("ref_fcpv", ref_fcpv)
		   & zeep::make_nvp("sense", sense)
		   & zeep::make_nvp("antisense", antisense)
		   & zeep::make_nvp("sense_normalized", sense_normalized)
		   & zeep::make_nvp("antisense_normalized", antisense_normalized);
	}
};

struct SLDataReplicate
{
	std::string name;
	std::vector<SLDataPoint> data;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("name", name)
		   & zeep::make_nvp("data", data);
	}
};

struct SLDataResult
{
	std::vector<SLDataReplicate> replicate;
	std::set<std::string> significant;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("replicate", replicate)
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

	SLDataResult dataPoints(const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts, const SLScreenData& controlData, unsigned groupSize,
		float pvCutOff, float binomCutOff, float effectSize);

	std::vector<std::string> getReplicateNames() const;
	std::tuple<std::vector<uint32_t>,std::vector<uint32_t>> getInsertionsForReplicate(
		const std::string& replicate, const std::string& assembly, CHROM chrom, uint32_t start, uint32_t end) const;

//   private:

	// template<size_t NC>
	// static std::vector<InsertionCount> normalize(const std::vector<InsertionCount>& counts,
	//  	const std::array<std::vector<InsertionCount>,NC>& controlInsertions, unsigned groupSize);

	void count_insertions(const std::string& replicate, const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts, std::vector<InsertionCount>& insertions) const;

	template<size_t NC>
	static std::vector<SLDataPoint> dataPoints(const std::vector<Transcript>& transcripts,
		const std::vector<InsertionCount>& insertions,
		const std::array<std::vector<InsertionCount>,NC>& controlInsertions, unsigned groupSize);

	template<size_t NC>
	friend SLDataResult dataPoints(const std::vector<Transcript>& transcripts,
		const std::array<std::vector<InsertionCount>, 3>& insertions,
		const std::array<std::vector<InsertionCount>, NC>& controlInsertions,
		unsigned groupSize, float pvCutOff, float binomCutOff, float effectSize);
};

template<size_t NC>
SLDataResult dataPoints(const std::vector<Transcript>& transcripts,
	const std::array<std::vector<InsertionCount>, 3>& insertions,
	const std::array<std::vector<InsertionCount>, NC>& controlInsertions,
	unsigned groupSize, float pvCutOff, float binomCutOff, float effectSize);


#include "binom.hpp"
#include "fisher.hpp"


std::vector<std::tuple<size_t, size_t>> divide(size_t listsize, size_t suggested_groupsize);


template<size_t NC>
std::vector<InsertionCount> normalize(const std::vector<InsertionCount> &insertions,
	const std::array<std::vector<InsertionCount>, NC> &controlInsertions, unsigned groupSize)
{
	std::vector<double> senseRatio(insertions.size()), refSenseRatio(insertions.size());
	std::vector<InsertionCount> result(insertions);

	parallel_for(insertions.size(), [&](size_t i) {
		int sense = insertions[i].sense;
		int antisense = insertions[i].antiSense;

		// if (sense + antisense >= 20 and
		// 	((controlInsertions[0][i].sense + controlInsertions[0][i].antiSense) >= 20) and
		// 	((controlInsertions[1][i].sense + controlInsertions[1][i].antiSense) >= 20) and
		// 	((controlInsertions[2][i].sense + controlInsertions[2][i].antiSense) >= 20) and
		// 	((controlInsertions[3][i].sense + controlInsertions[3][i].antiSense) >= 20))
		if (sense + antisense >= 1 and
			std::accumulate(controlInsertions.begin(), controlInsertions.end(), 0L,
				[i](size_t sum, const std::vector<InsertionCount> &v)
				{
					return sum + (v[i].sense + v[i].antiSense > 1 ? 1 : 0);
				}) == NC)
			// ((controlInsertions[0][i].sense + controlInsertions[0][i].antiSense) >= 1) and
			// ((controlInsertions[1][i].sense + controlInsertions[1][i].antiSense) >= 1) and
			// ((controlInsertions[2][i].sense + controlInsertions[2][i].antiSense) >= 1) and
			// ((controlInsertions[3][i].sense + controlInsertions[3][i].antiSense) >= 1))
		{
			int ref_sense, ref_antisense;
			std::tie(ref_sense, ref_antisense) = std::accumulate(controlInsertions.begin(), controlInsertions.end(), std::make_tuple(0, 0),
				[i](std::tuple<int,int> sum, const std::vector<InsertionCount> &v)
				{
					return std::make_tuple(
						std::get<0>(sum) + v[i].sense,
						std::get<1>(sum) + v[i].antiSense
					);
				});

			// int ref_sense =
			// 	controlInsertions[0][i].sense +
			// 	controlInsertions[1][i].sense +
			// 	controlInsertions[2][i].sense +
			// 	controlInsertions[3][i].sense;

			// int ref_antisense =
			// 	controlInsertions[0][i].antiSense +
			// 	controlInsertions[1][i].antiSense +
			// 	controlInsertions[2][i].antiSense +
			// 	controlInsertions[3][i].antiSense;

			senseRatio[i] = (sense + 1.0f) / (sense + antisense + 2);
			refSenseRatio[i] = (ref_sense + 1.0f) / (ref_sense + ref_antisense + 2);
		}



		// // if (sense + antisense >= 20 and
		// // 	((controlInsertions[0][i].sense + controlInsertions[0][i].antiSense) >= 20) and
		// // 	((controlInsertions[1][i].sense + controlInsertions[1][i].antiSense) >= 20) and
		// // 	((controlInsertions[2][i].sense + controlInsertions[2][i].antiSense) >= 20) and
		// // 	((controlInsertions[3][i].sense + controlInsertions[3][i].antiSense) >= 20))
		// if (sense + antisense >= 1 and
		// 	((controlInsertions[0][i].sense + controlInsertions[0][i].antiSense) >= 1) and
		// 	((controlInsertions[1][i].sense + controlInsertions[1][i].antiSense) >= 1) and
		// 	((controlInsertions[2][i].sense + controlInsertions[2][i].antiSense) >= 1) and
		// 	((controlInsertions[3][i].sense + controlInsertions[3][i].antiSense) >= 1))
		// {
		// 	int ref_sense =
		// 		controlInsertions[0][i].sense +
		// 		controlInsertions[1][i].sense +
		// 		controlInsertions[2][i].sense +
		// 		controlInsertions[3][i].sense;

		// 	int ref_antisense =
		// 		controlInsertions[0][i].antiSense +
		// 		controlInsertions[1][i].antiSense +
		// 		controlInsertions[2][i].antiSense +
		// 		controlInsertions[3][i].antiSense;

		// 	senseRatio[i] = (sense + 1.0f) / (sense + antisense + 2);
		// 	refSenseRatio[i] = (ref_sense + 1.0f) / (ref_sense + ref_antisense + 2);
		// }
	});

	// collect the datapoints with both counts in sample and in reference

	std::vector<size_t> index;
	index.reserve(insertions.size());
	for (size_t i = 0; i < insertions.size(); ++i)
	{
		if (senseRatio[i] <= 0 or refSenseRatio[i] <= 0)
			continue;

		index.push_back(i);
	}

	// sort datapoints based on ref_ratio
	std::sort(index.begin(), index.end(),
		[&refSenseRatio](size_t a, size_t b) { return refSenseRatio[a] < refSenseRatio[b]; });

	auto groups = divide(index.size(), groupSize);

	parallel_for(groups.size(), [&](size_t i) {
		const auto &[b, e] = groups[i];
		auto l = e - b;

		// calculate median ratio for sample and reference in this group
		// The median for ref can be picked up immediately since the
		// group is already sorted on this value
		double ref_median;

		if (l & 1)
		{
			auto ix = (e + b) / 2 + 1;
			ref_median = refSenseRatio[index[ix]];
		}
		else
		{
			auto ix = (e + b) / 2;
			ref_median = (refSenseRatio[index[ix]] + refSenseRatio[index[ix + 1]]) / 2.0;
		}

		// median for sample needs to be calculated
		std::vector<double> srs;
		for (auto ix = b; ix < e; ++ix)
			srs.push_back(senseRatio[index[ix]]);
		std::sort(srs.begin(), srs.end());
		double sample_median = l & 1
								   ? srs[l / 2 + 1]
								   : (srs[l / 2] + srs[l / 2 + 1]) / 2.0;

		// adjust counts
		for (size_t ix = b; ix < e; ++ix)
		{
			auto iix = index[ix];
			assert(iix < insertions.size());

			auto iSenseRatio = senseRatio[iix];

			double f = iSenseRatio <= sample_median
						   ? (ref_median * iSenseRatio) / sample_median
						   : 1 - ((1 - ref_median) * (1 - iSenseRatio)) / (1 - sample_median);

			if (f > 1)
				f = 1;

			auto total = insertions[iix].sense + insertions[iix].antiSense;
			result[iix].sense = static_cast<int>(std::round(f * (total)));
			result[iix].antiSense = total - result[iix].sense;
		}
	});

	return result;
}


template<size_t NC>
SLDataResult dataPoints(const std::vector<Transcript>& transcripts,
	const std::array<std::vector<InsertionCount>, 3>& insertions,
	const std::array<std::vector<InsertionCount>, NC>& controlInsertions,
	unsigned groupSize, float pvCutOff, float binomCutOff, float effectSize)
{
	std::exception_ptr eptr;

	std::array<std::vector<InsertionCount>, 1> normalizedControlInsertions;
	normalizedControlInsertions[0].reserve(controlInsertions[0].size());

	for (size_t i = 0; i < controlInsertions[0].size(); ++i)
	{
		normalizedControlInsertions[0].emplace_back(InsertionCount{
			controlInsertions[0][i].sense + controlInsertions[1][i].sense + controlInsertions[2][i].sense + controlInsertions[3][i].sense,
			controlInsertions[0][i].antiSense + controlInsertions[1][i].antiSense + controlInsertions[2][i].antiSense + controlInsertions[3][i].antiSense
		});
	}

	// parallel_for(4, [&](size_t i) {
	// 	try
	// 	{
	//		normalizedControlInsertions[i] = SLScreenData::normalize(controlInsertions[i],NC
	// 	catch (const std::exception &e)
	// 	{
	// 		eptr = std::current_exception();
	// 	}
	// });

	if (eptr)
		std::rethrow_exception(eptr);

	SLDataResult result;

	for (size_t i = 0; i < insertions.size(); ++i)
		result.replicate.push_back({ "replicate-" + std::to_string(i + 1) });

	parallel_for(result.replicate.size(), [&](size_t i) {
		try
		{
			auto &replicate = result.replicate[i];

			// Then load the screen data
			const std::vector<InsertionCount>& ins = insertions[i];

			// And now analyse this
			replicate.data = SLScreenData::dataPoints(transcripts, ins, normalizedControlInsertions, groupSize);

			// // remove redundant datapoints
			// replicate.data.erase(
			// 	std::remove_if(replicate.data.begin(), replicate.data.end(), [](auto& dp) { return dp.sense == 0 or dp.antisense == 0; }),
			// 	replicate.data.end());
		}
		catch (const std::exception &e)
		{
			eptr = std::current_exception();
		}
	});

	if (eptr)
		std::rethrow_exception(eptr);

	// find out which genes are significant

	std::mutex m;
	parallel_for(transcripts.size(), [&](size_t i) {
		// for (size_t i = 0; i < transcripts.size(); ++ i) {

		double minSenseRatio = std::numeric_limits<double>::max();
		for (auto &nci : normalizedControlInsertions)
		{
			auto &nc = nci[i];
			if (minSenseRatio > (nc.sense + 1.0) / (nc.sense + nc.antiSense + 2))
				minSenseRatio = (nc.sense + 1.0) / (nc.sense + nc.antiSense + 2);
		}

		auto maxSenseRatio = std::numeric_limits<double>::lowest();
		size_t n = 0;
		size_t s_g = 0, a_g = 0;

		for (auto &r : result.replicate)
		{
			auto &nc = r.data[i];

			if (nc.binom_fdr > binomCutOff)
				continue;

			// if (nc.ref_fcpv[0] > pvCutOff or nc.ref_fcpv[1] > pvCutOff or nc.ref_fcpv[2] > pvCutOff or nc.ref_fcpv[3] > pvCutOff)
			// 	continue;

			if (nc.ref_pv[0] > pvCutOff or nc.ref_pv[1] > pvCutOff or nc.ref_pv[2] > pvCutOff or nc.ref_pv[3] > pvCutOff)
				continue;

			double senseRatio = (nc.sense + 1.0) / (nc.sense + nc.antisense + 2);
			if (senseRatio >= 0.5)
				continue;

			++n;

			s_g += nc.sense_normalized;
			a_g += nc.antisense_normalized;

			if (maxSenseRatio < senseRatio)
				maxSenseRatio = senseRatio;
		}

		size_t s_wt = 0, a_wt = 0;

		for (auto &nc : normalizedControlInsertions)
		{
			s_wt += nc[i].sense;
			a_wt += nc[i].antiSense;
		}

		float strength = 0;
		if (s_g != 0)
		{
			strength = (1.0f * s_wt * a_g) / (a_wt * s_g);
			for (auto &r : result.replicate)
				r.data[i].strength = strength;
		}

		if (n == result.replicate.size() and strength >= effectSize)
		{
			std::unique_lock lock(m);
			result.significant.insert(transcripts[i].geneName);
		}

		// if (maxSenseRatio > 0 and maxSenseRatio < minSenseRatio and (minSenseRatio - maxSenseRatio) >= effectSize and minSenseRatio != 0.5)
		// {
		// 	std::unique_lock lock(m);
		// 	result.significant.insert(transcripts[i].geneName);
		// }
		// }
	});

	for (auto &r : result.replicate)
	{
		// remove redundant datapoints
		r.data.erase(
			std::remove_if(r.data.begin(), r.data.end(), [](auto &dp) { return dp.sense == 0 or dp.antisense == 0; }),
			r.data.end());
	}

	return result;
}

// --------------------------------------------------------------------

template<size_t NC>
std::vector<SLDataPoint> SLScreenData::dataPoints(const std::vector<Transcript> &transcripts,
	const std::vector<InsertionCount> &insertions,
	const std::array<std::vector<InsertionCount>, NC> &controlInsertions,
	unsigned groupSize)
{
	auto normalized = normalize(insertions, controlInsertions, groupSize);

	const size_t N = transcripts.size();
	std::vector<SLDataPoint> datapoints(N, SLDataPoint{});

	std::vector<size_t> index;
	index.reserve(N);

	for (size_t i = 0; i < N; ++i)
	{
		if (insertions[i].sense + insertions[i].antiSense > 0)
			index.push_back(i);
	}

	const size_t M = index.size();

	std::vector<double> pvalues[5];

	for (auto &pv : pvalues)
		pv.resize(M);

	// Calculate the minimal sense ratio per gene in the controls
	std::vector<double> minSenseRatio(N);
	for (auto &cdi : controlInsertions)
	{
		for (auto i : index)
		{
			auto &cd = cdi[i];
			double r = (cd.sense + 1.0f) / (cd.sense + cd.antiSense + 2);
			if (minSenseRatio[i] > r or minSenseRatio[i] == 0)
				minSenseRatio[i] = r;
		}
	}

	parallel_for(M, [&](size_t ix) {
		size_t i = index[ix];

		SLDataPoint &dp = datapoints[i];

		dp.gene = transcripts[i].geneName;
		dp.sense = insertions[i].sense;
		dp.antisense = insertions[i].antiSense;
		dp.sense_normalized = normalized[i].sense;
		dp.antisense_normalized = normalized[i].antiSense;

		// calculate p-value for insertion
		auto twoTailedPValue = binom_test(dp.sense_normalized, dp.sense_normalized + dp.antisense_normalized);
		
		pvalues[0][ix] = twoTailedPValue / 2;

		// and calculate p-values for the screen vs controls
		for (int j = 0; j < NC; ++j)
		{
			long v[2][2] = {
				{dp.sense_normalized, dp.antisense_normalized},
				{static_cast<long>(controlInsertions[j][i].sense), static_cast<long>(controlInsertions[j][i].antiSense)}};

			if (v[0][0] + v[0][1] == 0 or v[1][0] + v[1][1] == 0)
				dp.ref_pv[j] = -1;
			else
				dp.ref_pv[j] = fisherTest2x2(v);

			pvalues[j + 1][ix] = dp.ref_pv[j];
		}
	});

	std::vector<double> fcpv[NC + 1];
	parallel_for(NC + 1, [&](size_t i) {
		fcpv[i] = adjustFDR_BH(pvalues[i]);
	});

	parallel_for(M, [&](size_t ix) {
		size_t i = index[ix];

		auto &dp = datapoints[i];

		// dp.pv = pvalues[0][ix];
		dp.binom_fdr = fcpv[0][ix];

		// dp.ref_fcpv[0] = fcpv[1][ix];
		// dp.ref_fcpv[1] = fcpv[2][ix];
		// dp.ref_fcpv[2] = fcpv[3][ix];
		// dp.ref_fcpv[3] = fcpv[4][ix];
	});

	return datapoints;
}