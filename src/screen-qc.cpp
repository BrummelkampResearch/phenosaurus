// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <numeric>
#include <filesystem>

#include <boost/algorithm/string.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filtering_stream.hpp>

#include "mrsrc.hpp"

#include "screen-qc.hpp"
#include "screen-service.hpp"


using json = zeep::json::element;
namespace fs = std::filesystem;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

extern int VERBOSE;

const size_t kBinSize = 20000;

// --------------------------------------------------------------------

struct ChromosomeInfo
{
	CHROM	chr;
	size_t	start, end;
};

struct RefSeqInfo : public std::vector<ChromosomeInfo>
{
	size_t binSize;		// size of a single bin
	size_t binCount;	// total number of bins

	std::unordered_map<CHROM,size_t>	chromToBinStart;

	size_t bin(CHROM chr, size_t index) const
	{
		return chromToBinStart.at(chr) + index / binSize;
	}

	RefSeqInfo(size_t binSize);
};

RefSeqInfo::RefSeqInfo(size_t binSize)
	: binSize(binSize)
{
	mrsrc::istream file("refSeqs.json");
	json data;
	parse_json(file, data);

	size_t binStart = 0;

	for (auto ci: data)
	{
		CHROM chr = from_string(ci["name"].as<std::string>());

		if (chr == INVALID)
		{
			if (VERBOSE > 1)
				std::cout << "skipping chrom: " << ci["name"].as<std::string>() << std::endl;
		
			continue;
		}

		emplace_back(ChromosomeInfo{ chr, ci["start"].as<size_t>(), ci["end"].as<size_t>() });

		chromToBinStart[chr] = binStart;
		binStart += back().end / binSize + 1;

		if (VERBOSE > 1)
			std::cout << "chrom: " << chr << " bin-start: " << chromToBinStart[chr] << " next bin-start: " << binStart << std::endl;
	}

	binCount = std::accumulate(begin(), end(), 0UL, [binSize](size_t sum, const ChromosomeInfo& ci) { return sum + ci.end / binSize + 1; });
	assert(binCount == binStart);
}

// --------------------------------------------------------------------

class InsertionCounts
{
  public:
	InsertionCounts(RefSeqInfo refseq)
		: refseq(refseq) {}

	void add(const std::string& screen, std::vector<uint16_t>&& counts);
	void calculateStats();

	float zscore(const std::string& screen, size_t bin) const;

	RefSeqInfo refseq;
	std::map<std::string,std::vector<uint16_t>> insertions;

	// std::tuple of average and stddev
	std::vector<std::tuple<float,float>> statistics;
};

void InsertionCounts::add(const std::string& screen, std::vector<uint16_t>&& counts)
{
	assert(counts.size() == refseq.binCount);

	insertions.emplace(make_pair(screen, move(counts)));
}

void InsertionCounts::calculateStats()
{
	const size_t N = refseq.binCount;

	std::vector<float> sums(N, 0);
	for (auto& ins: insertions)
	{
		auto sump = sums.begin();

		for (size_t count: ins.second)
			*sump++ += count;
	}

	std::vector<float> averages(N, 0);
	for (size_t i = 0; i < N; ++i)
		averages[i] = sums[i] / N;

	std::vector<float> sumsSq(N, 0.f);
	for (auto& ins: insertions)
	{
		for (size_t i = 0; i < N; ++i)
			sumsSq[i] += (ins.second[i] - averages[i]) * (ins.second[i] - averages[i]);
	}
	
	statistics.resize(refseq.binCount);
	for (size_t i = 0; i < refseq.binCount; ++i)
		statistics[i] = std::make_tuple(averages[i], sumsSq[i] == 0 ? 0 : sqrt(sumsSq[i] / (N - 1)));
}

float InsertionCounts::zscore(const std::string& screen, size_t bin) const
{
	auto& ins = insertions.at(screen);

	float average, stddev;
	std::tie(average, stddev) = statistics[bin];

	return stddev == 0 ? 0 : (ins[bin] - average) / stddev;
}

// --------------------------------------------------------------------

