//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include <filesystem>

#include <pqxx/pqxx>

#include <zeep/crypto.hpp>

#include "mrsrc.hpp"

#include "user-service.hpp"
#include "screen-service.hpp"
#include "db-connection.hpp"
#include "job-scheduler.hpp"
#include "bowtie.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

extern int VERBOSE;

// --------------------------------------------------------------------

class gene_ranking
{
  public:
	static gene_ranking& instance()
	{
		static gene_ranking s_instance;
		return s_instance;
	}

	int operator()(const std::string& gene) const
	{
		auto i = m_ranked.find(gene);
		return i != m_ranked.end() ? i->second : -1;
	}

  private:
	gene_ranking()
	{
		mrsrc::istream data("ranked.txt");

		std::string line;

		int nr = 0;
		while (std::getline(data, line))
			m_ranked[line] = nr++;
	}

	std::map<std::string,int> m_ranked;
};

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

bool screen_data_cache::is_up_to_date() const
{
	auto screens = screen_service::instance().get_all_screens_for_type(m_type);
	auto screenDataDir = screen_service::instance().get_screen_data_dir();

	std::set<std::string> current;
	for (auto& s: screens)
		current.insert(s.name);

	bool result = true;

	for (auto& s: m_screens)
	{
		if (not current.count(s.name))
		{
			result = false;
			break;
		}

		current.erase(s.name);
	}

	result = result and current.empty();

	return result;
}

// --------------------------------------------------------------------

ip_screen_data_cache::ip_screen_data_cache(ScreenType type, const std::string& assembly, short trim_length, Mode mode,
		bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction)
	: screen_data_cache(type, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd)
	, m_direction(direction), m_data(nullptr)
{
	auto screens = screen_service::instance().get_all_screens_for_type(type);
	auto screenDataDir = screen_service::instance().get_screen_data_dir();

	uint32_t data_offset = 0;
	size_t N = m_transcripts.size();
	size_t M = screens.size();
	
	for (auto& screen: screens)
	{
		m_screens.push_back({ screen.name, false, screen.ignore, 0, data_offset });
		data_offset += N;
	}

	m_data = new data_point[N * M];
	memset(m_data, 0, N * M * sizeof(data_point));

	// for (size_t si = 0; si < m_screens.size(); ++si)
	for (auto &screen : m_screens)
	{
		try
		{
			fs::path screenDir = screenDataDir / screen.name;
			auto data = IPPAScreenData::load(screenDir);

			auto d_data = m_data + screen.data_offset;

			auto cf = get_cache_file_path(screen.name);
			if (fs::exists(cf) and fs::file_size(cf) == N * sizeof(data_point))
			{
				std::ifstream fcf(cf, std::ios::binary);

				fcf.read(reinterpret_cast<char*>(d_data), N * sizeof(data_point));

				screen.filled = true;
				continue;
			}

			std::vector<Insertions> lowInsertions, highInsertions;

			data->analyze(m_assembly, m_trim_length, m_transcripts, lowInsertions, highInsertions);

			auto dp = data->dataPoints(m_transcripts, lowInsertions, highInsertions, m_direction);

			for (size_t ti = 0; ti < N; ++ti)
			{
				auto& d = d_data[ti];
				auto& p = dp[ti];

				d.pv = p.pv;
				d.fcpv = p.fcpv;
				d.mi = p.mi;
				d.low = p.low;
				d.high = p.high;
			}

			screen.filled = true;

			if (fs::exists(cf))
				fs::remove(cf);
			
			std::ofstream fcf(cf, std::ios::binary);

			fcf.write(reinterpret_cast<char*>(d_data), N * sizeof(data_point));
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
		}
	}
}

ip_screen_data_cache::~ip_screen_data_cache()
{
	delete [] m_data;
}

fs::path ip_screen_data_cache::get_cache_file_path(const std::string &screen_name) const
{
	std::stringstream ss;
	ss << "cache"
	   << '-' << zeep::value_serializer<Mode>::to_string(m_mode)
	   << '-' << (m_cutOverlap ? "cut" : "no-cut")
	   << '-' << m_geneStart
	   << '-' << m_geneEnd
	   << '-' << zeep::value_serializer<Direction>::to_string(m_direction);

	return screen_service::instance().get_screen_data_dir() / screen_name / m_assembly / std::to_string(m_trim_length) / ss.str();
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

	auto& rank = gene_ranking::instance();

	for (size_t i = 0; i < N; ++i)
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

		auto r = rank(p.gene);
		if (r >= 0)
			p.rank.emplace(r);

		result.push_back(std::move(p));
	}
	
	return result;
}

