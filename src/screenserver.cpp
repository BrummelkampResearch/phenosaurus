// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <iostream>

#include <zeep/http/webapp.hpp>
#include <zeep/rest/controller.hpp>

#include "screenserver.hpp"
#include "screendata.hpp"
#include "fisher.hpp"

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

struct ScreenInfo
{
	std::string					name;
	std::vector<std::string>	assemblies;
	std::vector<unsigned>		readLengths;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("name", name)
		   & zeep::make_nvp("assembly", assemblies)
		   & zeep::make_nvp("readlength", readLengths);
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
			"id", "assembly", "read-length", "reference", "mode", "cut-overlap", "gene-start", "gene-end");
	}

	std::vector<DataPoint> screenData(const std::string& screen);
	std::vector<DataPoint> screenDataEx(const std::string& screen, const std::string& assembly, unsigned readLength, std::string reference,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);

	fs::path mScreenDir;
};

std::vector<DataPoint> ScreenRestController::screenDataEx(const std::string& screen, const std::string& assembly, unsigned readLength,
	std::string reference, Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd)
{
	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<ScreenData> data(new ScreenData(screenDir));
	
	// -----------------------------------------------------------------------

	if (reference.empty())
		reference = "ncbi";

	const std::string reference_file = reference + "-genes-" + assembly + ".txt";

	auto transcripts = loadTranscripts(reference_file, mode, geneStart, geneEnd, cutOverlap);

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

	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		long low = lowInsertions[i].sense.size();
		long high = highInsertions[i].sense.size();
	
		long v[2][2] = {
			{ low, high },
			{ lowSenseCount - low, highSenseCount - high }
		};

		pvalues[i] = fisherTest2x2(v);
	}

	auto fcpv = adjustFDR_BH(pvalues);

	std::vector<DataPoint> result;

	for (size_t i = 0; i < transcripts.size(); ++i)
	{
		auto& t = transcripts[i];

		auto low = lowInsertions[i].sense.size();
		auto high = highInsertions[i].sense.size();

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
	return screenDataEx(screen, "hg19", 0, "ncbi-genes-hg19.txt", Mode::Collapse, true, "tx", "cds");
}

// --------------------------------------------------------------------

class ScreenServer : public zh::webapp
{
  public:
	ScreenServer(const fs::path& screenDir)
		: zh::webapp(fs::current_path() / "docroot")
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

zh::server* createServer(const fs::path& screenDir)
{
	return new ScreenServer(screenDir);
}