InsertionCounts createIndex(RefSeqInfo& refseq, std::vector<std::tuple<std::string,fs::path>>& files, size_t nrOfThreads)
{
	if (VERBOSE)
		std::cout << "About to read " << files.size() << " files" << std::endl;

	// std::unordered_map<std::string,ChromosomeInfo> ciMap;
	// for (auto ci: refseq)
	// 	ciMap[ci.name] = ci;

	InsertionCounts inscnt(refseq);

	size_t maxBin = refseq.binCount - 1;

	std::vector<std::thread> t;
	std::atomic<size_t> next(0);
	std::mutex m;

	for (size_t ti = 0; ti < nrOfThreads; ++ti)
	{
		t.emplace_back([&]() {
			for (;;)
			{
				size_t ix = next++;
				if (ix >= files.size())
					break;

				const auto& [name, file] = files[ix];

				try
				{
					std::vector<uint16_t> counts(refseq.binCount, 0);
					size_t count = 0;

					for (auto& ins: ScreenData::read_insertions(file))
					{
						size_t bin = refseq.bin(ins.chr, ins.pos + 1);
						if (bin >= maxBin)
							throw std::runtime_error("bin '" + std::to_string(bin) + "' out of range in file " + file.string());
						
						counts[bin] += 1;
						++count;
					}

					std::lock_guard lock(m);

					inscnt.add(name, move(counts));
					std::cout << '.';
					std::cout.flush();
				}
				catch(const std::exception& e)
				{
					std::cerr << "Error parsing file " << file << std::endl
						 << e.what() << std::endl;
				}
			}
		});
	}

	for (size_t ti = 0; ti < nrOfThreads; ++ti)
		t[ti].join();

	std::cout << std::endl
		 << "calculating statistics...";
	std::cout.flush();

	inscnt.calculateStats();

	std::cout << " done" << std::endl;

	return inscnt;
}

// --------------------------------------------------------------------

struct bin_remapper
{
	size_t m_start, m_end, m_width, m_count, m_bin_base_count;

	std::pair<size_t,size_t> operator()(size_t bin) const
	{
		assert(bin < m_count);

		size_t first = m_start + bin * m_width;
		size_t second = first + m_width;
		if (second > m_end)
			second = m_end;

		return std::make_pair(first, second);
	}
};

// --------------------------------------------------------------------

struct ChromComparator
{
	static const std::regex kRx;

	bool operator()(const std::string& a, const std::string& b) const
	{
		int d;

		std::smatch ma, mb;
		if (std::regex_match(a, ma, kRx) and std::regex_match(b, mb, kRx))
		{
			if ((ma.length(2) == 0) != (mb.length(2) == 0))
			{
				if (ma.length(2) == 0)
					d = -1;
				else
					d = 1;
			}
			else
			{
				std::string na = ma[1], nb = mb[1];
				if (isdigit(na[0]) and isdigit(nb[0]))
					d = stoul(na) - stoul(nb);
				else
					d = na.compare(nb);
				
				if (d == 0)
					d = ma[2].compare(mb[2]);
			}
		}
		else
			d = a.compare(b);
		
		return d < 0;
	}
};

const std::regex ChromComparator::kRx(R"(chr(\d+|[[:alpha:]]+)(?:_(.+))?)");

typedef std::map<std::string,bin_remapper,ChromComparator> ChromBinMap;

// --------------------------------------------------------------------

class screen_qc_data
{
  public:
	// size_t  binSize() const							{ return m_data["binSize"].as<size_t>(); }
	// size_t  binCount() const						{ return m_data["binCount"].as<size_t>(); }

	static screen_qc_data& instance();

	std::vector<std::string> screens() const;
	std::vector<std::string> chromosomes() const;

	ChromBinMap remapBins(size_t requestedBinCount, const std::string& chrom) const;
	std::map<std::string,std::vector<float>> heatmap(const ChromBinMap& rm, const std::set<std::string>& skip, float winsorize, bool emptiness) const;
	std::map<std::string,std::vector<float>> emptiness(const ChromBinMap& rm, const std::set<std::string>& skip, float winsorize) const;
	// std::map<std::string,std::vector<float>> emptybins(const ChromBinMap& rm, const std::set<std::string>& skip) const;

	static std::vector<std::string> cluster(const std::map<std::string,std::vector<float>>& data);

  private:

	screen_qc_data();

