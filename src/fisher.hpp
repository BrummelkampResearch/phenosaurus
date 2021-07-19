// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <vector>

enum class FisherAlternative {
	Left, Right, TwoSided
};

class FishersExactTest
{
  public:
	FishersExactTest(long v[2][2], FisherAlternative alternative = FisherAlternative::TwoSided);

	double pvalue() const			{ return m_pvalue; }
	double oddsRatio() const		{ return m_oddsRatio; }

  private:
	double m_pvalue;
	double m_oddsRatio;
};

double fisherTest2x2(long v[2][2], FisherAlternative alternative = FisherAlternative::TwoSided);

std::vector<double> adjustFDR_BH(const std::vector<double>& p);
