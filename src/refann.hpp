// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <string>
#include <vector>

// --------------------------------------------------------------------

enum class Mode
{
	Collapse, Longest, Start, End
};

// -----------------------------------------------------------------------

struct Range
{
	uint32_t start, end;
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

	// The final range as calculated
	Range r;
};

// --------------------------------------------------------------------

std::vector<Transcript> loadTranscripts(const std::string& refGenesFile, Mode mode,
	const std::string& startPos, const std::string& endPos, bool cutOverlap);