std::vector<gene_uniqueness> ip_screen_data_cache::uniqueness(const std::string& screen, float pvCutOff, bool singlesided)
{
	auto si = std::find_if(m_screens.begin(), m_screens.end(), [screen](auto& si) { return si.name == screen; });
	if (si == m_screens.end() or si->filled == false)
		return {};

	size_t screenIx = si - m_screens.begin();
	size_t N = m_transcripts.size();
	auto data = m_data + screenIx * N;

	std::vector<gene_uniqueness> result;

	// double r = std::pow(m_screens.size(), 0.001) - 1;
	size_t screenCount = std::count_if(m_screens.begin(), m_screens.end(), [](auto const &s) { return not s.ignore; });
	size_t minCount = screenCount, maxCount = 0;

	for (size_t ti = 0; ti < N; ++ti)
	{
		auto& dp = data[ti];

		if (dp.fcpv > pvCutOff)
			continue;
		
		size_t geneCount = 0;
		for (size_t si = 0; si < m_screens.size(); ++si)
		{
			if (m_screens[si].ignore)
				continue;

			auto& sp = m_data[si * N + ti];

			if (sp.fcpv > pvCutOff)
				continue;

			if (singlesided and (dp.mi < 1) != (sp.mi < 1))
				continue;

			++geneCount;
		}

		if (minCount > geneCount)
			minCount = geneCount;
		
		if (maxCount < geneCount)
			maxCount = geneCount;

		result.push_back(gene_uniqueness{m_transcripts[ti].geneName, 0, geneCount});
	}

	double r = std::pow(maxCount - minCount, 0.001) - 1;
	for (auto &unique : result)
	{
		auto c = unique.count - minCount;
		double cd = std::pow(c, 0.001) - 1;
		unique.colour = static_cast<int>(std::ceil(10 * cd / r));
	}

	return result;
}

