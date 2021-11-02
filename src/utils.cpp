// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <pwd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <thread>
#include <iomanip>

#include <iostream>
#include <regex>
#include <atomic>
#include <mutex>

#include <zeep/streambuf.hpp>

#include "utils.hpp"
#include "mrsrc.hpp"

const auto kProcessorCount = std::thread::hardware_concurrency();

// --------------------------------------------------------------------

void parallel_for(size_t N, std::function<void(size_t)>&& f)
{

#if DEBUG
	if (getenv("NO_PARALLEL"))
	{
		for (size_t i = 0; i < N; ++i)
			f(i);
		return;
	}
#endif

	std::atomic<size_t> i = 0;

	std::exception_ptr eptr;
	std::mutex m;

	std::list<std::thread> t;

	for (size_t n = 0; n < kProcessorCount; ++n)
		t.emplace_back([N, &i, &f, &eptr, &m]()
		{
			try
			{
				for (;;)
				{
					auto next = i++;
					if (next >= N)
						break;
				
					f(next);
				}
			}
			catch(const std::exception& e)
			{
				std::unique_lock lock(m);
				eptr = std::current_exception();
			}
		});

	for (auto& ti: t)
		ti.join();

	if (eptr)
		std::rethrow_exception(eptr);
}

// -----------------------------------------------------------------------

int get_terminal_width()
{
	int result = 80;

	if (isatty(STDOUT_FILENO))
	{
		struct winsize w;
		ioctl(0, TIOCGWINSZ, &w);
		result = w.ws_col;
	}
	return result;
}

// -----------------------------------------------------------------------

namespace {
	std::string gVersionNr, gVersionDate, gVersionTag;
}

void load_version_info()
{
	const std::regex
		rxVersionNr(R"(build-(\d+)-g([0-9a-f]{7})(-dirty)?)"),
		rxVersionDate(R"(Date: +(\d{4}-\d{2}-\d{2}).*)");

	auto version = mrsrc::rsrc("version.txt");
	if (version)
	{
		mrsrc::istream is(version);
		std::string line;

		while (std::getline(is, line))
		{
			std::smatch m;

			if (std::regex_match(line, m, rxVersionNr))
			{
				gVersionNr = m[1];
				gVersionTag = m[2];
				if (m[3].matched)
					gVersionTag += '*';
				continue;
			}

			if (std::regex_match(line, m, rxVersionDate))
			{
				gVersionDate = m[1];
				continue;
			}
		}
	}
}

std::string get_version_nr()
{
	if (gVersionNr.empty())
		load_version_info();

	return gVersionNr;
}

std::string get_version_date()
{
	if (gVersionDate.empty())
		load_version_info();

	return gVersionDate;
}

void showVersionInfo()
{
	if (gVersionNr.empty())
		load_version_info();

	std::cout << "Build: " << gVersionNr << ", Date: " << gVersionDate << ", Tag: " << gVersionTag << std::endl;
}

// --------------------------------------------------------------------

std::string get_user_name()
{
	struct passwd pwd;
	struct passwd *result = nullptr;
	char buf[16384];
	
	int s = getpwuid_r(getuid(), &pwd, buf, sizeof(buf), &result);

	return (s>= 0 and result != nullptr) ? result->pw_name : "";
}
