// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <list>
#include <filesystem>

#include "bowtie.hpp"

class ReferenceGenes;

class AnalyzedScreenData
{
  public:
	AnalyzedScreenData();
	AnalyzedScreenData(const AnalyzedScreenData&) = delete;
	AnalyzedScreenData& operator=(const AnalyzedScreenData&) = delete;



  private:


};

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

	void analyze(const std::string& assembly, unsigned readLength,
		const std::vector<Transcript>& transcripts,
		std::vector<Insertions>& lowInsertions, std::vector<Insertions>& highInsertions);

  private:

	ScreenData(std::filesystem::path dir, std::filesystem::path low, std::filesystem::path high);

	std::filesystem::path	mDataDir;
};

// --------------------------------------------------------------------

class MappedScreenData
{
  public:
	MappedScreenData(ScreenData& screenData, const std::string& assembly, unsigned readLength);

	MappedScreenData(const MappedScreenData&) = delete;
	MappedScreenData& operator=(const MappedScreenData&) = delete;

	const std::string& assembly() const		{ return mAssembly; }
	unsigned readLength() const				{ return mReadLength; }

  private:

	std::string mAssembly;
	unsigned mReadLength;
};

