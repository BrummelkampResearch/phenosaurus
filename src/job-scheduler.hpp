#pragma once

#include <thread>
#include <mutex>
#include <vector>
#include <queue>

#include "screen-data.hpp"

// --------------------------------------------------------------------

enum class job_status_type
{
	unknown,
	queued,
	running,
	finished,
	failed
};

using job_id = uint32_t;

class job
{
  public:

	job_id id() const						{ return m_id; }	
	const std::string& name() const			{ return m_name; }
	constexpr int priority() const			{ return m_priority; }
	job_status_type status() const			{ return m_status; }
	// float perc_done() const					{ return m_perc_done; }

  protected:

	job(const std::string& name, int priority = 1)
		: m_name(name), m_priority(priority) {}
	virtual ~job() {}

	job(const job&) = delete;
	job& operator=(const job&) = delete;

	virtual void set_status(job_status_type status)
	{
		m_status = status;
	}

	virtual void execute() = 0;

  private:
	friend class job_scheduler;

	job_id m_id;
	std::string m_name;
	int m_priority;
	job_status_type m_status = job_status_type::unknown;
	// float m_perc_done = 0.f;
};

// --------------------------------------------------------------------

class map_job : public job
{
  public:
	map_job(std::unique_ptr<ScreenData>&& screen, const std::string& assembly)
		: job("mapping " + screen->name())
		, m_screen(std::move(screen)), m_assembly(assembly) {}
	virtual ~map_job();

	virtual void execute();
	virtual void set_status(job_status_type status);

  private:
	std::unique_ptr<ScreenData>	m_screen;
	std::string m_assembly;
};

// --------------------------------------------------------------------

class job_scheduler
{
  public:

	static job_scheduler& instance();

	job_id push(job* job)
	{
		std::unique_lock lock(m_mutex);
		m_queue.push(job);
		if (job)	
			job->set_status(job_status_type::queued);
		m_cv.notify_one();
		return m_next_job_id++;
	}

  private:
	job_scheduler();
	job_scheduler(const job_scheduler&) = delete;
	job_scheduler& operator=(const job_scheduler&) = delete;
	~job_scheduler();

	void run();

	struct compare_jobs
	{
		constexpr bool operator()(const job* a, const job* b) const
		{
			return a->priority() < b->priority();
		}
	};

	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::thread m_thread;
	std::priority_queue<job*, std::vector<job*>, compare_jobs> m_queue;
	job_id m_next_job_id = 1;
};