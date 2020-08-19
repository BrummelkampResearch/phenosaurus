// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

#include <iostream>
#include <numeric>

#include <zeep/http/server.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/rest-controller.hpp>
#include <zeep/http/login-controller.hpp>

#include "screen-server.hpp"
#include "screen-data.hpp"
#include "screen-service.hpp"
#include "fisher.hpp"
#include "utils.hpp"
#include "user-service.hpp"
#include "db-connection.hpp"

namespace fs = std::filesystem;
namespace zh = zeep::http;

// --------------------------------------------------------------------
// This should be moved elsewhere, one day

class sa_strings_object : public zh::expression_utility_object<sa_strings_object>
{
  public:

	static constexpr const char* name() { return "strings"; }

	virtual zh::object evaluate(const zh::scope& scope, const std::string& methodName,
		const std::vector<zh::object>& parameters) const
	{
		zh::object result;

		if (methodName == "listJoin" and parameters.size() == 2)
		{
			auto list = parameters[0];
			auto separator = parameters[1].as<std::string>();

			std::ostringstream s;

			if (list.is_array())
			{
				auto n = list.size();
				for (auto& e: list)
				{
					s << e.as<std::string>();
					if (--n > 0)
						s << separator;
				}
			}
			else
				s << list;
			
			result = s.str();
		}

		return result;
	}
	
} s_post_expression_instance;

// -----------------------------------------------------------------------

