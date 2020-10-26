// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <zeep/http/rest-controller.hpp>
#include <zeep/http/html-controller.hpp>

// --------------------------------------------------------------------

struct ChromStart
{
	std::string	chrom;
	size_t		start;
	size_t		binBaseCount;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("chrom", chrom)
		   & zeep::make_nvp("start", start)
		   & zeep::make_nvp("binBaseCount", binBaseCount);
	}
};

// --------------------------------------------------------------------

struct ScreenQCData
{
	size_t										binCount;
	std::vector<std::string>					screens;
	std::vector<ChromStart>						chromosomeStarts;
	std::map<std::string,std::vector<float>>	data;
	std::vector<std::string>					clustered;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("binCount", binCount)
		   & zeep::make_nvp("screens", screens)
		   & zeep::make_nvp("chromosomeStarts", chromosomeStarts)
		   & zeep::make_nvp("data", data);
	}
};

// --------------------------------------------------------------------

class screen_qc_rest_controller : public zeep::http::rest_controller
{
  public:
	screen_qc_rest_controller();

	template<typename Algo>
	ScreenQCData get_data(size_t requestedBinCount, std::string chrom, std::string skip, Algo&& algo);

	ScreenQCData get_heatmap(size_t requestedBinCount, std::string chrom, std::string skip);
	ScreenQCData get_emptybins(size_t requestedBinCount, std::string chrom, std::string skip);

	const float m_winsorize = 0.9f;
};

// --------------------------------------------------------------------

class screen_qc_html_controller : public zeep::http::html_controller
{
  public:
	screen_qc_html_controller();

	void index(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
};
