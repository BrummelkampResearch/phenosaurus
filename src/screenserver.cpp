// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

#include <iostream>
#include <numeric>

#include <zeep/http/server.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/rest-controller.hpp>
#include <zeep/http/login-controller.hpp>

#include "screenserver.hpp"
#include "screendata.hpp"
#include "fisher.hpp"
#include "utils.hpp"
#include "user-service.hpp"

namespace fs = std::filesystem;
namespace zh = zeep::http;

// -----------------------------------------------------------------------

class IPScreenRestController : public zh::rest_controller
{
  public:
	IPScreenRestController(const fs::path& screenDir)
		: zh::rest_controller("ip")
		, mScreenDir(screenDir)
	{
		// map_get_request("screenData/{id}", &IPScreenRestController::screenData, "id");
		map_post_request("screenData/{id}", &IPScreenRestController::screenDataEx,
			"id", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction");
		map_post_request("gene-info/{id}", &IPScreenRestController::geneInfo, "id", "screen", "assembly", "mode", "cut-overlap", "gene-start", "gene-end");
	}

	// std::vector<IPDataPoint> screenData(const std::string& screen);
	std::vector<IPDataPoint> screenDataEx(const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	Region geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
};

std::vector<IPDataPoint> IPScreenRestController::screenDataEx(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<IPScreenData> data(new IPScreenData(screenDir));
	
	// -----------------------------------------------------------------------

	return data->dataPoints(assembly, mode, cutOverlap, geneStart, geneEnd, direction);
}

// std::vector<IPDataPoint> IPScreenRestController::screenData(const std::string& screen)
// {
// 	return screenDataEx(screen, "hg19", Mode::Collapse, true, "tx", "cds", Direction::Sense);
// }

Region IPScreenRestController::geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
{
	const int kWindowSize = 4000;

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

	std::unique_ptr<IPScreenData> data(new IPScreenData(screenDir));
	
	result.insertions.assign({
		{ "+", "high" },
		{ "-", "high" },
		{ "+", "low" },
		{ "-", "low" }		
	});

	std::tie(result.insertions[0].pos, result.insertions[1].pos, result.insertions[2].pos, result.insertions[3].pos) = 
		data->insertions(assembly, result.chrom, result.start, result.end);

	// filter
	filterTranscripts(transcripts, mode, geneStart, geneEnd, cutOverlap);

	for (auto& t: transcripts)
	{
		if (t.geneName != gene)
			continue;
		
		result.geneStrand = { t.strand };

		for (auto& r: t.ranges)
			result.area.emplace_back(GeneExon{r.start, r.end});
	}

	return result;
}

// --------------------------------------------------------------------

class IPScreenHtmlController : public zh::html_controller
{
  public:
	IPScreenHtmlController(const fs::path& screenDir)
		: zh::html_controller("ip")
		, mScreenDir(screenDir)
	{
		mount("screen", &IPScreenHtmlController::fishtail);
	}

	void fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply);

  private:
	fs::path mScreenDir;
};

void IPScreenHtmlController::fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	sub.put("page", "fishtail");

	using json = zeep::json::element;
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

	get_template_processor().create_reply_from_template("fishtail.html", sub, reply);
}

// --------------------------------------------------------------------

struct ScreenReplicateInfo
{
	int id;
	std::string name;
	std::vector<int> replicates;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("id", id)
		   & zeep::name_value_pair("name", name)
		   & zeep::name_value_pair("replicates", replicates);
	}
};

