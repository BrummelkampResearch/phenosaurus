// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <list>
#include <filesystem>

#include "bowtie.hpp"

// --------------------------------------------------------------------

class ScreenData
{
  public:
	ScreenData(std::filesystem::path dir);

	ScreenData(const ScreenData&) = delete;
	ScreenData& operator=(const ScreenData&) = delete;

	static ScreenData* create(std::filesystem::path dir,
		std::filesystem::path lowFastQ, std::filesystem::path highFastQ);

	void map(const std::string& assembly, unsigned readLength,
		std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
		unsigned threads);

	// note: will reorder transcripts!
	void analyze(const std::string& assembly, unsigned readLength,
		std::vector<Transcript>& transcripts,
		std::vector<Insertions>& lowInsertions, std::vector<Insertions>& highInsertions);

  private:

	ScreenData(std::filesystem::path dir, std::filesystem::path low, std::filesystem::path high);

	std::filesystem::path	mDataDir;
};
