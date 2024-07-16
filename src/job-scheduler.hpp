/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <zeep/nvp.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

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

// --------------------------------------------------------------------

struct job_status
{
	job_status_type m_status;
	float m_progress;
	std::string m_action;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar &zeep::name_value_pair("status", m_status) & zeep::name_value_pair("progress", m_progress) & zeep::name_value_pair("action", m_action);
	}
};

class job : std::enable_shared_from_this<job>
{
  public:
	const std::string &name() const { return m_name; }
	job_status_type status() const { return m_status; }
	float progress() const { return m_progress; }
	// const std::string& action() const		{ return m_action; }

	void set_progress(float progress, const std::string &action)
	{
		std::lock_guard lock(m_mutex);

		m_progress = progress;
		m_action = action;
	}

	job_status get_status()
	{
		std::lock_guard lock(m_mutex);
		return { m_status, m_progress, m_action };
	}

  protected:
	job(const std::string &name)
		: m_name(name)
	{
	}
	virtual ~job() {}

	job(const job &) = delete;
	job &operator=(const job &) = delete;

	virtual void set_status(job_status_type status)
	{
		std::lock_guard lock(m_mutex);

		m_status = status;
	}

	virtual void execute() = 0;

  private:
	friend class job_scheduler;

	std::mutex m_mutex;
	std::string m_name;
	job_status_type m_status = job_status_type::unknown;
	float m_progress = 0.f;
	std::string m_action;
};

// --------------------------------------------------------------------

class ScreenData;

class map_job : public job
{
  public:
	map_job(std::unique_ptr<ScreenData> &&screen, const std::string &assembly);
	virtual ~map_job();

	virtual void execute();
	virtual void set_status(job_status_type status);

  private:
	std::unique_ptr<ScreenData> m_screen;
	std::string m_assembly;
};

// --------------------------------------------------------------------

class progress
{
  public:
	progress(int64_t max, const std::string &action);

	void consumed(int64_t n);     // consumed is relative
	void set_progress(int64_t n); // progress is absolute
	void set_action(const std::string &action);

  private:
	progress(const progress &) = delete;
	progress &operator=(const progress &) = delete;

	std::shared_ptr<job> m_job;
	int64_t m_max;
	std::string m_action;
	std::atomic<int64_t> m_cur;
	std::chrono::system_clock::time_point
		m_last_update;
};

// --------------------------------------------------------------------

class job_scheduler
{
  public:
	static job_scheduler &instance();

	job_id push(std::shared_ptr<job> job)
	{
		std::lock_guard lock(m_mutex);

		m_queue.push_back(job);
		if (job)
			job->set_status(job_status_type::queued);

		m_cv.notify_one();

		return m_next_job_id++;
	}

	std::shared_ptr<job> current_job();
	std::optional<job_status> get_job_status_for_screen(const std::string &screen);

  private:
	job_scheduler();
	job_scheduler(const job_scheduler &) = delete;
	job_scheduler &operator=(const job_scheduler &) = delete;
	~job_scheduler();

	void run();

	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::thread m_thread;
	std::deque<std::shared_ptr<job>> m_queue;
	std::shared_ptr<job> m_current;
	job_id m_next_job_id = 1;
};
