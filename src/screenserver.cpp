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

class ScreenRestController : public zh::rest_controller
{
  public:
	ScreenRestController(const fs::path& screenDir)
		: zh::rest_controller("ajax")
		, mScreenDir(screenDir)
	{
		map_get_request("screenData/{id}", &ScreenRestController::screenData, "id");
	}

	std::vector<DataPoint> screenData(const std::string& screen);

	fs::path mScreenDir;
};

std::vector<DataPoint> ScreenRestController::screenData(const std::string& screen)
{
	fs::path screenDir = mScreenDir / screen;

	if (not fs::is_directory(screenDir))
		throw std::runtime_error("No such screen: " + screen);

	std::unique_ptr<ScreenData> data(new ScreenData(screenDir));

	std::string assembly = "hg19";	//vm["assembly"].as<std::string>();
	std::string reference = "ncbi-genes-hg19.txt"; //vm["reference"].as<std::string>();

	unsigned readLength = 0;
	// if (vm.count("read-length"))
	// 	readLength = vm["read-length"].as<unsigned>();
	
	// -----------------------------------------------------------------------

	bool cutOverlap = true;
	// if (vm.count("overlapped") and vm["overlapped"].as<std::string>() == "both")
	// 	cutOverlap = false;

	Mode mode;
	// if (vm["mode"].as<std::string>() == "collapse")
		mode = Mode::Collapse;
	// else // if (vm["mode"].as<std::string>() == "longest")
	// 	mode = Mode::Longest;

	std::string start = "tx";
	std::string end = "cds";

	auto transcripts = loadTranscripts(reference, mode, start, end, cutOverlap);

	// -----------------------------------------------------------------------
	
	std::vector<Insertions> lowInsertions, highInsertions;

	data->analyze(assembly, readLength, transcripts, lowInsertions, highInsertions);

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
	json screens;

	for (auto i = fs::directory_iterator(mScreenDir); i != fs::directory_iterator(); ++i)
	{
		if (not i->is_directory())
			continue;
		
		json screen{
			{ "id", i->path().filename().string() },
			{ "name", i->path().filename().string() }
		};

		screens.push_back(screen);
	}

	sub.put("screens", screens);

	create_reply_from_template("fishtail.html", sub, reply);
}

// --------------------------------------------------------------------

zh::server* createServer(const fs::path& screenDir)
{
	return new ScreenServer(screenDir);
}
