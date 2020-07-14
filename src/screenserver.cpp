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

void to_element(zeep::json::element& e, Mode mode)
{
	switch (mode)
	{
		case Mode::Collapse:	e = "collapse"; break;
		case Mode::Longest:		e = "longest"; break;
		case Mode::Start:		e = "start"; break;
		case Mode::End:			e = "end"; break;
	}
}

void from_element(const zeep::json::element& e, Mode& mode)
{
	if (e == "collapse")		mode = Mode::Collapse;
	else if (e == "longest")	mode = Mode::Longest;
	else if (e == "start")		mode = Mode::Start;
	else if (e == "end")		mode = Mode::End;
	else throw std::runtime_error("Invalid mode");
}

// --------------------------------------------------------------------

void to_element(zeep::json::element& e, Direction direction)
{
	switch (direction)
	{
		case Direction::Sense:		e = "sense"; break;
		case Direction::AntiSense:	e = "antisense"; break;
		case Direction::Both:		e = "both"; break;
	}
}

void from_element(const zeep::json::element& e, Direction& direction)
{
	if (e == "sense")			direction = Direction::Sense;
	else if (e == "antisense")	direction = Direction::AntiSense;
	else if (e == "both")		direction = Direction::Both;
	else throw std::runtime_error("Invalid direction");
}

// --------------------------------------------------------------------

class ScreenRestController : public zh::rest_controller
{
  public:
	ScreenRestController(const fs::path& screenDir)
		: zh::rest_controller("ajax")
		, mScreenDir(screenDir)
	{
		// map_get_request("screenData/{id}", &ScreenRestController::screenData, "id");
		map_post_request("screenData/{id}", &ScreenRestController::screenDataEx,
			"id", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction");
		map_post_request("gene-info/{id}", &ScreenRestController::geneInfo, "id", "screen", "assembly", "mode", "cut-overlap", "gene-start", "gene-end");
	}

	// std::vector<DataPoint> screenData(const std::string& screen);
	std::vector<DataPoint> screenDataEx(const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	Region geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
};

std::vector<DataPoint> ScreenRestController::screenDataEx(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<ScreenData> data(new ScreenData(screenDir));
	
	// -----------------------------------------------------------------------

	return data->dataPoints(assembly, mode, cutOverlap, geneStart, geneEnd, direction);
}

// std::vector<DataPoint> ScreenRestController::screenData(const std::string& screen)
// {
// 	return screenDataEx(screen, "hg19", Mode::Collapse, true, "tx", "cds", Direction::Sense);
// }

Region ScreenRestController::geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
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

		for (auto& r: t.ranges)
			result.area.emplace_back(GeneExon{r.start, r.end});
	}

	return result;
}

// --------------------------------------------------------------------

class ScreenHtmlController : public zh::html_controller
{
  public:
	ScreenHtmlController(const fs::path& screenDir)
		: mScreenDir(screenDir)
	{
		mount("{,index,index.html}", &ScreenHtmlController::welcome);
		mount("screen", &ScreenHtmlController::fishtail);
		mount("{css,scripts,fonts,images}/", &ScreenHtmlController::handle_file);
	}

	void welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply);

  private:
	fs::path mScreenDir;
};

void ScreenHtmlController::welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	return get_template_processor().create_reply_from_template("index", scope, reply);
}

void ScreenHtmlController::fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply)
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

zh::server* createServer(const fs::path& docroot, const fs::path& screenDir,
	const std::string& secret)
{
	auto sc = new zh::security_context(secret, user_service::instance());
	sc->add_rule("/admin", { "ADMIN" });
	sc->add_rule("/admin/**", { "ADMIN" });
	sc->add_rule("/screen", { "USER" });
	sc->add_rule("/", {});

	auto server = new zh::server(sc, docroot);
	server->add_controller(new zh::login_controller());
	server->add_controller(new ScreenRestController(screenDir));
	server->add_controller(new ScreenHtmlController(screenDir));
	return server;
}
