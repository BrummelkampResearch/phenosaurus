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
