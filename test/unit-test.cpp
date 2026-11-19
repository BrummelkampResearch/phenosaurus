// Copyright NKI/AVL 2026 
//
// SPDX-License-Identifier: BSD-2-Clause

#include "fisher.hpp"

#include <catch2/catch_all.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace
{
void check_fdr(const std::vector<double> &p, const std::vector<double> &expected)
{
	auto actual = adjustFDR_BH(p);

	REQUIRE(actual.size() == expected.size());

	for (size_t i = 0; i < expected.size(); ++i)
		CHECK(actual[i] == Catch::Approx(expected[i]).margin(1e-12));
}
}

TEST_CASE("first")
{
	int i = 42;
	CHECK(i == 42);
}

TEST_CASE("adjustFDR_BH applies the Benjamini-Hochberg step-up correction")
{
	SECTION("monotone step-up")
	{
		check_fdr({ 0.01, 0.02, 0.03 }, { 0.03, 0.03, 0.03 });
	}

	SECTION("unsorted input preserves original order")
	{
		check_fdr({ 0.01, 0.04, 0.03 }, { 0.03, 0.04, 0.04 });
	}

	SECTION("known reference values")
	{
		check_fdr({ 0.01, 0.04, 0.03, 0.002 }, { 0.02, 0.04, 0.04, 0.008 });
	}

	SECTION("equal p-values")
	{
		check_fdr({ 0.5, 0.5 }, { 0.5, 0.5 });
	}

	SECTION("invalid p-values are excluded and returned as 1.0")
	{
		check_fdr({ 0.01, std::numeric_limits<double>::quiet_NaN(), -1, 2.0 },
			{ 0.01, 1.0, 1.0, 1.0 });
	}

	SECTION("empty input")
	{
		check_fdr({}, {});
	}
}