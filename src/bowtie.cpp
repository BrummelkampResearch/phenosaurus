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

//	module to run bowtie and process results

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <regex>
#include <future>
#include <fstream>
#include <sys/uio.h>

#include <cassert>

#include <iostream>
#include <iomanip>
#include <filesystem>
#include <functional>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "bowtie.hpp"
#include "utils.hpp"
#include "job-scheduler.hpp"
#include "bsd-closefrom.h"

namespace fs = std::filesystem;
namespace io = boost::iostreams;
using namespace std::literals;

extern int VERBOSE;

// --------------------------------------------------------------------

double system_time()
{
	struct timeval tv;

	gettimeofday(&tv, nullptr);

	return tv.tv_sec + tv.tv_usec / 1e6;
}

// --------------------------------------------------------------------

std::unique_ptr<bowtie_parameters> bowtie_parameters::s_instance;

// --------------------------------------------------------------------

Insertion parseLine(const char* line, unsigned readLength)
{
	// result
	Insertion result = { INVALID, '+' };

	const char* s = line;

	// skip first field
	s = strchr(s, '\t');
	if (s == nullptr)
		throw std::runtime_error("Invalid input file");

	// this should be strand
	result.strand = *++s;
	s = strchr(s + 1, '\t');
	if ((result.strand != '+' and result.strand != '-') or s == nullptr)
		throw std::runtime_error("Invalid input file");
	
	// next is chromosome
	if (*++s == 'c' and s[1] == 'h' and s[2] == 'r')
	{
		s += 3;
		switch (*s++)
		{
			case '1':
				if (*s >= '0' and *s <= '9')
					result.chr = static_cast<CHROM>(10 + *s++ - '0');
				else
					result.chr = CHR_1;
				break;

			case '2':
				if (*s >= '0' and *s <= '3')
					result.chr = static_cast<CHROM>(20 + *s++ - '0');
				else
					result.chr = CHR_2;
				break;
			
			case '3':	result.chr = CHR_3;	break;
			case '4':	result.chr = CHR_4;	break;
			case '5':	result.chr = CHR_5;	break;
			case '6':	result.chr = CHR_6;	break;
			case '7':	result.chr = CHR_7;	break;
			case '8':	result.chr = CHR_8;	break;
			case '9':	result.chr = CHR_9;	break;
			case 'X':	result.chr = CHR_X;	break;
			case 'Y':	result.chr = CHR_Y;	break;
		}
	}

	if (result.chr != INVALID and *s++ == '\t')
	{
		while (*s >= '0' and *s <= '9')
			result.pos = result.pos * 10 + (*s++ - '0');

		if (result.strand == '-')
			result.pos += readLength;

		if (*s != '\t')
			throw std::runtime_error("Invalid input file");
	}

	return result;
}

// --------------------------------------------------------------------

struct progress_filter
{
	progress_filter() = delete;
	progress_filter(progress& p) : m_progress(p) {}
	
	progress_filter(const progress_filter& cf)
		: m_progress(const_cast<progress&>(cf.m_progress)) {}

	progress_filter& operator=(const progress_filter& cf) = delete;

	typedef char char_type;
	typedef io::multichar_input_filter_tag category;

	template<typename Source>
	std::streamsize read(Source& src, char* s, std::streamsize n)
	{
		auto r = boost::iostreams::read(src, s, n);
		if (r > 0)
			m_progress.consumed(r);
		
		return r;
	}

	progress& m_progress;
};

// -----------------------------------------------------------------------

