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

#include <algorithm>
#include <string>
#include <vector>
#include <numeric>

// --------------------------------------------------------------------

enum class Mode
{
	Collapse, LongestTranscript, LongestExon
};

// -----------------------------------------------------------------------

struct Range
{
	uint32_t start, end;

	bool empty() const { return end <= start; }
};

enum class CDSStat : uint8_t
{
	NONE, UNKNOWN, INCOMPLETE, COMPLETE
};

struct CDS : public Range
{
	CDSStat stat;
};

struct Exon : public Range
{
	int8_t frame;
};

enum CHROM : int8_t
{
	INVALID, CHR_1, CHR_2, CHR_3, CHR_4, CHR_5, CHR_6, CHR_7, CHR_8, CHR_9, CHR_10,
	CHR_11, CHR_12, CHR_13, CHR_14, CHR_15, CHR_16, CHR_17, CHR_18, CHR_19, CHR_20,
	CHR_21, CHR_22, CHR_23, CHR_X, CHR_Y
};

std::ostream& operator<<(std::ostream& os, CHROM chr);

std::string to_string(CHROM chr);
CHROM from_string(const std::string& chr);

struct Transcript
{
	std::string	name;
	CHROM chrom;
	char strand;
	CDS cds;
	Range tx;
	std::vector<Exon> exons;
	float score;
	std::string geneName;

	// used by algorithms
	bool longest = false;
	bool overlapped = false;
	bool unique = true;

	// The final ranges as calculated
	// Range r;
	std::vector<Range> ranges;

	bool empty() const
	{
		return ranges.empty() or
			std::find_if(ranges.begin(), ranges.end(), [](auto& r) { return not r.empty(); }) == ranges.end();
	}

	bool hasOverlap(const Transcript& b) const;

	uint32_t start() const
	{
		return ranges.empty() ? tx.start : ranges.front().start;
	}

	uint32_t length_exons() const;

	void start(uint32_t v)
	{
		if (ranges.empty())
			ranges.push_back({v, 0});
		else
			ranges.front().start = v;
	}

	uint32_t end() const
	{
		return ranges.empty() ? tx.end : ranges.back().end;
	}

	void end(uint32_t v)
	{
		if (ranges.empty())
			ranges.push_back({0, v});
		else
			ranges.back().end = v;
	}

	bool operator<(const Transcript& t) const
	{
		int d = chrom - t.chrom;
		if (d == 0)
			d = start() - t.start();
		return d < 0;
	}

	bool operator==(const Transcript& t) const
	{
		// sufficient for now, I hope
		return chrom == t.chrom and start() == t.start() and end() == t.end();
	}
};

void cutOverlap(Transcript& a, Transcript& b);

// --------------------------------------------------------------------

void init_refseq(const std::string& file);

std::vector<Transcript> loadGenes(const std::string& assembly,
	const std::string &transcript_selection, bool completeOnly, bool knownOnly);

void selectTranscripts(std::vector<Transcript>& transcripts, uint32_t maxGap, Mode mode);

std::vector<Transcript> loadTranscripts(const std::string& assembly,
	const std::string &transcript_selection, Mode mode,
	const std::string& startPos, const std::string& endPos, bool cutOverlap);

void filterTranscripts(std::vector<Transcript>& transcripts, Mode mode,
	const std::string& startPos, const std::string& endPos, bool cutOverlap);

// second form of loadTranscripts loads the transcripts in a window around a specified gene
std::vector<Transcript> loadTranscripts(const std::string& assembly,
	const std::string &transcript_selection, const std::string& gene,
	int window = 500);

// loadTranscripts variant that reads the transcripts from a BED file
std::vector<Transcript> loadTranscripts(const std::string& bedFile);

// --------------------------------------------------------------------
// Yet another selection: filter out exons

void filterOutExons(std::vector<Transcript>& transcripts);

void cutOverlappingRegions(std::vector<Transcript>& transcripts);