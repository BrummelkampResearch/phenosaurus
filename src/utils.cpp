// copyright 2020 M.L. Hekkelman, NKI/AVL

#include "config.hpp"

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
#include "mrsrc.h"

const auto kProcessorCount = std::thread::hardware_concurrency();

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
	std::mutex mMutex;
	std::thread mThread;
	std::chrono::time_point<std::chrono::system_clock> mStart = std::chrono::system_clock::now();
};

void progress_impl::Run()
{
	try
	{
		for (;;)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			std::unique_lock lock(mMutex);

			if (mConsumed == mMax)
				break;

			if (mConsumed == mLast)
				continue;

			auto elapsed = std::chrono::system_clock::now() - mStart;

			if (elapsed < std::chrono::seconds(5))
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

namespace
{

std::ostream& operator<<(std::ostream& os, const std::chrono::duration<double>& t)
{
	uint64_t s = static_cast<uint64_t>(std::trunc(t.count()));
	if (s > 24 * 60 * 60)
	{
		uint32_t days = s / (24 * 60 * 60);
		os << days << "d ";
		s %= 24 * 60 * 60;
	}
	
	if (s > 60 * 60)
	{
		uint32_t hours = s / (60 * 60);
		os << hours << "h ";
		s %= 60 * 60;
	}
	
	if (s > 60)
	{
		uint32_t minutes = s / 60;
		os << minutes << "m ";
		s %= 60;
	}
	
	double ss = s + 1e-6 * (t.count() - s);
	
	os << std::fixed << std::setprecision(1) << ss << 's';

	return os;
}

}

void progress_impl::PrintDone()
{
	std::string::size_type width = 80;

	std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - mStart;

	std::ostringstream msgstr;
	msgstr << mAction << " done in " << elapsed << " cpu / %ws wall";
	auto msg = msgstr.str();

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
		m_impl->mThread.join();
	}

	delete m_impl;
}

void progress::consumed(int64_t n)
{
	if ((m_impl->mConsumed += n) >= m_impl->mMax and
		m_impl->mThread.joinable())
	{
		m_impl->mThread.join();
	}
}

void progress::set(int64_t n)
{
	if ((m_impl->mConsumed = n) >= m_impl->mMax and
		m_impl->mThread.joinable())
	{
		m_impl->mThread.join();
	}
}

void progress::message(const std::string& msg)
{
	std::unique_lock lock(m_impl->mMutex);
	m_impl->mMessage = msg;
}