class SLScreenRestController : public zh::rest_controller
{
  public:
	SLScreenRestController(const fs::path& screenDir)
		: zh::rest_controller("sl")
		, mScreenDir(screenDir)
	{
		map_post_request("screenData/{id}/{replicate}", &SLScreenRestController::screenData,
			"id", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction", "replicate", "pvCutOff", "binomCutOff", "effectSize");
	}

	std::vector<SLDataPoint> screenData(const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction, uint16_t replicate, float pvCutOff, float binomCutOff, float effectSize);

	Region geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
};

std::vector<SLDataPoint> SLScreenRestController::screenData(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction, uint16_t replicate,
	float pvCutOff, float binomCutOff, float effectSize)
{
	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<SLScreenData> data(new SLScreenData(screenDir));
#warning "make control a parameter"
	std::unique_ptr<SLScreenData> controlData(new SLScreenData(mScreenDir / "ControlData-HAP1"));
	
	// -----------------------------------------------------------------------

	std::vector<Transcript> transcripts = loadTranscripts(assembly, mode, geneStart, geneEnd, cutOverlap);
	filterOutExons(transcripts);

	// reorder transcripts based on chr > end-position, makes code easier and faster
	std::sort(transcripts.begin(), transcripts.end(), [](auto& a, auto& b)
	{
		int d = a.chrom - b.chrom;
		if (d == 0)
			d = a.start() - b.start();
		return d < 0;
	});

	// --------------------------------------------------------------------
	
#warning "make groupSize a parameter"
	unsigned groupSize = 500;
#warning "make trimLength a parameter"
	unsigned trimLength = 50;

	// -----------------------------------------------------------------------

	return data->dataPoints(replicate, assembly, trimLength, transcripts, *controlData, groupSize, pvCutOff, binomCutOff, effectSize);
}

// std::vector<SLDataPoint> SLScreenRestController::screenData(const std::string& screen)
// {
// 	return screenDataEx(screen, "hg19", Mode::Collapse, true, "tx", "cds", Direction::Sense);
// }

Region SLScreenRestController::geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
{
	// const int kWindowSize = 4000;

	// auto transcripts = loadTranscripts(assembly, gene, kWindowSize);

	// Region result = {};

	// result.chrom = transcripts.front().chrom;
	// result.start = std::numeric_limits<int>::max();

	// for (auto& t: transcripts)
	// {
	// 	auto& tn = result.genes.emplace_back(Gene{t.geneName, { t.strand }, t.tx.start, t.tx.end, t.cds.start, t.cds.end});
	// 	for (auto e: t.exons)
	// 	{
	// 		if (e.start >= t.cds.start and e.end <= t.cds.end)
	// 		{
	// 			tn.exons.emplace_back(GeneExon{e.start, e.end});
	// 			continue;
	// 		}

	// 		if (e.start < t.cds.start)
	// 		{
	// 			auto u = e;
	// 			if (u.end > t.cds.start)
	// 				u.end = t.cds.start;
				
	// 			if (t.strand == '+')
	// 				tn.utr3.emplace_back(GeneExon{ u.start, u.end });
	// 			else
	// 				tn.utr5.emplace_back(GeneExon{ u.start, u.end });
				
	// 			e.start = t.cds.start;
	// 			if (e.start >= e.end)
	// 				continue;
	// 		}

	// 		if (e.end > t.cds.end)
	// 		{
	// 			auto u = e;
	// 			if (u.start < t.cds.end)
	// 				u.start = t.cds.end;
				
	// 			if (t.strand == '+')
	// 				tn.utr5.emplace_back(GeneExon{ u.start, u.end });
	// 			else
	// 				tn.utr3.emplace_back(GeneExon{ u.start, u.end });
				
	// 			e.end = t.cds.end;
	// 			if (e.start >= e.end)
	// 				continue;
	// 		}

	// 		tn.exons.emplace_back(GeneExon{ e.start, e.end });
	// 	}
		
	// 	if (t.geneName != gene)
	// 		continue;

	// 	if (result.start > t.tx.start)
	// 		result.start = t.tx.start;
	// 	if (result.end < t.tx.end)
	// 		result.end = t.tx.end;
	// }

	// result.start -= kWindowSize;
	// result.end += kWindowSize;

	// // screen data

	// fs::path screenDir = mScreenDir / screen;

	// if (not fs::is_directory(screenDir))
	// 	throw std::runtime_error("No such screen: " + screen);

	// std::unique_ptr<SLScreenData> data(new SLScreenData(screenDir));
	
	// result.insertions.assign({
	// 	{ "+", "high" },
	// 	{ "-", "high" },
	// 	{ "+", "low" },
	// 	{ "-", "low" }		
	// });

	// std::tie(result.insertions[0].pos, result.insertions[1].pos, result.insertions[2].pos, result.insertions[3].pos) = 
	// 	data->insertions(assembly, result.chrom, result.start, result.end);

	// // filter
	// filterTranscripts(transcripts, mode, geneStart, geneEnd, cutOverlap);

	// for (auto& t: transcripts)
	// {
	// 	if (t.geneName != gene)
	// 		continue;
		
	// 	result.geneStrand = { t.strand };

	// 	for (auto& r: t.ranges)
	// 		result.area.emplace_back(GeneExon{r.start, r.end});
	// }

	// return result;
	return {};
}


class SLScreenHtmlController : public zh::html_controller
{
  public:
	SLScreenHtmlController(const fs::path& screenDir)
		: zh::html_controller("sl")
		, mScreenDir(screenDir)
	{
		mount("screen", &SLScreenHtmlController::screen);
	}

