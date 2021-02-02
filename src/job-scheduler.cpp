#include "config.hpp"

#include <functional>

#include <zeep/value-serializer.hpp>

#include "job-scheduler.hpp"
#include "screen-service.hpp"

// --------------------------------------------------------------------

map_job::map_job(std::unique_ptr<ScreenData>&& screen, const std::string& assembly)
	: job(screen->name())
	, m_screen(std::move(screen)), m_assembly(assembly)
{
}

map_job::~map_job()
{
}

void map_job::execute()
{
	m_screen->map(m_assembly);
}

void map_job::set_status(job_status_type status)
{
	job::set_status(status);

	if (status == job_status_type::finished)
		screen_service::instance().screen_mapped(m_screen);
}

// --------------------------------------------------------------------

job_scheduler::job_scheduler()
	: m_thread(std::bind(&job_scheduler::run, this))
{
	zeep::value_serializer<job_status_type>::init("job-status", {
		{ job_status_type::unknown, 	"unknown" },
		{ job_status_type::queued, 		"queued" },
		{ job_status_type::running, 	"running" },
		{ job_status_type::failed, 		"failed" },
		{ job_status_type::finished, 	"finished" }
	});
}

job_scheduler::~job_scheduler()
{
	push({});
	m_thread.join();
}

job_scheduler& job_scheduler::instance()
{
	static job_scheduler s_instance;
	return s_instance;
}

void job_scheduler::run()
{
	for (;;)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		if (m_queue.empty())
		{
			m_cv.wait(lock, [this] { return not m_queue.empty(); });
			continue;
		}

		m_current = m_queue.front();
		m_queue.pop_front();

		if (not m_current)
			break;

		// unlock, to allow push, etc.
		lock.unlock();

		try
		{
			m_current->set_status(job_status_type::running);
			m_current->execute();
			m_current->set_status(job_status_type::finished);
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			m_current->set_status(job_status_type::failed);
		}

		m_current.reset();
	}
}

std::shared_ptr<job> job_scheduler::current_job()
{
	std::lock_guard lock(m_mutex);
	return m_current;
}

std::optional<job_status> job_scheduler::get_job_status_for_screen(const std::string& screen)
{
	std::lock_guard lock(m_mutex);

	std::optional<job_status> result;

	if (m_current and m_current->name() == screen)
		result = m_current->get_status();
	else
	{
		auto i = std::find_if(m_queue.begin(), m_queue.end(), [screen](auto job) { return job->name() == screen; });

		if (i != m_queue.end())
			result = (*i)->get_status();
	}

	return result;
}

// --------------------------------------------------------------------

progress::progress(int64_t max, const std::string& action)
	: m_job(job_scheduler::instance().current_job()), m_max(max), m_action(action)
	, m_cur(0)
	, m_last_update(std::chrono::system_clock::now())
{

}

void progress::consumed(int64_t n)	// consumed is relative
{
	using namespace std::literals;

	auto cur = m_cur += n;

	if (cur > m_max)
		cur = m_max;

	float p = static_cast<float>(cur) / m_max;
	auto now = std::chrono::system_clock::now();

	if (p >= 1.0f or (now - m_last_update) > 5s)
	{
		m_job->set_progress(p, m_action);
		m_last_update = now;
	}
}

void progress::set_progress(int64_t n)		// progress is absolute
{
	using namespace std::literals;

	auto cur = n;

	if (cur > m_max)
		cur = m_max;

	float p = static_cast<float>(cur) / m_max;
	auto now = std::chrono::system_clock::now();

	if (p >= 1.0f or (now - m_last_update) > 5s)
		m_job->set_progress(p, m_action);
}

void progress::set_action(const std::string& action)
{
	m_action = action;
}
