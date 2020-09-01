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

screen_data_cache::screen_data_cache(ScreenType type, const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
	: m_type(type), m_assembly(assembly), m_trim_length(trim_length), m_mode(mode), m_cutOverlap(cutOverlap)
	, m_geneStart(geneStart), m_geneEnd(geneEnd)
{
	m_transcripts = loadTranscripts(assembly, mode, geneStart, geneEnd, cutOverlap);
}

screen_data_cache::~screen_data_cache()
{
}

// --------------------------------------------------------------------

ip_screen_data_cache::ip_screen_data_cache(ScreenType type, const std::string& assembly, short trim_length, Mode mode,
		bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
	: screen_data_cache(type, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd)
	, m_direction(direction), m_data(nullptr)
{
	auto screens = screen_service::instance().get_all_screens_for_type(type);
	auto screenDataDir = screen_service::instance().get_screen_data_dir();

	for (auto& screen: screens)
		m_screens.push_back({ screen.name, false, screen.ignore });

	size_t N = m_transcripts.size();
	size_t M = m_screens.size();
	
	m_data = new data_point[N * M];
	memset(m_data, 0, N * M * sizeof(data_point));

	// for (size_t si = 0; si < m_screens.size(); ++si)
	parallel_for(m_screens.size(), [&](size_t si)
	{
		try
		{
			auto sd = m_data + si * m_transcripts.size();

			fs::path screenDir = screenDataDir / m_screens[si].name;
			auto data = IPPAScreenData::load(screenDir);
			
			std::vector<Insertions> lowInsertions, highInsertions;

			data->analyze(m_assembly, m_trim_length, m_transcripts, lowInsertions, highInsertions);

			auto dp = data->dataPoints(m_transcripts, lowInsertions, highInsertions, m_direction);

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

			m_screens[si].filled = true;
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
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

	auto si = std::find_if(m_screens.begin(), m_screens.end(), [screen](auto& si) { return si.name == screen; });
	if (si == m_screens.end() or not si->filled)
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
	auto si = std::find_if(m_screens.begin(), m_screens.end(), [screen](auto& si) { return si.name == screen; });
	if (si == m_screens.end() or si->filled == false)
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
			if (m_screens[si].ignore)
				continue;

			auto& sp = m_data[si * N + ti];
			if (sp.fcpv <= pvCutOff and (dp.mi < 1) == (sp.mi < 1))
				++c;
		}

		double cd = std::pow(c, 0.001) - 1;

		int colour = static_cast<int>(std::ceil(10 * cd / r));

		result.push_back(gene_uniqueness{m_transcripts[ti].geneName, colour});
	}

	return result;
}

std::vector<gene_finder_data_point> ip_screen_data_cache::find_gene(const std::string& gene, const std::set<std::string>& allowedScreens)
{
	auto gi = std::find_if(m_transcripts.begin(), m_transcripts.end(), [gene](auto& t) { return t.geneName == gene; });
	if (gi == m_transcripts.end())
		return {};

	size_t ti = gi - m_transcripts.begin();
	size_t N = m_transcripts.size();

	std::vector<gene_finder_data_point> result;

	for (size_t si = 0; si < m_screens.size(); ++si)
	{
		const auto& [name, filled, ignore] = m_screens[si];

		if (filled and /*not ignore and*/ allowedScreens.count(name))
		{
			size_t ix = si * N + ti;

			gene_finder_data_point p;

			p.screen = name;
			p.fcpv = m_data[ix].fcpv;
			p.mi = m_data[ix].mi;
			p.insertions = m_data[ix].high + m_data[ix].low;
			p.replicate = 0;

			result.push_back(std::move(p));
		}
	}

	return result;
}

std::vector<similar_data_point> ip_screen_data_cache::find_similar(const std::string& gene, float pvCutOff, float zscoreCutOff)
{
	size_t geneCount = m_transcripts.size(), screenCount = m_screens.size();

	auto gi = std::find_if(m_transcripts.begin(), m_transcripts.end(), [gene](auto& t) { return t.geneName == gene; });
	if (gi == m_transcripts.end())
		return {};

	size_t qg_ix = gi - m_transcripts.begin();

	std::vector<similar_data_point> result;

	for (auto anti: { false, true })
	{
		std::vector<similar_data_point> hits;
		hits.reserve(geneCount);
		
		double distanceSum = 0;

		for (size_t tg_ix = 0; tg_ix < geneCount; ++tg_ix)
		{
			long double sum = 0;
			bool data = false;

			for (size_t s_ix = 0; s_ix < screenCount; ++s_ix)
			{
				float miQ, miT;

				auto& a = m_data[s_ix * geneCount + tg_ix];
				auto& b = m_data[s_ix * geneCount + qg_ix];

				data = true;

				miQ = a.mi ? std::log2(a.mi) : 0;
				miT = b.mi ? std::log2(b.mi) : 0;

				if (anti)
					sum += (miQ + miT) * (miQ + miT);
				else
					sum += (miQ - miT) * (miQ - miT);
			}

			if (not data)
				continue;

			double d = static_cast<double>(sqrt(sum));

			hits.push_back(similar_data_point{ m_transcripts[tg_ix].geneName, static_cast<float>(d), 0.f, anti });

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

class distance_map
{
  public:
	distance_map(size_t dim)
		: m_dim(dim), m_data(dim * (dim - 1))
	{
	}
	
	double operator()(size_t a, size_t b) const
	{
		if (a == b)
			return 0;

		if (b < a)
			std::swap(a, b);
		
		size_t ix = b + a * m_dim - a * (a + 1) / 2;
		
		assert(ix < m_data.size());
		return m_data[ix];
	}

	void set(size_t a, size_t b, double value)
	{
		assert(a != b);

		if (b < a)
			std::swap(a, b);
		
		size_t ix = b + a * m_dim - a * (a + 1) / 2;
		
		assert(ix < m_data.size());
		m_data[ix] = value;
	}

  private:
	size_t m_dim;
	std::vector<double> m_data;
};

std::vector<cluster> ip_screen_data_cache::find_clusters(float pvCutOff, size_t minPts, float eps, size_t NNs)
{
	size_t geneCount = m_transcripts.size(), screenCount = m_screens.size(), dataCount = geneCount * screenCount;

	// std::vector<int> gene_detail_ids, screen_ids, geneIndex, screenIndex;
	// tie(gene_detail_ids, screen_ids, geneIndex, screenIndex) = load_data(genomeID, pvCutOff);

	// load mutational index data
	std::vector<float> data(dataCount);

	for (size_t g_ix = 0; g_ix < geneCount; ++g_ix)
	{
		for (size_t s_ix = 0; s_ix < screenCount; ++s_ix)
		{
			auto& d = m_data[s_ix * geneCount + g_ix];
			if (d.mi)
				data[g_ix * screenCount + s_ix] = std::log2(d.mi);
		}
	}

	auto distance = [&](int geneIxA, int geneIxB) -> float
	{
		size_t a_ix = geneIxA * screenCount;
		size_t b_ix = geneIxB * screenCount;

		const float* a = &data[a_ix];
		const float* b = &data[b_ix];

		int na_significant = 0, nb_significant = 0, n_significant = 0;
		int n_missing = 0, n_mismatch = 0, n_match = 0;

		for (size_t i = 0; i < screenCount; ++i)
		{
			auto ai = a[i];
			auto bi = b[i];

			if (ai) ++na_significant;
			if (bi) ++nb_significant;
			if (ai and bi) ++n_significant;

			if (ai == 0 or bi == 0)
			{
				if (ai != bi)
					++n_missing;
				continue;
			}

			// sum += (ai - bi) * (ai - bi);
			if ((ai < 0) == (bi < 0))
				++n_match;
			else
				++n_mismatch;
		}

		float score = 0;
		if (n_significant > 0)
		{
			float f1 = (2.0f * n_significant) / (na_significant + nb_significant);
			float f2 = (0.2f * n_missing) / n_significant;

			int match = n_match - 0.75f * n_mismatch;
			if (match < 0)
				match = 0;

			score = match * (f1 - f2);
		}

		float distance = screenCount - score;
		if (distance < 0)
			distance = 0;
		if (distance > screenCount)
			distance = screenCount;

		return distance / screenCount;
	};

	// distance matrix for the primary distance

	distance_map D(geneCount);

	parallel_for(geneCount, [&](int x)
	{
		for (int y = x + 1; y < geneCount; ++y)
			D.set(x, y, distance(x, y));
	});

	// DBSCAN algorithm

	// using secondary distance 
	// https://doi.org/10.1007%2F978-3-642-13818-8_34

	enum : int { noise = -1, undefined = 0 };

	struct gene
	{
		int geneID;
		std::vector<int> nn;
	};

	std::vector<gene> genes(geneCount);

	if (NNs > 0)
	{
		// for (size_t i = 0; i < geneCount; ++i)
		parallel_for(geneCount, [&](size_t i)
		{
			auto& g = genes[i];
			g.geneID = i;

			std::vector<std::tuple<int,double>> v;
			auto cmp = [](auto a, auto b) { return std::get<1>(a) < std::get<1>(b); };

			v.reserve(NNs);

			for (size_t j = 0; j < geneCount; ++j)
			{
				if (i == j)
					continue;

				double d = D(i, j);
				// if (d >= screenCount)
				// 	continue;	// do not add as a neighbour if there is no contact

				if (v.size() < NNs)
				{
					v.emplace_back(j, d);
					std::push_heap(v.begin(), v.end(), cmp);
				}
				else if (std::get<1>(v.front()) > d)
				{
					std::pop_heap(v.begin(), v.end(), cmp);
					v.back() = std::make_tuple(j, d);
					std::push_heap(v.begin(), v.end(), cmp);
				}
			}
			
			g.nn.reserve(NNs);
			for (size_t k = 0; k < NNs; ++k)
				g.nn.push_back(std::get<0>(v[k]));

			std::sort(g.nn.begin(), g.nn.end());
		});
	}

	auto secD = [&genes,NNs,&D](auto a, auto b) -> float
	{
		// shortcut
		if (NNs == 0)
			return D(a, b);

		assert(a < genes.size());
		assert(b < genes.size());

		auto ai = genes[a].nn.begin();
		auto bi = genes[b].nn.begin();

		size_t s = 0;
		while (ai != genes[a].nn.end() and bi != genes[b].nn.end())
		{
			if (*ai == *bi)
			{
				++s;
				++ai;
				++bi;
			}
			else if (*ai < *bi)
				++ai;
			else
				++bi;
		}

		return 1 - static_cast<float>(s) / NNs;
	};

	// the actual DBSCAN implementation

	int clusterNr = 0;

	std::vector<int> label(geneCount, undefined);

	auto RangeQuery = [&secD,&D,eps,geneCount](size_t q)
	{
		std::set<int> result;

		for (size_t p = 0; p < geneCount; ++p)
		{
			if (p != q and secD(p, q) <= eps and D(p, q) < 1)
				result.insert(p);
		}
		
		return result;
	};

	for (size_t p = 0; p < geneCount; ++p)
	{
		if (label[p] != undefined)
			continue;
		
		auto neighbours = RangeQuery(p);
		if (neighbours.size() < minPts)
		{
			label[p] = noise;
			continue;
		}

		label[p] = ++clusterNr;

		auto S = neighbours;
		S.erase(p);

		while (not S.empty())
		{
			size_t q = *S.begin();
			S.erase(q);

			if (label[q] != undefined and label[q] != noise)
				continue;
			
			label[q] = clusterNr;

			auto N = RangeQuery(q);
			if (N.size() >= minPts)
			{
				for (size_t r: N)
				{
					if (label[r] != clusterNr)
						S.insert(r);
				}
			}
		}
	}

	// sort the clusters
	std::vector<std::tuple<std::vector<int>,double>> clusters;
	for (size_t i = 0; i < clusterNr; ++i)
	{
		std::vector<int> genes;

		for (size_t j = 0; j < label.size(); ++j)
		{
			if (label[j] == i)
				genes.push_back(j);
		}

		// // on variance
		// double sum = 0;
		// size_t N = 0;
		// for (auto gi: genes)
		// {
		// 	for (auto gj: genes)
		// 	{
		// 		if (gi == gj)
		// 			continue;
		// 		sum += distance(gi, gj);
		// 		N += 1;
		// 	}
		// }

		// double avg = sum / N;
		// double sumSq = 0;

		// for (auto gi: genes)
		// {
		// 	for (auto gj: genes)
		// 	{
		// 		if (gi == gj)
		// 			continue;
		// 		sumSq += (distance(gi, gj) - avg) * (distance(gi, gj) - avg);
		// 	}
		// }

		// double variance = sumSq / N;

		// on overlap
		std::vector<bool> overlap(screenCount, true);
		for (auto gi: genes)
		{
			float* gd = &data[gi * screenCount];
			for (size_t si = 0; si < screenCount; ++si)
				if (gd[si] == 0)
					overlap[si] = false;
		}

		size_t overlapN = 0;
		for (bool b: overlap)
			if (b) ++overlapN;
		double variance = 1 - static_cast<double>(overlapN) / screenCount;

		clusters.emplace_back(move(genes), variance);
	}

	std::sort(clusters.begin(), clusters.end(), [](auto a, auto b) { return std::get<1>(a) < std::get<1>(b); });

	std::vector<cluster> result;

	for (auto& sc: clusters)
	{
		cluster c;

		c.variance = std::get<1>(sc);

		for (auto g: std::get<0>(sc))
			c.genes.push_back(m_transcripts[g].geneName);

		if (not c.genes.empty())
			result.push_back(std::move(c));
	}

	return result;
}

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

bool screen_service::is_owner(const std::string& name, const std::string& username)
{
	bool result = false;

	std::ifstream manifest(m_screen_data_dir / name / "manifest.json");

	if (manifest.is_open())
	{
		try
		{
			zeep::json::element info;
			zeep::json::parse_json(manifest, info);

			screen_info screen;
			zeep::json::from_element(info, screen);

			result = screen.scientist == username;
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
		}
	}
	
	return result;
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

std::shared_ptr<ip_screen_data_cache> screen_service::get_screen_data(const ScreenType type, const std::string& assembly, short trim_length,
	Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	std::unique_lock lock(m_mutex);

	auto i = std::find_if(m_ip_data_cache.begin(), m_ip_data_cache.end(),
		std::bind(&ip_screen_data_cache::is_for, std::placeholders::_1, type, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd, direction));

	std::shared_ptr<ip_screen_data_cache> result;

	if (i != m_ip_data_cache.end())
		result = *i;
	else
	{
		result = std::make_shared<ip_screen_data_cache>(type, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd, direction);
		m_ip_data_cache.emplace_back(result);
	}
	
	return result;
}

std::vector<ip_data_point> screen_service::get_data_points(const ScreenType type, const std::string& screen, const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
{
	auto i = std::find_if(m_ip_data_cache.begin(), m_ip_data_cache.end(),
		std::bind(&ip_screen_data_cache::is_for, std::placeholders::_1, type, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd, direction));

	if (i != m_ip_data_cache.end() and (*i)->contains_data_for_screen(screen))
		return (*i)->data_points(screen);

	fs::path screenDir = m_screen_data_dir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	auto data = IPPAScreenData::load(screenDir);
	
	std::vector<ip_data_point> result;

	for (auto& dp: data->dataPoints(assembly, mode, cutOverlap, geneStart, geneEnd, direction))
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