	void screen(const zh::request& request, const zh::scope& scope, zh::reply& reply);

  private:
	fs::path mScreenDir;
};

void SLScreenHtmlController::screen(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	const std::regex rx(R"(replicate-(\d)\.fastq(?:\.gz)?)");

	zh::scope sub(scope);

	using json = zeep::json::element;
	json screens;

	std::vector<std::string> screenNames;
	for (auto i = fs::directory_iterator(mScreenDir); i != fs::directory_iterator(); ++i)
	{
		if (not i->is_directory())
			continue;
		
		const auto& [sd, type] = ScreenData::create(i->path());
		if (type != ScreenType::SyntheticLethal)
			continue;
		
		auto screenName = i->path().filename().string();

		json info{
			{ "id", screens.size() + 1 },
			{ "name", screenName }
		};

		std::vector<int> replicates;

		for (auto a = fs::directory_iterator(i->path()); a != fs::directory_iterator(); ++a)
		{
			if (a->is_directory())
				continue;
			
			std::smatch m;
			std::string filename = a->path().filename().string();
			if (not std::regex_match(filename, m, rx))
				continue;
			
			replicates.push_back(std::stoi(m[1].str()));
		}

		std::sort(replicates.begin(), replicates.end());
		
		info["replicates"] = replicates;

		screens.push_back(std::move(info));
	}

	// std::sort(screenNames.begin(), screenNames.end(), [](auto& a, auto& b) -> bool
	// {
	// 	auto r = std::mismatch(a.begin(), a.end(), b.begin(), b.end(), [](char ca, char cb) { return std::tolower(ca) == std::tolower(cb); });
	// 	bool result;
	// 	if (r.first == a.end() and r.second == b.end())
	// 		result = false;
	// 	else if (r.first == a.end())
	// 		result = true;
	// 	else if (r.second == b.end())
	// 		result = false;
	// 	else
	// 		result = std::tolower(*r.first) < std::tolower(*r.second);
	// 	return result;
	// });

	// for (auto& name: screenNames)
	// {
	// 	json screen{
	// 		{ "id", name },
	// 		{ "name", name }
	// 	};

	// 	screens.push_back(screen);
	// }

	sub.put("screenReplicates", screens);
	// sub.put("screenInfo", screenInfo);

	get_template_processor().create_reply_from_template("sl-screen.html", sub, reply);
}

// --------------------------------------------------------------------

class ScreenHtmlController : public zh::html_controller
{
  public:
	ScreenHtmlController()
	{
		mount("{,index,index.html}", &ScreenHtmlController::welcome);
		mount("{css,scripts,fonts,images}/", &ScreenHtmlController::handle_file);
	}

	void welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply);
};

void ScreenHtmlController::welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	return get_template_processor().create_reply_from_template("index", scope, reply);
}

// --------------------------------------------------------------------

zh::server* createServer(const fs::path& docroot, const fs::path& screenDir,
	const std::string& secret, const std::string& context_name)
{
	// map enums

	zeep::value_serializer<Mode>::init("mode", {
		{ Mode::Collapse, 	"collapse" },
		{ Mode::Longest, 	"longest" },
		{ Mode::Start, 		"start" },
		{ Mode::End, 		"end" }
	});

	zeep::value_serializer<Direction>::init("direction", {
		{ Direction::Sense, 	"sense" },
		{ Direction::AntiSense, "antisense" },
		{ Direction::Both, 		"both" }
	});

	auto sc = new zh::security_context(secret, user_service::instance());
	sc->add_rule("/admin", { "ADMIN" });
	sc->add_rule("/admin/**", { "ADMIN" });
	sc->add_rule("/{ip,sl,screen}/", { "USER" });
	sc->add_rule("/", {});

	auto server = new zh::server(sc, docroot);

	server->set_context_name(context_name);

	server->add_controller(new zh::login_controller());
	server->add_controller(new ScreenHtmlController());
	server->add_controller(new IPScreenRestController(screenDir));
	server->add_controller(new IPScreenHtmlController(screenDir));

	server->add_controller(new SLScreenRestController(screenDir));
	server->add_controller(new SLScreenHtmlController(screenDir));

	return server;
}