	struct screen_info
	{
		screen_info(const std::string& screen, std::vector<uint16_t>&& insertions)
			: screen(screen), insertions(std::move(insertions)) {}

		std::string				screen;
		std::vector<uint16_t>	insertions;
	};

	struct chrom_info
	{
		chrom_info(std::string&& name, size_t start, size_t end, size_t binCount)
			: chrom(std::move(name)), start(start), end(end), binCount(binCount) {}

		std::string		chrom;
		size_t			start, end;
		size_t			binCount;
	};




	size_t						m_binCount;
	size_t						m_binSize;
	std::vector<screen_info>	m_screenData;
	std::vector<chrom_info>		m_chromData;
};

// --------------------------------------------------------------------

screen_qc_data& screen_qc_data::instance()
{
	static std::mutex m;

	std::scoped_lock lock(m);

	static std::unique_ptr<screen_qc_data> s_instance;

	if (not s_instance)
		s_instance.reset(new screen_qc_data);

	return *s_instance;
}

screen_qc_data::screen_qc_data()
{
	RefSeqInfo refseq(kBinSize);

	std::vector<std::tuple<std::string,fs::path>> files;
	
	auto screensDir = screen_service::instance().get_screen_data_dir();

	for (auto& screen: screen_service::instance().get_all_screens())
	{
		auto screenDir = screensDir / screen.name / "hg38" / "50";

		for (auto file: screen.files)
		{
			auto f = screenDir / (file.name + ".sq");
			if (fs::exists(f))
				files.push_back(std::make_tuple(screen.name + '-' + file.name, f));
		}
	}

	auto stats = createIndex(refseq, files, std::thread::hardware_concurrency());

	m_binCount = refseq.binCount;
	m_binSize = refseq.binSize;

	for (auto& ins: stats.insertions)
		m_screenData.emplace_back(ins.first, std::move(ins.second));
	
	for (auto& chrom: refseq)
		m_chromData.emplace_back(to_string(chrom.chr), chrom.start, chrom.end, (chrom.end - chrom.start) / m_binSize + 1);
}

std::vector<std::string> screen_qc_data::screens() const
{
	std::vector<std::string> result;
	for (auto s: m_screenData)
		result.emplace_back(s.screen);
	return result;
}

std::vector<std::string> screen_qc_data::chromosomes() const
{
	std::vector<std::string> result;

	for (auto chrom: m_chromData)
		result.push_back(chrom.chrom);
	
	sort(result.begin(), result.end(), ChromComparator());

	return result;
}

ChromBinMap screen_qc_data::remapBins(size_t requestedBinCount, const std::string& chr) const
{
	ChromBinMap result;

	if (chr.empty())
	{
		float f = static_cast<float>(requestedBinCount) / m_binCount;
		size_t width = m_binCount / requestedBinCount + 1;
		
		size_t start = 0;

		for (auto chrom: m_chromData)
		{
			size_t binCount = static_cast<size_t>(ceil(f * chrom.binCount));
			if (binCount == 0)
				binCount = 1;
			
			size_t end = start + chrom.binCount;

			result.emplace(chrom.chrom, bin_remapper{ start, start + chrom.binCount, width, binCount,
				m_binSize * width });

			start = end;
		}

		assert(start == m_binCount);
	}
	else
	{
		size_t start = 0;

		for (auto chrom: m_chromData)
		{
			if (chrom.chrom != chr)
			{
				start += chrom.binCount;
				continue;
			}

			size_t width = chrom.binCount / requestedBinCount;
			if (width == 0)
				width = 1;
			size_t binCount = chrom.binCount / width;
			if (chrom.binCount % width != 0)
				++binCount;
			
			result.emplace(chrom.chrom, bin_remapper{ start, start + chrom.binCount, width, binCount,
				m_binSize * width });
			break;
		}
	}

	return result;
}

