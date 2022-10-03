/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
