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
#include "genome-browser.hpp"

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
	IPScreenRestController(const fs::path& screenDir, ScreenType type)
		: zh::rest_controller(zeep::value_serializer<ScreenType>::to_string(type))
		, mScreenDir(screenDir), mType(type)
	{
		map_post_request("screen/{id}", &IPScreenRestController::screenData,
			"id", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction");

		map_post_request("finder/{gene}", &IPScreenRestController::find_gene,
			"gene", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction");

		map_post_request("similar/{gene}", &IPScreenRestController::find_similar,
			"gene", "assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction", "pv-cutoff", "zs-cutoff");

		map_post_request("clusters", &IPScreenRestController::find_clusters,
			"assembly", "mode", "cut-overlap", "gene-start", "gene-end", "direction", "pv-cutoff", "minPts", "eps", "NNs");

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

	std::vector<cluster> find_clusters(const std::string& assembly, Mode mode, bool cutOverlap,
		const std::string& geneStart, const std::string& geneEnd, Direction direction,
		float pvCutOff, size_t minPts, float eps, size_t NNs);

	std::vector<gene_uniqueness> uniqueness(const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction, float pvCutOff);

	Region geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
	ScreenType mType;
};

std::vector<ip_data_point> IPScreenRestController::screenData(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	return screen_service::instance().get_data_points(mType, screen, assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
}

std::vector<gene_uniqueness> IPScreenRestController::uniqueness(const std::string& screen, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction, float pvCutOff)
{
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	auto dp = screen_service::instance().get_screen_data(mType, assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
	return dp->uniqueness(screen, pvCutOff);
}

std::vector<gene_finder_data_point> IPScreenRestController::find_gene(const std::string& gene, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	auto dp = screen_service::instance().get_screen_data(mType, assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
	return dp->find_gene(gene, user_service::instance().allowed_screens_for_user(get_credentials()["username"].as<std::string>()));
}

std::vector<similar_data_point> IPScreenRestController::find_similar(const std::string& gene, const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction,
	float pvCutOff, float zscoreCutOff)
{
	auto dp = screen_service::instance().get_screen_data(mType, assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
	return dp->find_similar(gene, pvCutOff, zscoreCutOff);
}

std::vector<cluster> IPScreenRestController::find_clusters(const std::string& assembly,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction,
	float pvCutOff, size_t minPts, float eps, size_t NNs)
{
	auto dp = screen_service::instance().get_screen_data(mType, assembly, 50, mode, cutOverlap, geneStart, geneEnd, direction);
	return dp->find_clusters(pvCutOff, minPts, eps, NNs);
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

	auto data = IPPAScreenData::load(screenDir);
	
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

class ScreenHtmlControllerBase : public zh::html_controller
{
  protected:
	ScreenHtmlControllerBase(ScreenType type)
		: zh::html_controller(zeep::value_serializer<ScreenType>::to_string(type))
		, mType(type) {}
	
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

		json screenNames;
		for (auto& si: s)
			screenNames.push_back(si.name);
		scope.put("screenNames", screenNames);

		scope.put("screenType", mType);
	}

	ScreenType mType;
};

// --------------------------------------------------------------------

class IPScreenHtmlController : public ScreenHtmlControllerBase
{
  public:
	IPScreenHtmlController(const fs::path& screenDir, ScreenType type)
		: ScreenHtmlControllerBase(type)
		, mScreenDir(screenDir)
	{
		mount("screen", &IPScreenHtmlController::fishtail);
		mount("finder", &IPScreenHtmlController::finder);
		mount("similar", &IPScreenHtmlController::similar);
		mount("cluster", &IPScreenHtmlController::cluster);
		mount("compare-1", &IPScreenHtmlController::compare_1);
		mount("compare-2", &IPScreenHtmlController::compare_2);
		mount("compare-3", &IPScreenHtmlController::compare_3);
	}

	void fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void finder(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void similar(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void cluster(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void compare_1(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void compare_2(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void compare_3(const zh::request& request, const zh::scope& scope, zh::reply& reply);

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
	get_template_processor().create_reply_from_template("find-genes.html", scope, reply);
}

void IPScreenHtmlController::similar(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("find-similar.html", scope, reply);
}

void IPScreenHtmlController::cluster(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("find-clusters.html", scope, reply);
}

void IPScreenHtmlController::compare_1(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("compare-1.html", scope, reply);
}

void IPScreenHtmlController::compare_2(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("compare-2.html", scope, reply);
}

void IPScreenHtmlController::compare_3(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("compare-3.html", scope, reply);
}

// --------------------------------------------------------------------

class SLScreenRestController : public zh::rest_controller
{
  public:
	SLScreenRestController(const fs::path& screenDir)
		: zh::rest_controller("sl")
		, mScreenDir(screenDir)
	{
		map_post_request("screen/{id}", &SLScreenRestController::screenData,
			"id", "assembly", "control", "mode", "cut-overlap", "gene-start", "gene-end", "direction", "pvCutOff", "binomCutOff", "effectSize");

		map_post_request("gene-info/{id}", &SLScreenRestController::geneInfo, "id", "screen", "assembly", "mode", "cut-overlap", "gene-start", "gene-end");
	}

	SLDataResult screenData(const std::string& screen, const std::string& assembly, std::string control,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction, float pvCutOff, float binomCutOff, float effectSize);

	Region geneInfo(const std::string& gene, const std::string& screen, const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
};

SLDataResult SLScreenRestController::screenData(const std::string& screen, const std::string& assembly, std::string control,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction,
	float pvCutOff, float binomCutOff, float effectSize)
{
	if (not user_service::instance().allow_screen_for_user(screen, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<SLScreenData> data(new SLScreenData(screenDir));

	if (control.empty())
		control = "ControlData-HAP1";

	std::unique_ptr<SLScreenData> controlData(new SLScreenData(mScreenDir / control));
	
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
	// unsigned groupSize = 500;
	unsigned groupSize = 200;
#warning "make trimLength a parameter"
	unsigned trimLength = 50;

	// -----------------------------------------------------------------------

	return data->dataPoints(assembly, trimLength, transcripts, *controlData, groupSize, pvCutOff, binomCutOff, effectSize);
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

	std::unique_ptr<SLScreenData> data(new SLScreenData(screenDir));

	for (auto r: data->getReplicateNames())
	{
		std::vector<uint32_t> pos_p, pos_m;
		std::tie(pos_p, pos_m) = data->getInsertionsForReplicate(r, assembly, result.chrom, result.start, result.end);

		result.insertions.emplace_back("+", r, std::move(pos_p));
		result.insertions.emplace_back("-", r, std::move(pos_m));
	}

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

class SLScreenHtmlController : public ScreenHtmlControllerBase
{
  public:
	SLScreenHtmlController(const fs::path& screenDir)
		: ScreenHtmlControllerBase(ScreenType::SyntheticLethal)
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
	void genome_browser(const zh::request& request, const zh::scope& scope, zh::reply& reply);
};

void ScreenHtmlController::welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	return get_template_processor().create_reply_from_template("index", scope, reply);
}

// --------------------------------------------------------------------

zh::server* createServer(const fs::path& docroot, const fs::path& screenDir,
	const std::string& secret, const std::string& context_name)
{
	std::set_terminate([]()
	{
		std::cerr << "Unhandled exception in server" << std::endl;
		std::abort();
	});

	// init screen_service
	screen_service::init(screenDir);

	auto sc = new zh::security_context(secret, user_service::instance());
	sc->add_rule("/admin", { "ADMIN" });
	sc->add_rule("/admin/**", { "ADMIN" });
	sc->add_rule("/{ip,pa,sl,screen}/", { "USER" });
	sc->add_rule("/", {});

	sc->set_validate_csrf(true);

	auto server = new zh::server(sc, docroot);

	server->add_error_handler(new db_error_handler());

	server->set_context_name(context_name);

	server->add_controller(new zh::login_controller());
	server->add_controller(new user_service_html_controller());

	server->add_controller(new ScreenHtmlController());
	server->add_controller(new IPScreenRestController(screenDir, ScreenType::IntracellularPhenotype));
	server->add_controller(new IPScreenRestController(screenDir, ScreenType::IntracellularPhenotypeActivation));
	server->add_controller(new IPScreenHtmlController(screenDir, ScreenType::IntracellularPhenotype));
	server->add_controller(new IPScreenHtmlController(screenDir, ScreenType::IntracellularPhenotypeActivation));

	server->add_controller(new SLScreenRestController(screenDir));
	server->add_controller(new SLScreenHtmlController(screenDir));

	server->add_controller(new genome_browser_html_controller());
	server->add_controller(new genome_browser_rest_controller());

	// admin
	server->add_controller(new user_admin_rest_controller());
	server->add_controller(new user_admin_html_controller());
	server->add_controller(new screen_admin_rest_controller());
	server->add_controller(new screen_admin_html_controller());
	server->add_controller(new screen_user_rest_controller());
	server->add_controller(new screen_user_html_controller());

	return server;
}
