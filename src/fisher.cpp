#include "config.hpp"

#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <cassert>

#include "fisher.hpp"

const double kLn2PI = log(2 * M_PI);

/*
	Bereken  x * log(x / np) + np - x;  

	Probleem is dat als x / np dicht bij 1 ligt de uitkomst
	niet stabiel is. Gebruik daarom een oplossing
	gebaseerd op de Taylor reeks voor log((1 + y) / (1 - y))
	met y = (x - np) / (x + np)

	log((1 + y) / (1 - y))  ==

	log(1 + y) - log(1 - y) ==

	(y - y^2/2 + y^3/3 - y^4/4 + y^5/5 - O(y^6)) - (-y - y^2/2 - y^3/3 - y^4/4 - y^5/5 - O(y^6)) ==

	y - y^2/2 + y^3/3 - y^4/4 + y^5/5 - O(y^6) + y + y^2/2 + y^3/3 + y^4/4 + y^5/5 + O(y^6)) ==

	2y + 2y^3 / 3 + 2y^5/5

	En dan... het blijkt dat de taylor benadering vele malen
	sneller is dan de code met een log. Dus maak dit maar de default.

	Dit was de oorspronkelijke code:

	if (not std::isfinite(x) or not std::isfinite(np) or np == 0)
		result = nan("1");
	else if (std::abs(x - np) < 0.1 * (x + np))
	{
		for (int n = 1; n < 1000; ++n)
		{
			y *= y2;
			double t = 2 * y / (2 * n + 1);
			double s1 = s + t;
			if (s1 == s)
				break;
			s = s1;
		}

		result = x * s + np - x;
	}
	else
		result = x * std::log(x / np) + np - x;
	
	return result;
*/

double bd0(double x, double np)
{
	double y = (x - np) / (x + np);
	double y2 = y * y;
	double s = 2 * y;

	for (int n = 1; n < 1000; ++n)
	{
		y *= y2;
		double t = 2 * y / (2 * n + 1);
		double s1 = s + t;
		if (s1 == s)
			break;
		s = s1;
	}

	return x * s + np - x;
}

double calculate_sterling_error(long n)
{
	return lgamma(n + 1.) - (n + 0.5) * log(n) + n - log(sqrt(2 * M_PI));
}

const double kStirlingErrors[16] = {
	0.0,
	calculate_sterling_error(1),
	calculate_sterling_error(2),
	calculate_sterling_error(3),
	calculate_sterling_error(4),
	calculate_sterling_error(5),
	calculate_sterling_error(6),
	calculate_sterling_error(7),
	calculate_sterling_error(8),
	calculate_sterling_error(9),
	calculate_sterling_error(10),
	calculate_sterling_error(11),
	calculate_sterling_error(12),
	calculate_sterling_error(13),
	calculate_sterling_error(14),
	calculate_sterling_error(15)
};

double stirling_error(long n)
{
	double result;

	if (n <= 15)
		result = kStirlingErrors[n];
	else
	{
		auto n2 = n * n;

		result = 0;
		if (n <= 35)	result = (1 / 1188.0         ) / n2;
		if (n <= 80)	result = (1 / 1680.0 - result) / n2;
		if (n <= 500)	result = (1 / 1260.0 - result) / n2;
						result = (1 /  360.0 - result) / n2;
						result = (1 /   12.0 - result) / n2;

		result *= n;
	}

	return result;
}

double binomial_coefficient(long x, long n, double p)
{
	double result = 0;

    if (x >= 0 and std::isfinite(x))
	{
		auto q = 1 - p;

		if (p == 0)
			result = x == 0 ? 1 : 0;
		else if (q == 0)
			result = x == n ? 1 : 0;
		else if (x == 0 and n == 0)
			result = 1;
		else if (x == 0)
			result = (p < 0.1) ? -bd0(n, n * q) - n * p : n * log(q);
		else if (x == n)
			result = (q < 0.1) ? -bd0(n, n * p) - n * q : n * log(p);
		else if (x < 0 or x > n)
			result = 0;
		else
		{
			auto lc = stirling_error(n) - stirling_error(x) - stirling_error(n - x) - bd0(x, n * p) - bd0(n - x, n * q);

			// auto lf = kLn2PI + log(x) + log(n-x) - log(n);
			auto lf = kLn2PI + log(x) + log1p(-x / n);

			result = lc - 0.5 * lf;
		}	
	}

	return result;
}

double hypergeometric_probability(long x, long r, long b, long n)
{
	double result;
	if (n < x or r < x or n - x > b)
		result = 0;
	else if (n == 0)
		result = x == 0 ? 1 : 0;
	else
	{
		double p = static_cast<double>(n) / (r + b);

		double p1 = binomial_coefficient(x, r, p);
		double p2 = binomial_coefficient(n - x, b, p);
		double p3 = binomial_coefficient(n, r + b, p);

		result = p1 + p2 - p3;
	}

	return result;
}

double fisherTest2x2(long v[2][2])
{
	auto m = v[0][0] + v[0][1];
	auto n = v[1][0] + v[1][1];
	auto k = v[0][0] + v[1][0];
	auto x = v[0][0];
	auto lo = k - n;
	if (lo < 0)
		lo = 0;
	auto hi = k;
	if (hi > m)
		hi = m;

	std::vector<double> d(hi - lo + 1);
	for (auto i = lo; i <= hi; ++i)
		d[i - lo] = hypergeometric_probability(i, m, n, k);
	
	auto dmax = *std::max_element(d.begin(), d.end());

	for (auto& di: d)
		di = std::exp(di - dmax);

	auto dsum = std::accumulate(d.begin(), d.end(), 0.0);

	for (auto& di: d)
		di /= dsum;

	const double kRelErr = 1 + 1e-7;

	return std::accumulate(d.begin(), d.end(), 0.0,
		[max=d[x - lo] * kRelErr](double s, double d) { return d <= max ? s + d : s; });
}

std::vector<double> adjustFDR_BH(const std::vector<double>& p)
{
	const size_t N = p.size();

	std::vector<size_t> ix(N);
	std::iota(ix.begin(), ix.end(), 0);

	ix.erase(std::remove_if(ix.begin(), ix.end(), [&p](size_t ix) { return p[ix] == -1; }), ix.end());

	const size_t M = ix.size();

	std::sort(ix.begin(), ix.end(), [&p](size_t i, size_t j) { return p[i] < p[j]; });

	std::vector<double> result(N, 0);

	for (size_t i = 0; i < M; ++i)
	{
		auto v = (M * p[ix[i]]) / (i + 1);
		if (v > 1)
			v = 1;
		result[ix[i]] = v;
	}
	
	return result;
}

#if defined(FISHER_MAIN)
int main(int argc, char* const argv[])
{
	if (argc == 5)
	{
		// long p[2][2] = { { 7, 1876460 }, { 6, 2137355 }};
		long p[2][2] = {};
		p[0][0] = std::stol(argv[1]);
		p[0][1] = std::stol(argv[2]);
		p[1][0] = std::stol(argv[3]);
		p[1][1] = std::stol(argv[4]);

		std::cout << std::fixed << fisherTest2x2(p) << std::endl;
	}

	// std::vector<double> pv({ 0.020908895501239, 0.474875175724479 , 0.626191716145329 , 0.9151072684633, 0.604567972506964 , 0.525678354264758 , 0.679038623768489 , 0.0646323092167551 });
	// auto a = adjustFDR_BH(pv);

	// for (size_t i = 0; i < pv.size(); ++i)
	// 	std::cout << "i: " << i << " p: " << pv[i] << " => " << a[i] << std::endl;

	return 0;
}
#endif