std::map<std::string,std::vector<float>> screen_qc_data::heatmap(const ChromBinMap& rm, const std::set<std::string>& skip, float winsorize, bool emptiness) const
{
	const size_t N = accumulate(rm.begin(), rm.end(), 0UL, [](size_t n, auto& r) { return n + r.second.m_count; });
	const size_t M = accumulate(rm.begin(), rm.end(), 0UL, [](size_t m, auto& r) { return std::max(m, r.second.m_end);});

	std::vector<size_t> m(M, N);
	size_t bin = 0;

	for (auto& mm: rm)
	{
		size_t e = bin + mm.second.m_count;

		for (size_t i = 0 ; bin < e; ++bin, ++i)
		{
			auto r = mm.second(i);

			for (size_t mi = r.first; mi < r.second; ++mi)
				m[mi] = bin; 
		}
	}
	assert(bin == N);

	std::regex rx(R"(-(high|low)$)");

	std::map<std::string,std::vector<uint32_t>> insertions;
	for (auto screen: m_screenData)
	{
		std::string name = screen.screen;

		if (skip.count(std::regex_replace(name, rx, "")))
			continue;

		auto& v = insertions[name];
		if (v.empty())
			v.resize(N);

		for (size_t b = 0; b < M; ++b)
		{
			if (m[b] >= N)
				continue;
			
			if (not emptiness)
				v[m[b]] += screen.insertions[b];
			else if (screen.insertions[b] > 0)
				v[m[b]] += 1;
		}
	}

	auto NS = insertions.size();

	std::vector<float> averages; averages.reserve(N);
	std::vector<float> stddevs; stddevs.reserve(N);

	// the term winsorize is given as a float
	// which is the fraction of the number of values to use.
	size_t P = 0;
	if (winsorize > 0)
		P = static_cast<size_t>(std::floor((1.0 - winsorize) * NS / 2));

	for (size_t bin = 0; bin < N; ++bin)
	{
		std::vector<size_t> c;
		for (auto& ins: insertions)
			c.push_back(ins.second[bin]);
		
		if (P > 0)
		{
			sort(c.begin(), c.end());

			auto low = c[P];
			auto high = c[NS - P - 1];

			for (auto& ci: c)
			{
				if (ci < low)
					ci = low;
				if (ci > high)
					ci = high;
			}
		}

		float avg = std::accumulate(c.begin(), c.end(), 0.f) / NS;

		float sumsq = 0;
		for (auto& ci: c)
			sumsq += (ci - avg) * (ci - avg);
		
		float stddev = sqrt(sumsq / (NS - 1));	

		averages.push_back(avg);
		stddevs.push_back(stddev);
	}

	std::map<std::string,std::vector<float>> data;
	for (auto& d: insertions)
	{
		auto& v = data[d.first] = std::vector<float>(N, 0);
		for (size_t i = 0; i < N; ++i)
		{
			if (not emptiness or d.second[i] > averages[i])
				v[i] = (d.second[i] - averages[i]) / stddevs[i];
		}
	}

	return data;
}

std::map<std::string,std::vector<float>> screen_qc_data::emptiness(const ChromBinMap& rm, const std::set<std::string>& skip, float winsorize) const
{
	const size_t N = accumulate(rm.begin(), rm.end(), 0UL, [](size_t n, auto& r) { return n + r.second.m_count; });
	const size_t M = accumulate(rm.begin(), rm.end(), 0UL, [](size_t m, auto& r) { return std::max(m, r.second.m_end);});

	std::vector<size_t> m(M, N);
	size_t bin = 0;

	for (auto& mm: rm)
	{
		size_t e = bin + mm.second.m_count;

		for (size_t i = 0 ; bin < e; ++bin, ++i)
		{
			auto r = mm.second(i);

			for (size_t mi = r.first; mi < r.second; ++mi)
				m[mi] = bin; 
		}
	}
	assert(bin == N);

	std::regex rx(R"(-(high|low|replicate-\d)$)");

	std::map<std::string,std::vector<uint32_t>> insertions;
	for (auto screen: m_screenData)
	{
		std::string name = screen.screen;

		if (skip.count(std::regex_replace(name, rx, "")))
			continue;

		auto& v = insertions[name];
		if (v.empty())
			v.resize(N);

		for (size_t b = 0; b < M; ++b)
		{
			if (m[b] >= N)
				continue;
			
			if (screen.insertions[b] == 0)
				v[m[b]] += 1;
		}
	}

	auto NS = insertions.size();

	std::vector<float> averages; averages.reserve(N);
	std::vector<float> stddevs; stddevs.reserve(N);

	// the term winsorize is given as a float
	// which is the fraction of the number of values to use.
	size_t P = 0;
    if (winsorize > 0)
        P = static_cast<size_t>(std::floor((1.0 - winsorize) * NS / 2));

    for (size_t bin = 0; bin < N; ++bin)
    {
        std::vector<size_t> c;
        for (auto& ins: insertions)
            c.push_back(ins.second[bin]);
        
        if (P > 0)
        {
            sort(c.begin(), c.end());

            auto low = c[P];
            auto high = c[NS - P - 1];

            for (auto& ci: c)
            {
                if (ci < low)
                    ci = low;
                if (ci > high)
                    ci = high;
            }
        }

        float avg = std::accumulate(c.begin(), c.end(), 0.f) / NS;

        float sumsq = 0;
        for (auto& ci: c)
            sumsq += (ci - avg) * (ci - avg);
        
        float stddev = sqrt(sumsq / (NS - 1));	

        averages.push_back(avg);
        stddevs.push_back(stddev);
    }

	std::map<std::string,std::vector<float>> data;
	for (auto& d: insertions)
	{
		auto& v = data[d.first] = std::vector<float>(N, 0);
		for (size_t i = 0; i < N; ++i)
		{
			if (d.second[i] > averages[i])
				v[i] = (d.second[i] - averages[i]) / stddevs[i];
		}
	}

	return data;
}

