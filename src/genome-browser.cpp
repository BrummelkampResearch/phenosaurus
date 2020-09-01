//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

#include "genome-browser.hpp"

// --------------------------------------------------------------------

genome_browser_html_controller::genome_browser_html_controller()
{
	mount("jbrowse/{dist,plugins,img}/", &genome_browser_html_controller::handle_file);
	mount("genome-browser", &genome_browser_html_controller::genome_browser);
	mount("jbrowse/jbrowse.conf", &genome_browser_html_controller::jbrowse_conf);
	mount("jbrowse/jbrowse_conf.json", &genome_browser_html_controller::jbrowse_conf_json);
	mount("jbrowse/data/seq/", &genome_browser_html_controller::handle_file);
}

void genome_browser_html_controller::genome_browser(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	return get_template_processor().create_reply_from_template("genome-browser", scope, reply);
}

void genome_browser_html_controller::jbrowse_conf(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	const char* config = R"([GENERAL]
include  = {dataRoot}/trackList.json
include += {dataRoot}/tracks.conf
)";
	reply.set_content(config, "text/plain");
}

void genome_browser_html_controller::jbrowse_conf_json(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	reply.set_content(zeep::json::element::object());
}


// --------------------------------------------------------------------

genome_browser_rest_controller::genome_browser_rest_controller()
	: zeep::http::rest_controller("/jbrowse")
{
	map_get_request("data/trackList.json", &genome_browser_rest_controller::trackList);
	map_get_request("data/tracks.conf", &genome_browser_rest_controller::tracks);
	map_get_request("data/names", &genome_browser_rest_controller::names, "equals", "startsWith");
}

track_list genome_browser_rest_controller::trackList()
{
	track_list result;

	result.names = { "REST", "names"};

	track refseq{
		"Reference sequence", "Reference sequence", "DNA", "JBrowse/Store/Sequence/StaticChunked", "SequenceTrack", "dna", 20000, "seq/{refseq_dirpath}/{refseq}-", {}
	};

	result.tracks.push_back(refseq);

	return result;
}

std::string genome_browser_rest_controller::tracks()
{
	return "";
}

std::vector<named_location> genome_browser_rest_controller::names(const std::optional<std::string>& equals, const std::optional<std::string>& startsWith)
{
	return {};
}