std::vector<ip_gene_finder_data_point> ip_screen_data_cache::find_gene(const std::string& gene, const std::set<std::string>& allowedScreens)
{
	auto gi = std::find_if(m_transcripts.begin(), m_transcripts.end(), [gene](auto& t) { return t.geneName == gene; });
	if (gi == m_transcripts.end())
		return {};

	size_t ti = gi - m_transcripts.begin();
	size_t N = m_transcripts.size();

	std::vector<ip_gene_finder_data_point> result;

	for (size_t si = 0; si < m_screens.size(); ++si)
	{
		const auto& [name, filled, ignore, ignore_2, ignore_3, ignore_4] = m_screens[si];

		if (filled and /*not ignore and*/ allowedScreens.count(name))
		{
			size_t ix = si * N + ti;

			ip_gene_finder_data_point p{};

			p.screen = name;
			p.fcpv = m_data[ix].fcpv;
			p.mi = m_data[ix].mi;
			p.insertions = m_data[ix].high + m_data[ix].low;

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

	parallel_for(geneCount, [&](size_t x)
	{
		for (size_t y = x + 1; y < geneCount; ++y)
			D.set(x, y, distance(x, y));
	});

	// DBSCAN algorithm

	// using secondary distance 
	// https://doi.org/10.1007%2F978-3-642-13818-8_34

	enum : size_t { noise = ~0UL, undefined = 0 };

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

	size_t clusterNr = 0;

	std::vector<size_t> label(geneCount, undefined);

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

sl_screen_data_cache::sl_screen_data_cache(const std::string &assembly, short trim_length,
	Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd)
	: screen_data_cache(ScreenType::SyntheticLethal, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd)
{
	auto screens = screen_service::instance().get_all_screens_for_type(m_type);
	auto screenDataDir = screen_service::instance().get_screen_data_dir();

	size_t N = m_transcripts.size();
	size_t M = screens.size();
	size_t O = 0;
	
	uint32_t data_offset = 0;

	for (auto& screen: screens)
	{
		m_screens.push_back({
			screen.name, false, screen.ignore,
			static_cast<uint8_t>(screen.files.size()),
			data_offset,
			static_cast<uint32_t>(O * M * N)
		});
		data_offset += N;
		O += screen.files.size();
	}

	m_data = new data_point[N * M];
	memset(m_data, 0, N * M * sizeof(data_point));

	m_replicate_data = new data_point_replicate[N * M * O];
	memset(m_replicate_data, 0, N * M * O * sizeof(data_point_replicate));

	std::string control = "ControlData-HAP1";
	auto controlDataPtr = SLScreenData::load(screenDataDir / control);
	auto controlData = static_cast<SLScreenData*>(controlDataPtr.get());

	filterOutExons(m_transcripts);

	// reorder transcripts based on chr > end-position, makes code easier and faster
	std::sort(m_transcripts.begin(), m_transcripts.end(), [](auto& a, auto& b)
	{
		int d = a.chrom - b.chrom;
		if (d == 0)
			d = a.start() - b.start();
		return d < 0;
	});

// #warning "make groupSize a parameter"
	// unsigned groupSize = 500;
	unsigned groupSize = 200;

	auto normalizedControlInsertions = controlData->loadNormalizedInsertions(assembly, trim_length, m_transcripts, groupSize);

	for (auto &screen : m_screens)
	{
		if (VERBOSE)
			std::cerr << "loading " << screen.name << std::endl;

		try
		{
			fs::path screenDir = screenDataDir / screen.name;

			auto dataPtr = SLScreenData::load(screenDir);
			auto data = static_cast<SLScreenData*>(dataPtr.get());

			auto d_data = m_data + screen.data_offset;

			data_point_replicate *r_data[4];
			r_data[0] = m_replicate_data + screen.replicate_offset;
			r_data[1] = r_data[0] + N;
			r_data[2] = r_data[1] + N;
			r_data[3] = r_data[2] + N;

			auto cd_data = d_data;	// copy, for cache
			auto cr_data = r_data[0];

			auto cf = get_cache_file_path(screen.name);
			if (fs::exists(cf) and fs::file_size(cf) == N * sizeof(data_point) + N * screen.file_count * sizeof(data_point_replicate))
			{
				std::ifstream fcf(cf, std::ios::binary);

				fcf.read(reinterpret_cast<char*>(cd_data), N * sizeof(data_point));
				fcf.read(reinterpret_cast<char*>(cr_data), N * screen.file_count * sizeof(data_point_replicate));

				d_data += N;

				screen.filled = true;
				continue;
			}

			// ----------------------------------------------------------------------

			auto dp = data->dataPoints(assembly, trim_length, m_transcripts, normalizedControlInsertions, groupSize);

			for (size_t ti = 0; ti < N; ++ti)
			{
				auto &d = d_data[ti];
				auto &p = dp[ti];

				d.odds_ratio = p.oddsRatio;
				// d.sense_ratio = p.senseRatio;
				// d.control_sense_ratio = p.controlSenseRatio;
				d.control_binom = p.controlBinom;
				// d.consistent = p.consistent;

				for (size_t ri = 0; ri < p.replicates.size(); ++ri)
				{
					auto &rp = p.replicates[ri];
					auto &rd = r_data[ri][ti];

					rd.sense = rp.sense_normalized;
					rd.antisense = rp.antisense_normalized;
					rd.pv[0] = rp.ref_pv[0];
					rd.pv[1] = rp.ref_pv[1];
					rd.pv[2] = rp.ref_pv[2];
					rd.pv[3] = rp.ref_pv[3];
					rd.binom_fdr = rp.binom_fdr;
				}
			}

			screen.filled = true;

			if (fs::exists(cf))
				fs::remove(cf);
			
			std::ofstream fcf(cf, std::ios::binary);

			fcf.write(reinterpret_cast<char*>(cd_data), N * sizeof(data_point));
			fcf.write(reinterpret_cast<char*>(cr_data), N * screen.file_count * sizeof(data_point_replicate));
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
		}
	};
}

sl_screen_data_cache::~sl_screen_data_cache()
{
	delete[] m_data;
	delete[] m_replicate_data;
}

fs::path sl_screen_data_cache::get_cache_file_path(const std::string &screen_name) const
{
	std::stringstream ss;
	ss << "cache"
	   << '-' << zeep::value_serializer<Mode>::to_string(m_mode)
	   << '-' << (m_cutOverlap ? "cut" : "no-cut")
	   << '-' << m_geneStart
	   << '-' << m_geneEnd;

	return screen_service::instance().get_screen_data_dir() / screen_name / m_assembly / std::to_string(m_trim_length) / ss.str();
}

std::vector<sl_data_point> sl_screen_data_cache::data_points(const std::string &screen)
{
	std::vector<sl_data_point> result;

	size_t N = m_transcripts.size();

	auto si = std::find_if(m_screens.begin(), m_screens.end(), [screen](auto& si) { return si.name == screen; });
	if (si == m_screens.end() or not si->filled)
		return {};

	// screen data
	size_t screenIx = si - m_screens.begin();
	auto d_data = m_data + m_screens[screenIx].data_offset;

	data_point_replicate *r_data[4];
	r_data[0] = m_replicate_data + m_screens[screenIx].replicate_offset;
	r_data[1] = r_data[0] + N;
	r_data[2] = r_data[1] + N;
	r_data[3] = r_data[2] + N;

	// reference/control data
	std::string control = "ControlData-HAP1";
	si = std::find_if(m_screens.begin(), m_screens.end(), [control](auto& si) { return si.name == control; });
	if (si == m_screens.end() or not si->filled)
		throw std::runtime_error("Missing control data");
	size_t controlScreenIx = si - m_screens.begin();

	data_point_replicate *cr_data[4];
	cr_data[0] = m_replicate_data + m_screens[controlScreenIx].replicate_offset;
	cr_data[1] = cr_data[0] + N;
	cr_data[2] = cr_data[1] + N;
	cr_data[3] = cr_data[2] + N;

	for (size_t ti = 0; ti < N; ++ti)
	{
		auto &dp = d_data[ti];

		sl_data_point p{};

		enum class ConsistencyCheck {
			Undefined, Up, Down, Inconsistent
		} check = ConsistencyCheck::Undefined;

		size_t s_g = 0, a_g = 0;

		for (size_t j = 0; j < m_screens[screenIx].file_count; ++j)
		{
			sl_data_replicate rp{};

			auto &nc = r_data[j][ti];

			rp.sense = nc.sense;
			rp.antisense = nc.antisense;
			rp.binom_fdr = nc.binom_fdr;
			rp.ref_pv[0] = nc.pv[0];
			rp.ref_pv[1] = nc.pv[1];
			rp.ref_pv[2] = nc.pv[2];
			rp.ref_pv[3] = nc.pv[3];

			p.replicates.emplace_back(std::move(rp));

			s_g += nc.sense;
			a_g += nc.antisense;

			// check consistency
			if (check == ConsistencyCheck::Inconsistent)
				continue;
			
			for (size_t k = 0; k < 4; ++k)
			{
				auto &ncc = cr_data[k][ti];

				bool up = ((1.0f + nc.sense) / (2.0f + nc.sense + nc.antisense)) <
						((1.0f + ncc.sense) / (2.0f + ncc.sense + ncc.antisense));
				
				if (up)
				{
					if (check == ConsistencyCheck::Down)
					{
						check = ConsistencyCheck::Inconsistent;
						break;
					}
					else
						check = ConsistencyCheck::Up;
				}
				else
				{
					if (check == ConsistencyCheck::Up)
					{
						check = ConsistencyCheck::Inconsistent;
						break;
					}
					else
						check = ConsistencyCheck::Down;
				}
			}
		}

		size_t s_wt = 0, a_wt = 0;

		for (size_t j = 0; j < 4; ++j)
		{
			s_wt += cr_data[j][ti].sense;
			a_wt += cr_data[j][ti].antisense;
		}

		p.gene = m_transcripts[ti].geneName;
		p.consistent = check != ConsistencyCheck::Inconsistent;
		p.controlBinom = dp.control_binom;
		p.oddsRatio = dp.odds_ratio;
		p.senseRatio = (1.0f + s_g) / (2.0f + s_g + a_g);
		p.controlSenseRatio = (1.0f + s_wt) / (2.0f + s_wt + a_wt);

		result.push_back(std::move(p));
	}
	
	return result;
}

std::vector<sl_gene_finder_data_point> sl_screen_data_cache::find_gene(const std::string &gene, const std::set<std::string> &allowedScreens)
{
	auto gi = std::find_if(m_transcripts.begin(), m_transcripts.end(), [gene](auto& t) { return t.geneName == gene; });
	if (gi == m_transcripts.end())
		return {};

	size_t ti = gi - m_transcripts.begin();
	size_t N = m_transcripts.size();

	std::vector<sl_gene_finder_data_point> result;

	// reference/control data
	std::string control = "ControlData-HAP1";
	auto si = std::find_if(m_screens.begin(), m_screens.end(), [control](auto& si) { return si.name == control; });
	if (si == m_screens.end() or not si->filled)
		throw std::runtime_error("Missing control data");
	size_t controlScreenIx = si - m_screens.begin();

	data_point_replicate *cr_data[4];
	cr_data[0] = m_replicate_data + m_screens[controlScreenIx].replicate_offset;
	cr_data[1] = cr_data[0] + N;
	cr_data[2] = cr_data[1] + N;
	cr_data[3] = cr_data[2] + N;

	for (size_t si = 0; si < m_screens.size(); ++si)
	{
		auto &screen = m_screens[si];
		const auto& [name, filled, ignore, ignore_2, ignore_3, ignore_4] = screen;

		if (filled and /*not ignore and*/ allowedScreens.count(name))
		{
			auto d_data = m_data + screen.data_offset;
			auto &dp = d_data[ti];

			data_point_replicate *r_data[4];
			r_data[0] = m_replicate_data + screen.replicate_offset;
			r_data[1] = r_data[0] + N;
			r_data[2] = r_data[1] + N;
			r_data[3] = r_data[2] + N;

			sl_gene_finder_data_point p{};

			p.screen = name;

			enum class ConsistencyCheck {
				Undefined, Up, Down, Inconsistent
			} check = ConsistencyCheck::Undefined;

			size_t s_g = 0, a_g = 0;

			for (size_t j = 0; j < screen.file_count; ++j)
			{
				auto &nc = r_data[j][ti];

				p.senseRatioPerReplicate.push_back((1.0f + nc.sense) / (2 + nc.sense + nc.antisense));

				s_g += nc.sense;
				a_g += nc.antisense;

				// check consistency
				if (check == ConsistencyCheck::Inconsistent)
					continue;
				
				for (size_t k = 0; k < 4; ++k)
				{
					auto &ncc = cr_data[k][ti];

					bool up = ((1.0f + nc.sense) / (2.0f + nc.sense + nc.antisense)) <
							((1.0f + ncc.sense) / (2.0f + ncc.sense + ncc.antisense));
					
					if (up)
					{
						if (check == ConsistencyCheck::Down)
						{
							check = ConsistencyCheck::Inconsistent;
							break;
						}
						else
							check = ConsistencyCheck::Up;
					}
					else
					{
						if (check == ConsistencyCheck::Up)
						{
							check = ConsistencyCheck::Inconsistent;
							break;
						}
						else
							check = ConsistencyCheck::Down;
					}
				}
			}

			// size_t s_wt = 0, a_wt = 0;

			// for (size_t j = 0; j < 4; ++j)
			// {
			// 	s_wt += cr_data[j][ti].sense;
			// 	a_wt += cr_data[j][ti].antisense;
			// }

			// p.controlBinom = dp.control_binom;
			// p.controlSenseRatio = (1.0f + s_wt) / (2.0f + s_wt + a_wt);

			p.senseRatio = (1.0f + s_g) / (2.0f + s_g + a_g);
			p.oddsRatio = dp.odds_ratio;
			p.consistent = check != ConsistencyCheck::Inconsistent;

			result.push_back(std::move(p));
		}
	}

	return result;
}

// --------------------------------------------------------------------

std::unique_ptr<screen_service> screen_service::s_instance;

void screen_service::init(const std::string& screen_data_dir, const std::string &screen_cache_dir)
{
	assert(not s_instance);
	s_instance.reset(new screen_service(screen_data_dir, screen_cache_dir));
}

screen_service& screen_service::instance()
{
	assert(s_instance);
	return *s_instance;
}

screen_service::screen_service(const std::string& screen_data_dir, const std::string &screen_cache_dir)
	: m_screen_data_dir(screen_data_dir)
	, m_screen_cache_dir(screen_cache_dir)
{
	if (not fs::exists(m_screen_data_dir))
		throw std::runtime_error("Screen data directory " + screen_data_dir + " does not exist");
	if (not fs::exists(m_screen_cache_dir))
		std::cerr << "Screen cache directory " << screen_cache_dir << " does not exist" << std::endl;
}

std::vector<screen_info> screen_service::get_all_screens() const
{
	std::vector<screen_info> result;

	for (auto si: fs::directory_iterator(m_screen_data_dir))
	{
		if (not si.is_directory())
			continue;

		std::error_code ec;
		if (not fs::exists(si.path() / "manifest.json", ec))
			continue;

		try
		{
			auto screen = ScreenData::loadManifest(si.path());
			screen.status = job_scheduler::instance().get_job_status_for_screen(screen.name);
			result.push_back(screen);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Could not load screen: " << si.path().filename() << ": " << e.what() << std::endl;
		}
	}

	return result;
}

std::vector<screen_info> screen_service::get_all_screens_for_type(ScreenType type) const
{
	auto screens = get_all_screens();
	screens.erase(std::remove_if(screens.begin(), screens.end(), [type](auto& s) { return s.type != type; }), screens.end());
	return screens;
}

std::vector<screen_info> screen_service::get_all_screens_for_user(const std::string& username) const
{
	auto screens = get_all_screens();

	auto& user_service = user_service::instance();
	auto user = user_service.retrieve_user(username);

	screens.erase(std::remove_if(screens.begin(), screens.end(), [user](auto& screen) {

		if (screen.scientist == user.username)
			return false;

		for (auto& g: screen.groups)
		{
			if (std::find(user.groups.begin(), user.groups.end(), g) != user.groups.end())
				return false;
		}

		return true;
	}), screens.end());

	return screens;
}

std::vector<screen_info> screen_service::get_all_screens_for_user_and_type(const std::string& user, ScreenType type) const
{
	auto screens = get_all_screens_for_user(user);
	screens.erase(std::remove_if(screens.begin(), screens.end(), [type](auto& s) { return s.type != type; }), screens.end());
	return screens;
}

std::set<std::string> screen_service::get_allowed_screens_for_user(const user& user) const
{
	std::set<std::string> result;

	for (auto& si: fs::directory_iterator(m_screen_data_dir))
	{
		if (not si.is_directory())
			continue;
		
		std::error_code ec;
		if (not fs::exists(si.path() / "manifest.json", ec))
			continue;
		
		try
		{
			auto screen = ScreenData::loadManifest(si.path());

			if (screen.scientist == user.username or user.admin)
			{
				result.insert(screen.name);
				continue;
			}

			for (auto& g: screen.groups)
			{
				if (std::find(user.groups.begin(), user.groups.end(), g) == user.groups.end())
					continue;

				result.insert(screen.name);
				break;
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "Could not load screen: " << si.path().filename() << ": " << e.what() << std::endl;
		}
	}

	return result;	
}

screen_info screen_service::retrieve_screen(const std::string& name) const
{
	return ScreenData::loadManifest(m_screen_data_dir / name);
}

bool screen_service::exists(const std::string& name) const noexcept
{
	std::error_code ec;
	return fs::exists(m_screen_data_dir / name / "manifest.json", ec);
}

bool screen_service::is_valid_name(const std::string& name)
{
	const std::regex rx(R"([\n\r\t :<>|&])");
	return not std::regex_search(name.begin(), name.end(), rx);
}

bool screen_service::is_owner(const std::string& name, const std::string& username) const
{
	bool result = false;

	try
	{
		auto manifest = ScreenData::loadManifest(m_screen_data_dir / name);
		result = manifest.scientist == username;
	}
	catch (const std::exception& ex)
	{
		std::cerr << ex.what() << std::endl;
	}
	
	return result;
}

bool screen_service::is_allowed(const std::string& screenname, const std::string& username) const
{
	bool result = false;

	auto user = user_service::instance().retrieve_user(username);
	if (user.admin)
		result = true;
	else
	{
		try
		{
			auto manifest = ScreenData::loadManifest(m_screen_data_dir / screenname);

			if (manifest.scientist == username)
				result = true;
			else
			{
				for (auto& g: manifest.groups)
				{
					if (std::find(user.groups.begin(), user.groups.end(), g) == user.groups.end())
						continue;

					result = true;
					break;
				}
			}
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
		}
	}
	
	return result;
}

std::unique_ptr<ScreenData> screen_service::create_screen(const screen_info& screen)
{
	auto screenDir = m_screen_data_dir / screen.name;

	std::unique_ptr<ScreenData> data;

	switch (screen.type)
	{
		case ScreenType::IntracellularPhenotype:
		{
			data = std::make_unique<IPScreenData>(screenDir, screen);
			break;
		}

		case ScreenType::IntracellularPhenotypeActivation:
		{
			data = std::make_unique<PAScreenData>(screenDir, screen);
			break;
		}

		case ScreenType::SyntheticLethal:
		{
			data = std::make_unique<SLScreenData>(screenDir, screen);
			break;
		}

		case ScreenType::Unspecified:
			throw std::runtime_error("Unknown screen type");
			break;
	}

	return data;
}

void screen_service::update_screen(const std::string& name, const screen_info& screen)
{
	ScreenData::saveManifest(screen, m_screen_data_dir / name);
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
	{
		if ((*i)->is_up_to_date())
			result = *i;
		else
			m_ip_data_cache.erase(i);
	}
	
	if (not result)
	{
		result = std::make_shared<ip_screen_data_cache>(type, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd, direction);
		m_ip_data_cache.emplace_back(result);
	}
	
	return result;
}

std::shared_ptr<sl_screen_data_cache> screen_service::get_screen_data(const std::string &assembly, short trim_length,
	Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd)
{
	std::unique_lock lock(m_mutex);

	auto i = std::find_if(m_sl_data_cache.begin(), m_sl_data_cache.end(),
		std::bind(&sl_screen_data_cache::is_for, std::placeholders::_1, ScreenType::SyntheticLethal, assembly, trim_length, mode, cutOverlap, geneStart, geneEnd));

	std::shared_ptr<sl_screen_data_cache> result;

	if (i != m_sl_data_cache.end())
	{
		if ((*i)->is_up_to_date())
			result = *i;
		else
			m_sl_data_cache.erase(i);
	}
	
	if (not result)
	{
		result = std::make_shared<sl_screen_data_cache>(assembly, trim_length, mode, cutOverlap, geneStart, geneEnd);
		m_sl_data_cache.emplace_back(result);
	}
	
	return result;
}

void screen_service::screen_mapped(const std::unique_ptr<ScreenData>& screen)
{
	std::unique_lock lock(m_mutex);

	m_ip_data_cache.erase(
		std::remove_if(m_ip_data_cache.begin(), m_ip_data_cache.end(),
			[name=screen->name()]
			(std::shared_ptr<screen_data_cache> i)
			{
				return i->contains_data_for_screen(name);
			}),
			m_ip_data_cache.end());

	m_sl_data_cache.erase(
		std::remove_if(m_sl_data_cache.begin(), m_sl_data_cache.end(),
			[name=screen->name()]
			(std::shared_ptr<screen_data_cache> i)
			{
				return i->contains_data_for_screen(name);
			}),
			m_sl_data_cache.end());
}

// --------------------------------------------------------------------

class screen_analyzer_list_utility_object : public zeep::http::expression_utility_object<screen_analyzer_list_utility_object>
{
  public:

	static constexpr const char* name() { return "list"; }

	virtual zeep::http::object evaluate(const zeep::http::scope& scope, const std::string& methodName,
		const std::vector<zeep::http::object>& parameters) const
	{
		zeep::http::object result;

		if (methodName == "contains" and parameters.size() == 2)
		{
			auto list = parameters[0];
			auto q = parameters[1];

			for (const auto& i: list)
			{
				if (i != q)
					continue;
				
				result = true;
				break;
			}
		}

		return result;
	}
	
} s_screen_analyzer_list_utility_object;

// --------------------------------------------------------------------

screen_html_controller::screen_html_controller()
	: zeep::http::html_controller("/")
{
	mount("screens", &screen_html_controller::handle_screen_user);
	mount("create-screen", &screen_html_controller::handle_create_screen_user);
	mount("edit-screen", &screen_html_controller::handle_edit_screen_user);

	mount("screen-table", &screen_html_controller::handle_screen_table);
}

void screen_html_controller::handle_screen_user(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
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
	auto& ss = screen_service::instance();

	s.erase(std::remove_if(s.begin(), s.end(), [username,&ss](auto& si)
		{ return not ss.is_allowed(si.name, username); }), s.end());

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

	get_template_processor().create_reply_from_template("list-screens.html", sub, reply);
}

void screen_html_controller::handle_screen_table(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	zeep::http::scope sub(scope);

	// zeep::json::element users;
	// auto u = user_service::instance().get_all_users();
	// to_element(users, u);
	// sub.put("users", users);

	// zeep::json::element groups;
	// auto g = user_service::instance().get_all_groups();
	// to_element(groups, g);
	// sub.put("groups", groups);

	auto credentials = get_credentials();
	auto username = credentials["username"].as<std::string>();

	using json = zeep::json::element;
	json screens;

	auto s = screen_service::instance().get_all_screens();
	auto& ss = screen_service::instance();

	s.erase(std::remove_if(s.begin(), s.end(), [username,&ss](auto& si)
		{ return not ss.is_allowed(si.name, username); }), s.end());

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

	get_template_processor().create_reply_from_template("list-screens::screen-table", sub, reply);
}

void screen_html_controller::handle_create_screen_user(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
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

	get_template_processor().create_reply_from_template("create-screen", sub, reply);
}

void screen_html_controller::handle_edit_screen_user(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	zeep::http::scope sub(scope);

	const std::string screenID = request.get_parameter("screen-id");

	auto info = screen_service::instance().retrieve_screen(screenID);

	// make the mapped section complete
	auto& mapped = info.mappedInfo;

	for (auto a: { "hg19", "hg38" })
	{
		if (std::find_if(mapped.begin(), mapped.end(), [a](mapped_info& mi) { return mi.assembly == a; }) != mapped.end())
			continue;
		
		mapped.push_back({ a, 50 });
	}

	zeep::json::element screen;
	to_element(screen, info);
	sub.put("screen", screen);

	zeep::json::element groups;
	auto g = user_service::instance().get_all_groups();
	to_element(groups, g);
	sub.put("groups", groups);

	get_template_processor().create_reply_from_template("edit-screen", sub, reply);
}

// --------------------------------------------------------------------

screen_rest_controller::screen_rest_controller()
	: zeep::http::rest_controller("/")
{
	map_post_request("screen/validate/fastq", &screen_rest_controller::validateFastQFile, "file");
	map_post_request("screen/validate/name", &screen_rest_controller::validateScreenName, "name");

	map_post_request("screen", &screen_rest_controller::create_screen, "screen");
	map_get_request("screen/{id}", &screen_rest_controller::retrieve_screen, "id");
	map_put_request("screen/{id}", &screen_rest_controller::update_screen, "id", "screen");
	map_delete_request("screen/{id}", &screen_rest_controller::delete_screen, "id");

	map_get_request("screen/{id}/map/{assembly}", &screen_rest_controller::map_screen, "id", "assembly");
}

std::string screen_rest_controller::create_screen(const screen_info& screen)
{
	const auto& params = bowtie_parameters::instance();

	job_scheduler::instance().push(std::make_shared<map_job>(screen_service::instance().create_screen(screen), params.assembly()));

	return screen.name;
}

screen_info screen_rest_controller::retrieve_screen(const std::string& name)
{
	if (not screen_service::instance().is_allowed(name, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	return screen_service::instance().retrieve_screen(name);
}

void screen_rest_controller::update_screen(const std::string& name, const screen_info& screen)
{
	if (not screen_service::instance().is_allowed(name, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	auto info = screen_service::instance().retrieve_screen(name);

	// replace only the editable fields:
	info.treatment_details = screen.treatment_details;
	info.cell_line = screen.cell_line;
	info.description = screen.description;
	info.ignore = screen.ignore;
	info.groups = screen.groups;

	screen_service::instance().update_screen(name, info);
}

void screen_rest_controller::delete_screen(const std::string& name)
{
	if (not screen_service::instance().is_allowed(name, get_credentials()["username"].as<std::string>()))
		throw zeep::http::forbidden;

	screen_service::instance().delete_screen(name);
}

bool screen_rest_controller::validateFastQFile(const std::string& filename)
{
	checkIsFastQ(filename);
	return true;
}

bool screen_rest_controller::validateScreenName(const std::string& name)
{
	return screen_service::is_valid_name(name) and not screen_service::instance().exists(name);
}

void screen_rest_controller::map_screen(const std::string& screen, const std::string& assembly)
{
	job_scheduler::instance().push(std::make_shared<map_job>(screen_service::instance().load_screen<ScreenData>(screen), assembly));
}