std::vector<Insertion> runBowtieInt(const std::filesystem::path& bowtie,
	const std::filesystem::path& bowtieIndex, const std::filesystem::path& fastq,
	const std::filesystem::path& logFile, unsigned threads, unsigned trimLength,
	int maxmismatch = 0, std::filesystem::path mismatchfile = {})
{
	auto p = std::to_string(threads);
	auto v = std::to_string(maxmismatch);

	std::vector<const char*> args = {
		bowtie.c_str(),
		"-m", "1",
		"-v", v.c_str(),
		"--best",
		"-p", p.c_str(),
		bowtieIndex.c_str(),
		"-"
	};

	if (maxmismatch > 0)
	{
		args.push_back("--max");
		args.push_back(mismatchfile.c_str());
	}

	args.push_back(nullptr);

	if (not fs::exists(args.front()))
		throw std::runtime_error("The executable '"s + args.front() + "' does not seem to exist");

	if (not fs::exists(fastq))
		throw std::runtime_error("The FastQ file '" + fastq.string() + "' does not seem to exist");

	// ready to roll
	int ifd[2], ofd[2], err;

	err = pipe2(ifd, O_CLOEXEC); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe2(ofd, O_CLOEXEC); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));

	// open log file for appending
	int efd = open(logFile.c_str(), O_CREAT | O_APPEND | O_RDWR, 0644);
	const auto log_head = "\nbowtie output for " + fastq.string() + "\n" + std::string(18 + fastq.string().length(), '-') + "\n";
	write(efd, log_head.data(), log_head.size());

	int pid = fork();

	if (pid == 0)    // the child
	{
		setpgid(0, 0);        // detach from the process group, create new

		dup2(ifd[0], STDIN_FILENO);
		close(ifd[0]);
		close(ifd[1]);

		dup2(ofd[1], STDOUT_FILENO);
		close(ofd[0]);
		close(ofd[1]);

		dup2(efd, STDERR_FILENO);
		close(efd);

		closefrom(STDERR_FILENO + 1);

		const char* env[] = { nullptr };
		(void)execve(args.front(), const_cast<char* const*>(&args[0]), const_cast<char* const*>(env));
		exit(-1);
	}

	if (pid == -1)
	{
		close(ifd[0]);
		close(ifd[1]);
		close(ofd[0]);
		close(ofd[1]);
		close(efd);

		throw std::runtime_error("fork failed: "s + strerror(errno));
	}

	close(ifd[0]);

	std::exception_ptr ep;

	// always assume we have to trim (we used to check for trim length==read length, but that complicated the code too much)
	std::thread thread([trimLength, &fastq, fd = ifd[1], &ep]()
	{
		try
		{
			progress p(fs::file_size(fastq), fastq.string());
			p.set_action(fastq.filename().string());

			std::ifstream file(fastq, std::ios::binary);

			if (not file.is_open())
				throw std::runtime_error("Could not open file " + fastq.string());

			io::filtering_stream<io::input> in;
			std::string ext = fastq.extension().string();
			
			if (fastq.extension() == ".gz")
			{
				in.push(io::gzip_decompressor());
				ext = fastq.stem().extension().string();
			}

			in.push(progress_filter(p));
			
			in.push(file);

			char nl[1] = { '\n' };

			while (not in.eof())
			{
				// readLength != trimLength
				// read four lines

				std::string line[4];
				if (not std::getline(in, line[0]) or 
					not std::getline(in, line[1]) or 
					not std::getline(in, line[2]) or 
					not std::getline(in, line[3]))
				{
					break;
					// throw std::runtime_error("Could not read from " + fastq.string() + ", invalid file?");
				}

				if (line[0].length() < 2 or line[0][0] != '@')
					throw std::runtime_error("Invalid FastQ file " + fastq.string() + ", first line not valid");

				if (line[2].empty() or line[2][0] != '+')
					throw std::runtime_error("Invalid FastQ file " + fastq.string() + ", third line not valid");

				if (line[1].length() != line[3].length() or line[1].empty())
					throw std::runtime_error("Invalid FastQ file " + fastq.string() + ", no valid sequence data");			

				iovec v[8] = {
					{ line[0].data(), line[0].length() },
					{ nl, 1 },
					{ line[1].data(), trimLength },
					{ nl, 1 },
					{ line[2].data(), line[2].length() },
					{ nl, 1 },
					{ line[3].data(), trimLength },
					{ nl, 1 },
				};

				int r = writev(fd, v, 8);
				if (r < 0)
				{
					std::cerr << "Error writing to bowtie: " << strerror(errno) << std::endl;
					break;
				}
			}

			close(fd);
		}
		catch (const std::exception& ex)
		{
			ep = std::current_exception();
		}
	});

	close(ofd[1]);

	char buffer[8192];
	std::string line;
	std::vector<Insertion> result;

	for (;;)
	{
		int r = read(ofd[0], buffer, sizeof(buffer));

		if (r <= 0)	// keep it simple
			break;

		for (char* s = buffer; s < buffer + r; ++s)
		{
			char ch = *s;
			if (ch != '\n')
			{
				line += ch;
				continue;
			}
			
			try
			{
				auto ins = parseLine(line.c_str(), trimLength);
				if (ins.chr != INVALID)
				{
					result.push_back(ins);
					std::push_heap(result.begin(), result.end());
				}
			}
			catch (const std::exception& e)
			{
				std::cerr << std::endl
						  << "Exception parsing " << fastq << e.what() << std::endl
						  << line << std::endl
						  << std::endl;
			}

			line.clear();
		}
	}

	// should not happen... bowtie output is always terminated with a newline, right?
	if (not line.empty())
	{
		try
		{
			auto ins = parseLine(line.c_str(), trimLength);
			if (ins.chr != INVALID)
			{
				result.push_back(ins);
				std::push_heap(result.begin(), result.end());
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << std::endl
					  << line << std::endl;
		}
	}

	// return sorted and unique array of hits
	std::sort_heap(result.begin(), result.end());

	result.erase(std::unique(result.begin(), result.end()), result.end());

	thread.join();

	close(ofd[0]);
	close(efd);

	// no zombies please, removed the WNOHANG. the forked application should really stop here.
	int status = 0;
	int r = waitpid(pid, &status, 0);

	if (r == pid and WIFEXITED(status))
		status = WEXITSTATUS(status);

	if (status != 0)
		throw std::runtime_error("Error executing bowtie, result is " + std::to_string(status));

	if (ep)
		std::rethrow_exception(ep);

	return result;
}

