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

// --------------------------------------------------------------------

std::string get_user_name()
{
	struct passwd pwd;
	struct passwd *result = nullptr;
	char buf[16384];
	
	int s = getpwuid_r(getuid(), &pwd, buf, sizeof(buf), &result);

	return (s>= 0 and result != nullptr) ? result->pw_name : "";
}
