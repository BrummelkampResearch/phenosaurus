//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/rest-controller.hpp>

// --------------------------------------------------------------------

class genome_browser_html_controller : public zeep::http::html_controller
{
  public:
	genome_browser_html_controller();

	void genome_browser(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
	void jbrowse_conf(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
	void jbrowse_conf_json(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
};

// --------------------------------------------------------------------

struct track
{
	std::string category;
	std::string key;
	std::string label;
	std::string storeClass;
	std::string type;
	std::optional<std::string> seqType;
	std::optional<int> chunkSize;
	std::optional<std::string> urlTemplate;
	std::optional<std::string> baseUrl;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("category", category)
		   & zeep::name_value_pair("key", key)
		   & zeep::name_value_pair("label", label)
		   & zeep::name_value_pair("storeClass", storeClass)
		   & zeep::name_value_pair("type", type)
		   & zeep::name_value_pair("seqType", seqType)
		   & zeep::name_value_pair("chunkSize", chunkSize)
		   & zeep::name_value_pair("urlTemplate", urlTemplate)
		   & zeep::name_value_pair("baseUrl", baseUrl);
	}
};

struct name
{
	std::string type = "REST";
	std::string url;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("type", type)
		   & zeep::name_value_pair("url", url);
	}
};

struct track_list
{
	int formatVersion;
	name names;
	std::vector<track> tracks;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("formatVersion", formatVersion)
		   & zeep::name_value_pair("names", names)
		   & zeep::name_value_pair("tracks", tracks);
	}
};

struct location
{
	std::string ref;
	long start;
	long end;
	std::vector<std::string> tracks;
	std::string objectName;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("ref", ref)
		   & zeep::name_value_pair("start", start)
		   & zeep::name_value_pair("end", end)
		   & zeep::name_value_pair("tracks", tracks)
		   & zeep::name_value_pair("objectName", objectName);
	}
};

struct named_location
{
	std::string name;
	location location;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("name", name)
		   & zeep::name_value_pair("location", location);
	}
};

class genome_browser_rest_controller : public zeep::http::rest_controller
{
  public:
	genome_browser_rest_controller();

	track_list trackList();
	std::string tracks();
	std::vector<named_location> names(const std::optional<std::string>& equals, const std::optional<std::string>& startsWith);
};

