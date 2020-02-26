// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <iostream>

#include <zeep/http/webapp.hpp>
#include <zeep/rest/controller.hpp>

#include <tbb/parallel_for.h>

#include "screenserver.hpp"
#include "screendata.hpp"
#include "fisher.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;
namespace zh = zeep::http;

// --------------------------------------------------------------------

struct DataPoint
{
	int geneID;
	std::string geneName;
	float fcpv;
	float mi;
	int low;
	int high;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("geneID", geneID)
		   & zeep::make_nvp("geneName", geneName)
		   & zeep::make_nvp("fcpv", fcpv)
		   & zeep::make_nvp("mi", mi)
		   & zeep::make_nvp("low", low)
		   & zeep::make_nvp("high", high);
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

struct Region
{
	CHROM chrom;
	int start, end;
	std::string geneStrand;
	std::vector<GeneExon> area;
	std::vector<Gene> genes;
	std::vector<uint32_t> lowPlus, lowMinus, highPlus, highMinus;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("chrom", chrom)
		   & zeep::make_nvp("start", start)
		   & zeep::make_nvp("end", end)
		   & zeep::make_nvp("geneStrand", geneStrand)
		   & zeep::make_nvp("genes", genes)
		   & zeep::make_nvp("area", area)
		   & zeep::make_nvp("lowPlus", lowPlus)
		   & zeep::make_nvp("lowMinus", lowMinus)
		   & zeep::make_nvp("highPlus", highPlus)
		   & zeep::make_nvp("highMinus", highMinus);
	}	
};

// -----------------------------------------------------------------------

void to_element(zeep::el::element& e, Mode mode)
{
	switch (mode)
	{
		case Mode::Collapse:	e = "collapse"; break;
		case Mode::Longest:		e = "longest"; break;
		case Mode::Start:		e = "start"; break;
		case Mode::End:			e = "end"; break;
	}
}

void from_element(const zeep::el::element& e, Mode& mode)
{
	if (e == "collapse")		mode = Mode::Collapse;
	else if (e == "longest")	mode = Mode::Longest;
	else if (e == "start")		mode = Mode::Start;
	else if (e == "end")		mode = Mode::End;
	else throw std::runtime_error("Invalid mode");
}

// --------------------------------------------------------------------

class ScreenRestController : public zh::rest_controller
{
  public:
	ScreenRestController(const fs::path& screenDir)
		: zh::rest_controller("ajax")
		, mScreenDir(screenDir)
	{
		map_get_request("screenData/{id}", &ScreenRestController::screenData, "id");
		map_post_request("screenData/{id}", &ScreenRestController::screenDataEx,
			"id", "assembly", "mode", "cut-overlap", "gene-start", "gene-end");
		map_post_request("gene-info/{id}", &ScreenRestController::geneInfo, "id", "screen", "assembly", "mode", "cut-overlap", "gene-start", "gene-end");
	}