class IPScreenRestController : public zh::rest_controller
{
  public:
	IPScreenRestController(const fs::path& screenDir)
		: zh::rest_controller("ip")
		, mScreenDir(screenDir)
	{
		map_post_request("screenData/{id}", &IPScreenRestController::screenData,
			"id", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction");

		map_post_request("finder/{gene}", &IPScreenRestController::find_gene,
			"gene", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction");

		map_post_request("similar/{gene}", &IPScreenRestController::find_similar,
			"gene", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction", "pv-cut-off", "zscore-cut-off");

		map_post_request("unique/{id}", &IPScreenRestController::uniqueness,
			"id", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction", "pv-cut-off");

		map_post_request("gene-info/{id}", &IPScreenRestController::geneInfo, "id", "screen", "assembly", "mode", "cut-overlap", "gene-start", "gene-end");
	}

	std::vector<ip_data_point> screenData(const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	std::vector<gene_finder_data_point> find_gene(const std::string& gene, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	std::vector<similar_data_point> find_similar(const std::string& gene, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction,
		float pvCutOff, float zscoreCutOff);

	std::vector<gene_uniqueness> uniqueness(const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction, float pvCutOff);

	Region geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
};

#if USE_CACHE
// std::vector<ip_data_point> IPScreenRestController::screenData(const std::string& screen, const std::string& assembly,
// 	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
// {
// 	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
// 		throw zeep::http::forbidden;

// 	auto dp = screen_service::instance().get_ip_screen_data(assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
// 	return dp->data_points(screen);
// }
#else
std::vector<ip_data_point> IPScreenRestController::screenData(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	IPScreenData data(screenDir);
	
	std::vector<ip_data_point> result;

	size_t i = 0;
	for (auto& dp: data.dataPoints(assembly, mode, cutOverlap, geneStart, geneEnd, direction))
	{
		if (dp.low == 0 and dp.high == 0)
			continue;

		ip_data_point p{};

		p.gene = dp.gene;
		p.pv = dp.pv;
		p.fcpv = dp.fcpv;
		p.mi = dp.mi;
		p.high = dp.high;
		p.low = dp.low;

		result.push_back(std::move(p));
	}
	
	return result;
}
#endif

std::vector<gene_uniqueness> IPScreenRestController::uniqueness(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction, float pvCutOff)
{
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	auto dp = screen_service::instance().get_ip_screen_data(assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
	return dp->uniqueness(screen, pvCutOff);
}

std::vector<gene_finder_data_point> IPScreenRestController::find_gene(const std::string& gene, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	auto dp = screen_service::instance().get_ip_screen_data(assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
	return dp->find_gene(gene);
}

std::vector<similar_data_point> IPScreenRestController::find_similar(const std::string& gene, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction,
	float pvCutOff, float zscoreCutOff)
{
	auto dp = screen_service::instance().get_ip_screen_data(assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
	return dp->find_similar(gene, pvCutOff, zscoreCutOff);
}

Region IPScreenRestController::geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
{
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

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

	IPScreenData data(screenDir);
	
	result.insertions.assign({
		{ "+", "high" },
		{ "-", "high" },
		{ "+", "low" },
		{ "-", "low" }		
	});

	std::tie(result.insertions[0].pos, result.insertions[1].pos, result.insertions[2].pos, result.insertions[3].pos) = 
		data.insertions(assembly, result.chrom, result.start, result.end);

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

class ScreenHtmlControllerBase : public zh::html_controller
{
  protected:
	ScreenHtmlControllerBase(const std::string& path, ScreenType type)
		: zh::html_controller(path), mType(type) {}
	
	void init_scope(zh::scope& scope)
	{
		auto credentials = get_credentials();

		using json = zeep::json::element;
		json screens;

		auto s = has_role("ADMIN") ?
			screen_service::instance().get_all_screens_for_type(mType) :
			screen_service::instance().get_all_screens_for_user_and_type(credentials["username"].as<std::string>(), mType);

		std::sort(s.begin(), s.end(), [](auto& sa, auto& sb) -> bool
		{
			std::string& a = sa.name;
			std::string& b = sb.name;

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

		to_element(screens, s);
		scope.put("screens", screens);
	}

	ScreenType mType;
};

// --------------------------------------------------------------------


class IPScreenHtmlController : public ScreenHtmlControllerBase
{
  public:
	IPScreenHtmlController(const fs::path& screenDir)
		: ScreenHtmlControllerBase("ip", ScreenType::IntracellularPhenotype)
		, mScreenDir(screenDir)
	{
		mount("screen", &IPScreenHtmlController::fishtail);
		mount("finder", &IPScreenHtmlController::finder);
		mount("similar", &IPScreenHtmlController::similar);
	}

	void fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void finder(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void similar(const zh::request& request, const zh::scope& scope, zh::reply& reply);

  private:
	fs::path mScreenDir;
};

void IPScreenHtmlController::fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);
	sub.put("page", "fishtail");
	get_template_processor().create_reply_from_template("fishtail.html", sub, reply);
}

void IPScreenHtmlController::finder(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("gene-finder.html", scope, reply);
}

void IPScreenHtmlController::similar(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("find-similar.html", scope, reply);
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
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

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
// 	return screenData(screen, "hg19", Mode::Collapse, true, "tx", "cds", Direction::Sense);
// }

Region SLScreenRestController::geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
{
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

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


class SLScreenHtmlController : public ScreenHtmlControllerBase
{
  public:
	SLScreenHtmlController(const fs::path& screenDir)
		: ScreenHtmlControllerBase("sl", ScreenType::SyntheticLethal)
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
	get_template_processor().create_reply_from_template("sl-screen.html", scope, reply);
}

// --------------------------------------------------------------------

class ScreenHtmlController : public zh::html_controller
{
  public:
	ScreenHtmlController()
	{
		mount("{,index,index.html}", &ScreenHtmlController::welcome);
		mount("{css,scripts,fonts,images}/", &ScreenHtmlController::handle_file);
		mount("favicon.ico", &ScreenHtmlController::handle_file);
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

	// init screen_service
	screen_service::init(screenDir);

	auto sc = new zh::security_context(secret, user_service::instance());
	sc->add_rule("/admin", { "ADMIN" });
	sc->add_rule("/admin/**", { "ADMIN" });
	sc->add_rule("/{ip,sl,screen}/", { "USER" });
	sc->add_rule("/", {});

	auto server = new zh::server(sc, docroot);

	server->add_error_handler(new db_error_handler());

	server->set_context_name(context_name);

	server->add_controller(new zh::login_controller());
	server->add_controller(new ScreenHtmlController());
	server->add_controller(new IPScreenRestController(screenDir));
	server->add_controller(new IPScreenHtmlController(screenDir));

	server->add_controller(new SLScreenRestController(screenDir));
	server->add_controller(new SLScreenHtmlController(screenDir));

	// admin
	server->add_controller(new user_admin_rest_controller());
	server->add_controller(new user_admin_html_controller());
	server->add_controller(new screen_admin_rest_controller());
	server->add_controller(new screen_admin_html_controller());
	server->add_controller(new screen_user_rest_controller());
	server->add_controller(new screen_user_html_controller());

	return server;
}
