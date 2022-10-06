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
