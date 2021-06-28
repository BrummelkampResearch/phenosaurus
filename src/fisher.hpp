// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <vector>

enum class FisherAlternative {
	Left, Right, TwoSided
};

double fisherTest2x2(long v[2][2], FisherAlternative alternative = FisherAlternative::TwoSided);
std::vector<double> adjustFDR_BH(const std::vector<double>& p);