std::vector<std::string> screen_qc_data::cluster(const std::map<std::string,std::vector<float>>& data)
{
	size_t N = data.size();

	std::vector<std::string> screens(N);
	transform(data.begin(), data.end(), screens.begin(), [](auto& d) { return d.first; });

	std::vector<const std::vector<float>*> values(N);
	transform(data.begin(), data.end(), values.begin(), [](auto& d) { return &d.second; });

	// use a heap and complete-linkage clustering
	// --------------------------------------------------------------------
	// item is an element in the 'distance matrix'.
	// We start with a vector containing only items that
	// point to leafs (actual screens).
	// After each iteration we create a new item that 
	// points to two older items in the items array.
	// Therefore, if the value of a < N it points to
	// a screen, otherwise it is an index into the items
	// array to another item in the items array. 

	struct item
	{
		size_t a;			// index in screens if a < N, into items otherwise
		size_t b;			// index in screens if b < N, into items otherwise
		float dist;

		bool operator<(const item& rhs) const { return dist > rhs.dist; }
	};
	std::vector<item> items;

	auto compare_items = [&items](size_t x, size_t y)
	{
		assert(x < items.size());
		assert(y < items.size());

		return items[x].dist > items[y].dist;
	};

	std::vector<size_t> index;

	const std::regex rx(R"(\-(?:high|low)$)");

	for (size_t x = 0; x < N; ++x)
	{
		std::string nameX = std::regex_replace(screens[x], rx, "");

		auto& vx = *values[x];

		for (size_t y = x + 1; y < N; ++y)
		{
			std::string nameY = std::regex_replace(screens[y], rx, "");

			float distance = 0;

			if (nameX != nameY)
			{
				auto vy = *values[y];

				double sum = 0;
				for (size_t i = 0; i < vx.size(); ++i)
				{
					if (not (isnan(vx[i]) or isnan(vy[i])))
						sum += (vx[i] - vy[i]) * (vx[i] - vy[i]);
				}

				distance = static_cast<float>(sqrt(sum));
			}

			index.push_back(items.size());
			items.push_back({ x, y, distance });

			push_heap(index.begin(), index.end(), compare_items);
		}
	}

	while (index.size() > 1)
	{
		pop_heap(index.begin(), index.end(), compare_items);
		item p = items[index.back()];
		index.pop_back();

		std::vector<size_t> newIndex;
		std::map<size_t,float> newDist;

		size_t gi = items.size();
		items.push_back({ p.a, p.b, 0 });

		for (auto q: index)
		{
			auto& qi = items[q];

			if (p.a != qi.a and p.b != qi.b and p.a != qi.b and p.b != qi.a)
			{
				newIndex.push_back(q);
				push_heap(newIndex.begin(), newIndex.end(), compare_items);
				continue;
			}

			if (p.a == qi.a or p.b == qi.a)
			{
				if (newDist[qi.b] < qi.dist)
					newDist[qi.b] = qi.dist;
			}
			else
			{
				if (newDist[qi.a] < qi.dist)
					newDist[qi.a] = qi.dist;
			}
		}

		for (auto d: newDist)
		{
			newIndex.push_back(items.size());
			items.push_back({ d.first, gi, d.second });
			push_heap(newIndex.begin(), newIndex.end(), compare_items);
		}

		swap(index, newIndex);
	}

	std::vector<std::string> result;
	std::stack<size_t> s;
	s.push(index.front());

	while (not s.empty())
	{
		auto ix = s.top();
		s.pop();

		assert(ix < items.size());
		auto i = items[ix];

		if (i.a < N)
			result.push_back(screens[i.a]);
		else
			s.push(i.a);
		
		if (i.b < N)
			result.push_back(screens[i.b]);
		else
			s.push(i.b);
	}

	return result;
}