// -----------------------------------------------------------------------

std::vector<Insertion> runBowtie(const std::filesystem::path& bowtie,
	const std::filesystem::path& bowtieIndex, const std::filesystem::path& fastq,
	const std::filesystem::path& logFile, unsigned threads, unsigned trimLength)
{
	fs::path m = fs::temp_directory_path() / ("mismatched-" + std::to_string(getpid()) + ".fastq");

	auto result = runBowtieInt(bowtie, bowtieIndex, fastq, logFile, threads, trimLength, 1, m);

	if (fs::exists(m))
	{
		if (fs::file_size(m) > 0)
		{
			auto ins_2 = runBowtieInt(bowtie, bowtieIndex, m, logFile, threads, trimLength);

			std::vector<Insertion> merged;
			merged.reserve(result.size() + ins_2.size());

			std::merge(result.begin(), result.end(), ins_2.begin(), ins_2.end(), std::back_inserter(merged));

			std::swap(result, merged);

			result.erase(std::unique(result.begin(), result.end()), result.end());
		}

		fs::remove(m);
	}

	return result;
}

// --------------------------------------------------------------------

std::string bowtieVersion(std::filesystem::path bowtie)
{

	if (not fs::exists(bowtie))
		throw std::runtime_error("The executable '" + bowtie.string() + "' does not seem to exist");

	std::vector<const char*> args = {
		bowtie.c_str(),
		"--version",
		nullptr
	};

	// ready to roll
	int ofd[2], efd[2], err;

	err = pipe2(ofd, O_CLOEXEC); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe2(efd, O_CLOEXEC); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));

	int pid = fork();

	if (pid == 0)    // the child
	{
		setpgid(0, 0);        // detach from the process group, create new

		// it is dubious if this is needed:
		signal(SIGHUP, SIG_IGN);
		signal(SIGCHLD, SIG_IGN);    // block child died signals

		// fork again, to avoid being able to attach to a terminal device
		pid = fork();

		if (pid == -1)
			std::cerr << "Fork failed" << std::endl;

		if (pid != 0)
			_exit(0);

		signal(SIGHUP, SIG_IGN);
		signal(SIGCHLD, SIG_IGN);    // block child died signals

		dup2(ofd[1], STDOUT_FILENO);
		close(ofd[0]);
		close(ofd[1]);

		dup2(efd[1], STDERR_FILENO);
		close(efd[0]);
		close(efd[1]);

		closefrom(STDERR_FILENO + 1);

		const char* env[] = { nullptr };
		(void)execve(args.front(), const_cast<char* const*>(&args[0]), const_cast<char* const*>(env));
		exit(-1);
	}

	if (pid == -1)
	{
		close(ofd[0]);
		close(ofd[1]);
		close(efd[0]);
		close(efd[1]);

		throw std::runtime_error("fork failed: "s + strerror(errno));
	}

	close(ofd[1]);
	close(efd[1]);

	// OK, so now the executable is started and the pipes are set up
	// read from the pipes until done.

	std::promise<std::string> p;
	std::future<std::string> f = p.get_future();

	std::thread t([fd = ofd[0], efd = efd[0], &p]()
	{
		try
		{
			std::string line, result;

			const std::regex rx(R"(/\S+ version (\d+\.\d+\.\d+)\n)");

			char buffer[8192];

			for (;;)
			{
				int r = read(fd, buffer, sizeof(buffer) - 1);
				if (r <= 0)
					break;

				if (not result.empty())
					continue;

				buffer[r] = 0;

				std::cmatch m;

				if (std::regex_search(buffer, m, rx) and m[1].matched)
					result.assign(m[1].str());
			}

			// drain stderr
			for (;;)
			{
				int r = read(efd, buffer, sizeof(buffer));
				if (r <= 0)
					break;

				std::cerr.write(buffer, r);
			}

			close(fd);
			close(efd);

			p.set_value(result);
		}
		catch(const std::exception& e)
		{
			p.set_exception(std::current_exception());
		}
	});

	// no zombies please, removed the WNOHANG. the forked application should really stop here.
	int status = 0;
	int r = waitpid(pid, &status, 0);

	t.join();

	if (r == pid and WIFEXITED(status))
		status = WEXITSTATUS(status);
	
	if (status != 0)
		throw std::runtime_error("Error executing bowtie, result is " + std::to_string(status));

	return f.get();
}

// // --------------------------------------------------------------------

// std::vector<Insertion> runBowtie(const std::string& assembly, std::filesystem::path fastq)
// {
// 	auto params = bowtie_parameters::instance();
// 	return runBowtie(params.bowtie(), params.bowtieIndex(assembly), fastq, params.threads(), params.trimLength());
// }

