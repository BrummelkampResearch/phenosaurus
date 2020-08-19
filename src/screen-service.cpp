//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

#include <filesystem>

#include <zeep/crypto.hpp>

#include "user-service.hpp"
#include "screen-service.hpp"
#include "db-connection.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

// --------------------------------------------------------------------

screen_data_cache::screen_data_cache(const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
	: m_assembly(assembly), m_trim_length(trim_length), m_mode(mode), m_cutOverlap(cutOverlap)
	, m_geneStart(geneStart), m_geneEnd(geneEnd)
{
	m_transcripts = loadTranscripts(assembly, mode, geneStart, geneEnd, cutOverlap);
}

screen_data_cache::~screen_data_cache()
{
}

// --------------------------------------------------------------------

ip_screen_data_cache::ip_screen_data_cache(const std::string& assembly, short trim_length, Mode mode,
		bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
	: screen_data_cache(assembly, trim_length, mode, cutOverlap, geneStart, geneEnd)
	, m_direction(direction), m_data(nullptr)
{
	auto screens = screen_service::instance().get_all_screens_for_type(ScreenType::IntracellularPhenotype);
	auto screenDataDir = screen_service::instance().get_screen_data_dir();

	for (auto& screen: screens)
	{
		if (screen.ignore)
			continue;
		
		m_screens.push_back(screen.name);
	}

	size_t N = m_transcripts.size();
	size_t M = m_screens.size();
	
	m_data = new data_point[N * M];
	memset(m_data, 0, N * M * sizeof(data_point));

	// for (size_t si = 0; si < m_screens.size(); ++si)
	parallel_for(m_screens.size(), [&](size_t si)
	{
		auto sd = m_data + si * m_transcripts.size();

		fs::path screenDir = screenDataDir / m_screens[si];
		IPScreenData data(screenDir);
		
		std::vector<Insertions> lowInsertions, highInsertions;

		data.analyze(m_assembly, m_trim_length, m_transcripts, lowInsertions, highInsertions);

		auto dp = data.dataPoints(m_transcripts, lowInsertions, highInsertions, m_direction);

		for (size_t ti = 0; ti < m_transcripts.size(); ++ti)
		{
			auto& d = sd[ti];
			auto& p = dp[ti];

			d.pv = p.pv;
			d.fcpv = p.fcpv;
			d.mi = p.mi;
			d.low = p.low;
			d.high = p.high;
		}
	});
}

ip_screen_data_cache::~ip_screen_data_cache()
{
	delete [] m_data;
}

std::vector<ip_data_point> ip_screen_data_cache::data_points(const std::string& screen)
{
	std::vector<ip_data_point> result;

	auto si = std::find(m_screens.begin(), m_screens.end(), screen);
	if (si == m_screens.end())
		return {};
	
	size_t screenIx = si - m_screens.begin();
	size_t N = m_transcripts.size();
	auto data = m_data + screenIx * N;

	for (size_t i = 0; i < m_transcripts.size(); ++i)
	{
		auto& dp = data[i];
		if (dp.low == 0 and dp.high == 0)
			continue;

		ip_data_point p{};

		p.gene = m_transcripts[i].geneName;
		p.pv = dp.pv;
		p.fcpv = dp.fcpv;
		p.mi = dp.mi;
		p.high = dp.high;
		p.low = dp.low;

		result.push_back(std::move(p));
	}
	
	return result;
}

std::vector<gene_uniqueness> ip_screen_data_cache::uniqueness(const std::string& screen, float pvCutOff)
{
	auto si = std::find(m_screens.begin(), m_screens.end(), screen);
	if (si == m_screens.end())
		return {};

	size_t screenIx = si - m_screens.begin();
	size_t N = m_transcripts.size();
	auto data = m_data + screenIx * N;

	std::vector<gene_uniqueness> result;

	double r = std::pow(m_screens.size(), 0.001) - 1;

	for (size_t ti = 0; ti < m_transcripts.size(); ++ti)
	{
		auto& dp = data[ti];

		if (dp.fcpv > pvCutOff)
			continue;
		
		size_t c = 0;
		for (size_t si = 0; si < m_screens.size(); ++si)
		{
			auto& sp = m_data[si * N + ti];
			if (sp.fcpv <= pvCutOff and (dp.mi < 1) == (sp.mi < 1))
				++c;
		}

		double cd = std::pow(c, 0.001) - 1;

		int colour = static_cast<int>(std::ceil(10 * cd / r));

		result.emplace_back(gene_uniqueness{m_transcripts[ti].geneName, colour});
	}

	return result;
}

std::vector<gene_finder_data_point> ip_screen_data_cache::find_gene(const std::string& gene)
{
	return {};
}

std::vector<similar_data_point> ip_screen_data_cache::find_similar(const std::string& gene, float pvCutOff, float zscoreCutOff)
{
	size_t geneCount = m_transcripts.size(), screenCount = m_screens.size(), dataCount = geneCount * screenCount;

	auto gi = std::find_if(m_transcripts.begin(), m_transcripts.end(), [gene](auto& t) { return t.geneName == gene; });
	if (gi == m_transcripts.end())
		return {};

	size_t queryGeneIx = gi - m_transcripts.begin();

	std::vector<similar_data_point> result;

	for (auto negative: { false, true })
	{
		auto distance = [&](int geneIx)
		{
			size_t a_ix = queryGeneIx;
			const auto* a = &m_data[a_ix];
			size_t b_ix = geneIx;
			const auto* b = &m_data[b_ix];

			long double sum = 0;
			for (size_t i = 0; i < screenCount; ++i)
			{
				float miQ, miT;

				miQ = a[i * geneCount].mi;
				miT = b[i * geneCount].mi;

				if (negative)
					sum += (miQ + miT) * (miQ + miT);
				else
					sum += (miQ - miT) * (miQ - miT);
			}

			return static_cast<double>(sqrt(sum));
		};

		std::vector<similar_data_point> hits;
		hits.reserve(geneCount);
		
		double distanceSum = 0;

		for (size_t g_ix = 0; g_ix < geneCount; ++g_ix)
		{
			double d = distance(g_ix);

			hits.push_back(similar_data_point{ m_transcripts[g_ix].geneName, static_cast<float>(d), 0.f, negative });

			distanceSum += d;
		}

		double averageDistance = distanceSum / geneCount;
		double sumSq = accumulate(hits.begin(), hits.end(), 0.0, [averageDistance](double s, auto& h) { return s + (h.distance - averageDistance) * (h.distance - averageDistance); });
		double stddev = sqrt(sumSq / (geneCount - 1));

		for (auto& hit: hits)
			hit.zscore = (averageDistance - hit.distance) / stddev;

		hits.erase(remove_if(hits.begin(), hits.end(),
			[zscoreCutOff, averageDistance](auto hit) { return hit.distance > averageDistance or hit.zscore < zscoreCutOff; }),
			hits.end());

		sort(hits.begin(), hits.end());

		result.insert(result.end(), hits.begin(), hits.end());
	}

	return result;
}

// vector<GeneQuery::hit> GeneQuery::find_similar(const string& gene, const string& genome, float pvCutOff, float zscoreCutOff)
// {
// 	int genomeID = get_genome_id(genome);
// 	int geneID = get_gene_id(gene, genomeID);

// 	vector<int> gene_detail_ids, screen_ids, geneIndex, screenIndex;
// 	tie(gene_detail_ids, screen_ids, geneIndex, screenIndex) = load_data(genomeID, pvCutOff);

// 	if (geneID >= geneIndex.size())
// 		throw runtime_error("gene ID " + to_string(geneID) + " is out of range");
// 	auto queryGeneIx = geneIndex[geneID];

// 	pqxx::transaction<> tx(m_connection);

// 	// load mutational index data
// 	size_t geneCount = gene_detail_ids.size(), screenCount = screen_ids.size(), dataCount = geneCount * screenCount;
// 	vector<float> data(dataCount);

// 	size_t n = 0;

// 	for (auto r: tx.exec(
// 		"SELECT a.screen_id, a.gene_details_id, a.mi"
// 		" FROM ips_data_points a"
// 		" LEFT JOIN gene_details b ON a.gene_details_id = b.id"
// 		" LEFT JOIN screens c ON a.screen_id = c.id"
// 		" WHERE a.fcpv <= " + to_string(pvCutOff) +
// 		" AND b.genome_id = " + to_string(genomeID) +
// 		" AND c.ignored = FALSE"
// 		))
// 	{
// 		int s_ix = screenIndex[r[0].as<int>()];
// 		int g_ix = geneIndex[r[1].as<int>()];

// 		int ix = g_ix * screenCount + s_ix;
// 		assert(static_cast<size_t>(ix) < data.size());

// 		float mi = log2(r[2].as<float>());

// 		data[ix] = mi;

// 		++n;
// 	}
// 	tx.commit();

// 	vector<hit> result;

// 	for (auto negative: { false, true })
// 	{
// 		auto distance = [&](int geneIx)
// 		{
// 			size_t a_ix = queryGeneIx * screenCount;
// 			const auto* a = &data[a_ix];
// 			size_t b_ix = geneIx * screenCount;
// 			const auto* b = &data[b_ix];

// 			long double sum = 0;
// 			for (size_t i = 0; i < screenCount; ++i)
// 			{
// 				float miQ, miT;

// 				miQ = a[i];
// 				miT = b[i];

// 				if (negative)
// 					sum += (miQ + miT) * (miQ + miT);
// 				else
// 					sum += (miQ - miT) * (miQ - miT);
// 			}

// 			return static_cast<double>(sqrt(sum));
// 		};

// 		vector<hit> hits;
// 		hits.reserve(geneCount);
		
// 		double distanceSum = 0;

// 		for (size_t g_ix = 0; g_ix < geneCount; ++g_ix)
// 		{
// 			double d = distance(g_ix);

// 			hits.push_back({ gene_detail_ids[g_ix], d, negative });

// 			distanceSum += d;
// 		}

// 		double averageDistance = distanceSum / geneCount;
// 		double sumSq = accumulate(hits.begin(), hits.end(), 0.0, [averageDistance](double s, auto& h) { return s + (h.dist - averageDistance) * (h.dist - averageDistance); });
// 		double stddev = sqrt(sumSq / (geneCount - 1));

// 		for (auto& hit: hits)
// 			hit.zscore = (averageDistance - hit.dist) / stddev;

// 		hits.erase(remove_if(hits.begin(), hits.end(),
// 			[zscoreCutOff, averageDistance](auto hit) { return hit.dist > averageDistance or hit.zscore < zscoreCutOff; }),
// 			hits.end());

// 		sort(hits.begin(), hits.end());

// 		result.insert(result.end(), hits.begin(), hits.end());
// 	}

// 	for (auto& hit: result)
// 		hit.geneName = get_gene_name(hit.geneID);

// 	return result;
// }


// --------------------------------------------------------------------

std::unique_ptr<screen_service> screen_service::s_instance;

void screen_service::init(const std::string& screen_data_dir)
{
	assert(not s_instance);
	s_instance.reset(new screen_service(screen_data_dir));
}

screen_service& screen_service::instance()
{
	assert(s_instance);
	return *s_instance;
}

screen_service::screen_service(const std::string& screen_data_dir)
	: m_screen_data_dir(screen_data_dir)
{
	if (not fs::exists(m_screen_data_dir))
		throw std::runtime_error("Screen data directory " + screen_data_dir + " does not exist");
}

std::vector<screen_info> screen_service::get_all_screens() const
{
	std::vector<screen_info> result;

	for (auto i = fs::directory_iterator(m_screen_data_dir); i != fs::directory_iterator(); ++i)
	{
		if (not i->is_directory())
			continue;

		std::ifstream manifest(i->path() / "manifest.json");
		if (not manifest.is_open())
			continue;

		zeep::json::element info;
		zeep::json::parse_json(manifest, info);

		screen_info screen;
		zeep::json::from_element(info, screen);
		
		result.push_back(screen);
	}

	return result;
}

std::vector<screen_info> screen_service::get_all_screens_for_type(ScreenType type) const
{
	auto screens = get_all_screens();
	screens.erase(std::remove_if(screens.begin(), screens.end(), [type](auto& s) { return s.type != type; }), screens.end());
	return screens;
}

std::vector<screen_info> screen_service::get_all_screens_for_user(const std::string& user) const
{
	auto screens = get_all_screens();

	auto& user_service = user_service::instance();

	screens.erase(std::remove_if(screens.begin(), screens.end(), [user,&user_service](auto& s) {
		return not user_service.allow_screen_for_user(s.name, user);
	}), screens.end());
	return screens;
}

std::vector<screen_info> screen_service::get_all_screens_for_user_and_type(const std::string& user, ScreenType type) const
{
	auto screens = get_all_screens_for_user(user);
	screens.erase(std::remove_if(screens.begin(), screens.end(), [type](auto& s) { return s.type != type; }), screens.end());
	return screens;
}

screen_info screen_service::retrieve_screen(const std::string& name)
{
	std::ifstream manifest(m_screen_data_dir / name / "manifest.json");

	if (not manifest.is_open())
		throw std::runtime_error("No such screen?: " + name);
	
	zeep::json::element info;
	zeep::json::parse_json(manifest, info);

	screen_info screen;
	zeep::json::from_element(info, screen);

	for (auto& group: user_service::instance().get_groups_for_screen(screen.name))
		screen.groups.push_back(group);

	return screen;
}

void screen_service::update_screen(const std::string& name, const screen_info& screen)
{
	std::ofstream manifest(m_screen_data_dir / name / "manifest.json");
	if (not manifest.is_open())
		throw std::runtime_error("Could not create manifest file in " + m_screen_data_dir.string());

	zeep::json::element jInfo;
	zeep::json::to_element(jInfo, screen);
	manifest << jInfo;
	manifest.close();

	user_service::instance().set_groups_for_screen(name, screen.groups);
}

void screen_service::delete_screen(const std::string& name)
{
	fs::remove_all(m_screen_data_dir / name);
}

std::shared_ptr<ip_screen_data_cache> screen_service::get_ip_screen_data(const std::string& assembly, short trim_length,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	std::unique_lock lock(m_mutex);

	auto i = std::find_if(m_ip_data_cache.begin(), m_ip_data_cache.end(),
		std::bind(&ip_screen_data_cache::is_for, std::placeholders::_1, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd, direction));

	std::shared_ptr<ip_screen_data_cache> result;

	if (i != m_ip_data_cache.end())
		result = *i;
	else
	{
		result = std::make_shared<ip_screen_data_cache>(assembly, trim_length, mode, cutOverlap, geneStart, geneEnd, direction);
		m_ip_data_cache.emplace_back(result);
	}
	
	return result;
}

// --------------------------------------------------------------------

screen_admin_html_controller::screen_admin_html_controller()
	: zeep::http::html_controller("/admin")
{
	mount("screens", &screen_admin_html_controller::handle_screen_admin);
}

void screen_admin_html_controller::handle_screen_admin(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	zeep::http::scope sub(scope);

	zeep::json::element screens;
	auto s = screen_service::instance().get_all_screens();
	to_element(screens, s);
	sub.put("screens", screens);

	zeep::json::element users;
	auto u = user_service::instance().get_all_users();
	to_element(users, u);
	sub.put("users", users);

	zeep::json::element groups;
	auto g = user_service::instance().get_all_groups();
	to_element(groups, g);
	sub.put("groups", groups);

	get_template_processor().create_reply_from_template("admin-screens.html", sub, reply);
}

// --------------------------------------------------------------------

screen_admin_rest_controller::screen_admin_rest_controller()
	: zeep::http::rest_controller("/admin")
{
	// map_post_request("screen", &screen_admin_rest_controller::create_screen, "screen");
	map_get_request("screen/{id}", &screen_admin_rest_controller::retrieve_screen, "id");
	map_put_request("screen/{id}", &screen_admin_rest_controller::update_screen, "id", "screen");
	map_delete_request("screen/{id}", &screen_admin_rest_controller::delete_screen, "id");
}

// uint32_t screen_admin_rest_controller::create_screen(const screen& screen)
// {
// 	return screen_service::instance().create_screen(screen);
// }

screen_info screen_admin_rest_controller::retrieve_screen(const std::string& name)
{
	return screen_service::instance().retrieve_screen(name);
}

void screen_admin_rest_controller::update_screen(const std::string& name, const screen_info& screen)
{
	screen_service::instance().update_screen(name, screen);
}

void screen_admin_rest_controller::delete_screen(const std::string& name)
{
	screen_service::instance().delete_screen(name);
}


// --------------------------------------------------------------------

screen_user_html_controller::screen_user_html_controller()
	: zeep::http::html_controller("/")
{
	mount("screens", &screen_user_html_controller::handle_screen_user);
}

void screen_user_html_controller::handle_screen_user(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	zeep::http::scope sub(scope);

	zeep::json::element users;
	auto u = user_service::instance().get_all_users();
	to_element(users, u);
	sub.put("users", users);

	zeep::json::element groups;
	auto g = user_service::instance().get_all_groups();
	to_element(groups, g);
	sub.put("groups", groups);

	auto credentials = get_credentials();
	auto username = credentials["username"].as<std::string>();

	using json = zeep::json::element;
	json screens;

	auto s = screen_service::instance().get_all_screens();
	s.erase(std::remove_if(s.begin(), s.end(), [username](auto& si) { return si.scientist != username; }), s.end());

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
	sub.put("screens", screens);

	get_template_processor().create_reply_from_template("user-screens.html", sub, reply);
}

// --------------------------------------------------------------------

screen_user_rest_controller::screen_user_rest_controller()
	: zeep::http::rest_controller("/")
{
	// map_post_request("screen", &screen_user_rest_controller::create_screen, "screen");
	map_get_request("screen/{id}", &screen_user_rest_controller::retrieve_screen, "id");
	map_put_request("screen/{id}", &screen_user_rest_controller::update_screen, "id", "screen");
	map_delete_request("screen/{id}", &screen_user_rest_controller::delete_screen, "id");
}

// uint32_t screen_user_rest_controller::create_screen(const screen& screen)
// {
// 	return screen_service::instance().create_screen(screen);
// }

screen_info screen_user_rest_controller::retrieve_screen(const std::string& name)
{
	if (not user_service::instance().allow_screen_for_user(name, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	return screen_service::instance().retrieve_screen(name);
}

void screen_user_rest_controller::update_screen(const std::string& name, const screen_info& screen)
{
	if (not user_service::instance().allow_screen_for_user(name, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	screen_service::instance().update_screen(name, screen);
}

void screen_user_rest_controller::delete_screen(const std::string& name)
{
	if (not user_service::instance().allow_screen_for_user(name, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	screen_service::instance().delete_screen(name);
}
