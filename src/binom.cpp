#include "config.hpp"

#include <boost/math/special_functions/beta.hpp>

#include "binom.hpp"

namespace bm = boost::math;

inline double xlogy(double x, double y)
{
	return (x == 0 and not std::isnan(y)) 
		? 0
		: x * std::log(y);
}

inline double xlog1py(double x, double y)
{
	return (x == 0 and not std::isnan(y))
		? 0
		: x * std::log1p(y);
}

double binom_pmf(int x, int n, double p)
{
	double combiln = std::lgamma(n + 1) - (std::lgamma(x + 1) + std::lgamma(n - x + 1));
	return std::exp(combiln + xlogy(x, p) + xlog1py(n - x, -p));
}

double binom_cdf(double x, double n, double p)
{
	double result;

	if (x == n)
		result = 1;
	else if (x == 0)
		result = std::pow(1.0 - p, n - x);
	else
		result = bm::ibeta(n - x, x + 1, 1.0 - p);

	return result;
}

double binom_sf(double x, double n, double p)
{
	double result;

	if (x == n)
		result = 0;
	else if (x == 0)
	{
		if (p < 0.01)
			return -std::expm1((n - x) * log1p(-p));
		else
			return 1.0 - std::pow(1.0 - p, n - x);
	}
	else
		result = bm::ibeta(x + 1, n - x, p);

	return result;
}

double binom_test(int x, int n, double p)
{
	if (p < 0 or p > 1)
		throw std::invalid_argument("p should be in the range 0 <= p <= 1");

	double d = binom_pmf(x, n, p);
	double rerr = 1 + 1e-7;
	double d_rerr = d * rerr;

	double pval = 1;

	if (x < p * n)
	{
		int y = 0;

		for (int i = static_cast<int>(std::ceil(p * n)); i <= n; ++i)
		{
			auto di = binom_pmf(i, n, p);
			if (di <= d_rerr)
				++y;
		}

		pval = binom_cdf(x, n, p) + binom_sf(n - y, n, p);
	}
	else if (x > p * n)
	{
		int y = 0;

		for (int i = 0; i <= static_cast<int>(std::floor(p * n)); ++i)
		{
			auto di = binom_pmf(i, n, p);
			if (di <= d_rerr)
				++y;
		}

		pval = binom_cdf(y - 1, n, p) + binom_sf(x - 1, n, p);
	}

	return pval;
}