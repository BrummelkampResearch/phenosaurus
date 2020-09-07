// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

#include <pwd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>
#include <regex>
#include <atomic>
#include <mutex>

#include <boost/thread.hpp>
#include <boost/timer/timer.hpp>

#include <zeep/streambuf.hpp>

#include "utils.hpp"
#include "mrsrc.h"

// --------------------------------------------------------------------

void parallel_for(size_t N, std::function<void(size_t)>&& f)
{
// #if DEBUG
//     for (size_t i = 0; i < N; ++i)
//         f(i);
// #else
	std::atomic<size_t> i = 0;

	std::exception_ptr eptr;
	std::mutex m;

	boost::thread_group t;
	for (size_t n = 0; n < boost::thread::hardware_concurrency(); ++n)
		t.create_thread([N, &i, &f, &eptr, &m]()
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

	t.join_all();

	if (eptr)
		std::rethrow_exception(eptr);

// #endif
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

// --------------------------------------------------------------------

struct progress_impl
{
	progress_impl(std::string action, int64_t max)
		: mMax(max), mLast(0), mConsumed(0), mAction(action), mMessage(action)
		, mSpinner(0), mThread(std::bind(&progress_impl::Run, this)) {}
	~progress_impl() {}

	void Run();

	void PrintProgress();
	void PrintDone();

	std::string mDatabank;
	long mMax, mLast;
	std::atomic<long> mConsumed;
	std::string mAction, mMessage;
	int mSpinner;
	boost::mutex mMutex;
	boost::thread mThread;
	boost::timer::cpu_timer mTimer;
};

void progress_impl::Run()
{
	try
	{
		for (;;)
		{
			boost::this_thread::sleep(boost::posix_time::seconds(1));

			boost::unique_lock<boost::mutex> lock(mMutex);

			if (mConsumed == mMax)
				break;

			if (mConsumed == mLast)
				continue;

			PrintProgress();
			mLast = mConsumed;
		}
	}
	catch (...) {}

	PrintDone();
}

void progress_impl::PrintProgress()
{
	int width = 80;
	float progress = -1.0f;

	std::string msg;
	msg.reserve(width + 1);
	if (mMessage.length() <= 20)
	{
		msg = mMessage;
		if (msg.length() < 20)
			msg.append(20 - msg.length(), ' ');
	}
	else
		msg = mMessage.substr(0, 17) + "...";

	if (mMax == std::numeric_limits<int64_t>::max())
	{
		const char kSpinner[] = { '|', '/', '-', '\\' };

		mSpinner = (mSpinner + 1) % 4;

		msg += ' ';
		msg += kSpinner[mSpinner];
	}
	else
	{
		msg += " [";

		progress = static_cast<float>(mConsumed) / mMax;
		int tw = width - 28;
		int twd = static_cast<int>(tw * progress + 0.5f);
		msg.append(twd, '=');
		msg.append(tw - twd, ' ');
		msg.append("] ");

		int perc = static_cast<int>(100 * progress);
		if (perc < 100)
			msg += ' ';
		if (perc < 10)
			msg += ' ';
		msg += std::to_string(perc);
		msg += '%';
	}

	if (isatty(STDOUT_FILENO))
	{
		std::cout << '\r' << msg;
		std::cout.flush();
	}
}

void progress_impl::PrintDone()
{
	int width = 80;

	std::string msg = mTimer.format(0, mAction + " done in %ts cpu / %ws wall");
	if (msg.length() < width)
		msg += std::string(width - msg.length(), ' ');

	if (isatty(STDOUT_FILENO))
		std::cout << '\r' << msg << std::endl;
	else
		std::cout << msg << std::endl;
}

// --------------------------------------------------------------------

progress::progress(const std::string& action, int64_t max)
	: m_impl(new progress_impl(action, max))
{
}

progress::~progress()
{
	if (m_impl->mThread.joinable())
	{
		m_impl->mConsumed = m_impl->mMax;

		m_impl->mThread.interrupt();
		m_impl->mThread.join();
	}

	delete m_impl;
}

void progress::consumed(int64_t n)
{
	if ((m_impl->mConsumed += n) >= m_impl->mMax and
		m_impl->mThread.joinable())
	{
		m_impl->mThread.interrupt();
		m_impl->mThread.join();
	}
}

void progress::set(int64_t n)
{
	if ((m_impl->mConsumed = n) >= m_impl->mMax and
		m_impl->mThread.joinable())
	{
		m_impl->mThread.interrupt();
		m_impl->mThread.join();
	}
}

void progress::message(const std::string& msg)
{
	boost::unique_lock<boost::mutex> lock(m_impl->mMutex);
	m_impl->mMessage = msg;
}
