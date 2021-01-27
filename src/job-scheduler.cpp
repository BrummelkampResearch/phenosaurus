#include "config.hpp"

#include <functional>

#include <zeep/value-serializer.hpp>

#include "job-scheduler.hpp"
#include "screen-service.hpp"

// --------------------------------------------------------------------

map_job::~map_job()
{
}

void map_job::execute()
{
	m_screen->map(m_assembly);
}

void map_job::set_status(job_status_type status)
{
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
	push(nullptr);
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
		job* job = nullptr;

		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this] { return not m_queue.empty(); });

			job = m_queue.top();
			m_queue.pop();
		}

		if (job == nullptr)
			break;

		try
		{
			job->set_status(job_status_type::running);
			job->execute();
			job->set_status(job_status_type::finished);
		}
		catch(const std::exception& e)
		{
			std::cerr << e.what() << '\n';
			job->set_status(job_status_type::failed);
		}

		delete job;
	}
}