// --------------------------------------------------------------------


screen_qc_rest_controller::screen_qc_rest_controller()
	: zeep::http::rest_controller("/qc")
{
	map_post_request("heatmap", &screen_qc_rest_controller::get_heatmap, "requestedBinCount", "chr", "skip");
	map_post_request("emptybins", &screen_qc_rest_controller::get_emptybins, "requestedBinCount", "chr", "skip");
}

template<typename Algo>
ScreenQCData screen_qc_rest_controller::get_data(size_t requestedBinCount, std::string chrom, std::string skip, Algo&& algo)
{
	const auto& qc_data = screen_qc_data::instance();

	if (chrom == "null")
		chrom.clear();

	if (requestedBinCount == 0)
		throw std::runtime_error("Invalid bin count requested");

	std::set<std::string> skippedScreens;
	ba::split(skippedScreens, skip, ba::is_any_of(";"));

	// std::map<std::string,size_t> chromBinStarts;
	std::vector<ChromStart> chromBinStarts;
	auto remapped = qc_data.remapBins(requestedBinCount, chrom);

	size_t start = 0;
	for (auto rmbc: remapped)
	{
		std::string chrom;
		bin_remapper rm;
		tie(chrom, rm) = rmbc;

		chromBinStarts.push_back({chrom, start, rm.m_bin_base_count});
		start += rm.m_count;
	}

	std::vector<std::string> screens;
	// std::regex r(R"(-(high|low)$)");
	// for (auto screen: m_data.screens())
	// {
	// 	if (skippedScreens.count(std::regex_replace(screen, r, "")))
	// 		continue;
	// 	screens.push_back(screen);
	// }

	auto data = algo(qc_data, remapped, skippedScreens);
	screens = screen_qc_data::cluster(data);

	return {
		start,
		std::move(screens),
		std::move(chromBinStarts),
		std::move(data)
	};
}

ScreenQCData screen_qc_rest_controller::get_heatmap(size_t requestedBinCount, std::string chrom, std::string skip)
{
	using namespace std::placeholders;
	return get_data(requestedBinCount, chrom, skip, bind(&screen_qc_data::heatmap, _1, _2, _3, m_winsorize, false));
}

ScreenQCData screen_qc_rest_controller::get_emptybins(size_t requestedBinCount, std::string chrom, std::string skip)
{
	using namespace std::placeholders;
	return get_data(requestedBinCount, chrom, skip, bind(&screen_qc_data::emptiness, _1, _2, _3, m_winsorize));
}

// --------------------------------------------------------------------

screen_qc_html_controller::screen_qc_html_controller()
{
	mount("qc", &screen_qc_html_controller::index);
}

// --------------------------------------------------------------------

void screen_qc_html_controller::index(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	const auto& data = screen_qc_data::instance();

	zeep::http::scope sub(scope);

	sub.put("page", "index");
	sub.put("chromosomes", data.chromosomes());
	
	std::set<std::string> screens;
	std::regex r(R"(-(?:high|low|replicate-\d)$)");

	for (auto screen: data.screens())
		screens.insert(std::regex_replace(screen, r, ""));
	
	sub.put("screens", std::vector<std::string>(screens.begin(), screens.end()));

	get_template_processor().create_reply_from_template("qc.html", sub, reply);
}

