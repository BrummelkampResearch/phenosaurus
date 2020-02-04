// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <list>
#include <filesystem>

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

class MappedScreenData
{
  public:
	MappedScreenData(std::filesystem::path dir, const std::string& assembly, unsigned readLength);

	MappedScreenData(const MappedScreenData&) = delete;
	MappedScreenData& operator=(const MappedScreenData&) = delete;

	const std::string& assembly() const		{ return mAssembly; }
	unsigned readLength() const				{ return mReadLength; }

	void analyze();

	AnalyzedScreenData& getAnalyzedScreenData(const ReferenceGenes& refSeq);


  private:

	std::string mAssembly;
	unsigned mReadLength;
	std::list<AnalyzedScreenData> mAnalyzed;
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

	void map(const std::string& assembly,
		std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
		unsigned threads, unsigned readLength);

	MappedScreenData& getMapped(const std::string& assembly);

  private:

	ScreenData(std::filesystem::path dir, std::filesystem::path low, std::filesystem::path high);

	std::filesystem::path	mDataDir;
};