	std::vector<DataPoint> screenData(const std::string& screen);
	std::vector<DataPoint> screenDataEx(const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	Region geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
};

std::vector<DataPoint> ScreenRestController::screenDataEx(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
{
	const unsigned readLength = 50;

	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<ScreenData> data(new ScreenData(screenDir));
	
	// -----------------------------------------------------------------------

	auto transcripts = loadTranscripts(assembly, mode, geneStart, geneEnd, cutOverlap);

	// -----------------------------------------------------------------------
	
	std::vector<Insertions> lowInsertions, highInsertions;

	data->analyze(assembly, readLength, transcripts, lowInsertions, highInsertions);
	
	if (lowInsertions.size() != transcripts.size() or highInsertions.size() != transcripts.size())
		throw std::runtime_error("Failed to calculate analysis");

	long lowSenseCount = 0, lowAntiSenseCount = 0;
	for (auto& i: lowInsertions)
	{
		lowSenseCount += i.sense.size();
		lowAntiSenseCount += i.antiSense.size();
	}

	long highSenseCount = 0, highAntiSenseCount = 0;
	for (auto& i: highInsertions)
	{
		highSenseCount += i.sense.size();
		highAntiSenseCount += i.antiSense.size();
	}

	std::vector<double> pvalues(transcripts.size(), 0);

	// for (size_t i = 0; i < transcripts.size(); ++i)
	// {
	// 	long low = lowInsertions[i].sense.size();
	// 	long high = highInsertions[i].sense.size();
	
	// 	long v[2][2] = {
	// 		{ low, high },
	// 		{ lowSenseCount - low, highSenseCount - high }
	// 	};

	// 	pvalues[i] = fisherTest2x2(v);
	// }

	parallel_for(transcripts.size(), [&](size_t i)
	{
		long low = lowInsertions[i].sense.size();
		long high = highInsertions[i].sense.size();
	
		long v[2][2] = {
			{ low, high },
			{ lowSenseCount - low, highSenseCount - high }
		};

		pvalues[i] = fisherTest2x2(v);
	});

	auto fcpv = adjustFDR_BH(pvalues);

	std::vector<DataPoint> result;

	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		auto& t = transcripts[i];

		auto low = lowInsertions[i].sense.size();
		auto high = highInsertions[i].sense.size();

		if (low == 0 and high == 0)
			continue;

		float miL = low, miH = high, miLT = lowSenseCount - low, miHT = highSenseCount - high;
		if (low == 0)
		{
			miL = 1;
			miLT -= 1;
		}

		if (high == 0)
		{
			miH = 1;
			miHT -= 1;
		}

		DataPoint p;
		p.geneName = t.geneName;
		p.geneID = i;
		p.fcpv = fcpv[i];
		p.mi = ((miH / miHT) / (miL / miLT));
		p.high = high;
		p.low = low;
		result.push_back(std::move(p));
	}
	
	return result;
}

std::vector<DataPoint> ScreenRestController::screenData(const std::string& screen)
{
	return screenDataEx(screen, "hg19", Mode::Collapse, true, "tx", "cds");
}

Region ScreenRestController::geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
{
	const int kWindowSize = 2000;

	auto transcripts = loadTranscripts(assembly, gene, kWindowSize);

	Region result = {};

	result.chrom = transcripts.front().chrom;
	result.start = std::numeric_limits<int>::max();

	for (auto& t: transcripts)
	{
		auto& tn = result.genes.emplace_back(Gene{t.geneName, { t.strand }, t.tx.start, t.tx.end, t.cds.start, t.cds.end});
		for (auto e: t.exons)
		{
			if (e.start >= t.cds.start and e.end <= t.cds.end)
			{
				tn.exons.emplace_back(GeneExon{e.start, e.end});
				continue;
			}

			if (e.start < t.cds.start)
			{
				auto u = e;
				if (u.end > t.cds.start)
					u.end = t.cds.start;
				
				if (t.strand == '+')
					tn.utr3.emplace_back(GeneExon{ u.start, u.end });
				else
					tn.utr5.emplace_back(GeneExon{ u.start, u.end });
				
				e.start = t.cds.start;
				if (e.start >= e.end)
					continue;
			}

			if (e.end > t.cds.end)
			{
				auto u = e;
				if (u.start < t.cds.end)
					u.start = t.cds.end;
				
				if (t.strand == '+')
					tn.utr5.emplace_back(GeneExon{ u.start, u.end });
				else
					tn.utr3.emplace_back(GeneExon{ u.start, u.end });
				
				e.end = t.cds.end;
				if (e.start >= e.end)
					continue;
			}

			tn.exons.emplace_back(GeneExon{ e.start, e.end });
		}
		
		if (t.geneName != gene)
			continue;

		if (result.start > t.tx.start)
			result.start = t.tx.start;
		if (result.end < t.tx.end)
			result.end = t.tx.end;
	}

	result.start -= kWindowSize;
	result.end += kWindowSize;

	// screen data

	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<ScreenData> data(new ScreenData(screenDir));
	
	std::tie(result.highPlus, result.highMinus, result.lowPlus, result.lowMinus) = 
		data->insertions(assembly, result.chrom, result.start, result.end);

	// filter
	filterTranscripts(transcripts, mode, geneStart, geneEnd, cutOverlap);

	for (auto& t: transcripts)
	{
		if (t.geneName != gene)
			continue;
		
		result.geneStrand = { t.strand };
		result.area.emplace_back(GeneExon{t.r.start, t.r.end});
	}

	return result;
}

// --------------------------------------------------------------------

class ScreenServer : public zh::webapp
{
  public:
	ScreenServer(const fs::path& docroot, const fs::path& screenDir)
		: zh::webapp(docroot)
		, mRestController(new ScreenRestController(screenDir))
		, mScreenDir(screenDir)
	{
		register_tag_processor<zh::tag_processor_v2>("http://www.hekkelman.com/libzeep/m2");

		add_controller(mRestController);
	
		mount("", &ScreenServer::fishtail);

		mount("css", &ScreenServer::handle_file);
		mount("scripts", &ScreenServer::handle_file);
		mount("fonts", &ScreenServer::handle_file);
	}

	void fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply);

  private:
	ScreenRestController* mRestController;
	fs::path mScreenDir;
};

void ScreenServer::fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	sub.put("page", "fishtail");

	using json = zeep::el::element;
	json screens, screenInfo;

	std::vector<std::string> screenNames;
	for (auto i = fs::directory_iterator(mScreenDir); i != fs::directory_iterator(); ++i)
	{
		if (not i->is_directory())
			continue;
		
		screenNames.push_back(i->path().filename().string());

		json info;

		for (auto a = fs::directory_iterator(i->path()); a != fs::directory_iterator(); ++a)
		{
			if (not a->is_directory())
				continue;
			
			info["assembly"].push_back(a->path().filename().string());
		}

		screenInfo[screenNames.back()] = info;
	}

	std::sort(screenNames.begin(), screenNames.end(), [](auto& a, auto& b) -> bool
	{
		auto r = std::mismatch(a.begin(), a.end(), b.begin(), b.end(), [](char ca, char cb) { return std::tolower(ca) == std::tolower(cb); });
		bool result;
		if (r.first == a.end() and r.second == b.end())
			result = false;
		else if (r.first == a.end())
			result = true;
		else if (r.second == b.end())
			result = false;
		else
			result = std::tolower(*r.first) < std::tolower(*r.second);
		return result;
	});

	for (auto& name: screenNames)
	{
		json screen{
			{ "id", name },
			{ "name", name }
		};

		screens.push_back(screen);
	}

	sub.put("screens", screens);
	sub.put("screenInfo", screenInfo);

	create_reply_from_template("fishtail.html", sub, reply);
}

// --------------------------------------------------------------------

zh::server* createServer(const fs::path& docroot, const fs::path& screenDir)
{
	return new ScreenServer(docroot, screenDir);
}
