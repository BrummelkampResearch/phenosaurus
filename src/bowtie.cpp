// copyright 2020 M.L. Hekkelman, NKI/AVL
//
//	module to run bowtie and process results

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>

#include <cassert>

#include <iostream>
#include <iomanip>
#include <filesystem>
#include <functional>

// #include <thread>
// Don't use GNU's version, it crashes...
#include <boost/thread.hpp>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/process.hpp>

#include "bowtie.hpp"
#include "utils.hpp"

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

std::vector<Insertion> runBowtieInt(std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
	std::filesystem::path fastq, unsigned threads, unsigned trimLength,
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

	// ready to roll
	int ifd[2], ofd[2], efd[2], err;

	err = pipe2(ifd, O_CLOEXEC); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe2(ofd, O_CLOEXEC); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe2(efd, O_CLOEXEC); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));

	int pid = fork();

	if (pid == 0)    // the child
	{
		setpgid(0, 0);        // detach from the process group, create new

		signal(SIGCHLD, SIG_IGN);    // block child died signals

		dup2(ifd[0], STDIN_FILENO);
		close(ifd[0]);
		close(ifd[1]);

		dup2(ofd[1], STDOUT_FILENO);
		close(ofd[0]);
		close(ofd[1]);

		dup2(efd[1], STDERR_FILENO);
		close(efd[0]);
		close(efd[1]);

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
		close(efd[0]);
		close(efd[1]);

		throw std::runtime_error("fork failed: "s + strerror(errno));
	}

	close(ifd[0]);

	// this sucks a little, we have to find out the readlength
	// but somehow we have to do it by prescanning the fastq file.
	// (the other solution didn't work properly....)

	unsigned readLength;

	{
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

		in.push(file);

		std::string line[4];
		if (not std::getline(in, line[0]) or 
			not std::getline(in, line[1]) or 
			not std::getline(in, line[2]) or 
			not std::getline(in, line[3]))
		{
			throw std::runtime_error("Could not read from " + fastq.string() + ", invalid file?");
		}

		if (line[0].length() < 2 or line[0][0] != '@')
			throw std::runtime_error("Invalid FastQ file " + fastq.string() + ", first line not valid");

		if (line[2].empty() or line[2][0] != '+')
			throw std::runtime_error("Invalid FastQ file " + fastq.string() + ", third line not valid");

		if (line[1].length() != line[3].length() or line[1].empty())
			throw std::runtime_error("Invalid FastQ file " + fastq.string() + ", no valid sequence data");			

		readLength = line[1].length();
	}

	if (readLength == 0)
		throw std::runtime_error("invalid read length...");
	
	if (trimLength == 0)
		trimLength = readLength;

	boost::thread thread([trimLength=(trimLength == readLength ? trimLength : 0), &fastq, fd = ifd[1]]()
	{
		progress p(fastq.string(), fs::file_size(fastq));
		p.message(fastq.filename().string());

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

		char buffer[1024];
		char nl[1] = { '\n' };

		while (not in.eof())
		{
			if (trimLength)
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
			else
			{
				std::streamsize k = io::read(in, buffer, sizeof(buffer));

				if (k <= -1)
					break;

				int r = write(fd, buffer, k);
				if (r != k)
				{
					std::cerr << "Error writing to bowtie: " << strerror(errno) << std::endl;
					break;
				}
			}
		}

	    close(fd);
	});

	close(ofd[1]);
	close(efd[1]);

	// OK, so now the executable is started and the pipes are set up
	// read from the pipes until done.

	// start a thread to read stderr, can be used for logging

	boost::thread err_thread([fd = efd[0]]()
	{
		char buffer[1024];
		for (;;)
		{
			int r = read(fd, buffer, sizeof(buffer));
			if (r <= 0)
				break;

			std::cerr.write(buffer, r);
		}
	});

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
			catch(const std::exception& e)
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
		catch(const std::exception& e)
		{
			std::cerr << e.what() << std::endl
						<< line << std::endl;
		}
	}

	// return sorted and unique array of hits
	std::sort_heap(result.begin(), result.end());

	result.erase(std::unique(result.begin(), result.end()), result.end());

	thread.join();
	err_thread.join();

	close(ofd[0]);
	close(efd[0]);

	// no zombies please, removed the WNOHANG. the forked application should really stop here.
	int status = 0;
	waitpid(pid, &status, 0);

	int r = -1;
	if (WIFEXITED(status))
		r = WEXITSTATUS(status);
	
	if (r != 0)
		throw std::runtime_error("Error executing bowtie, result is " + std::to_string(r));

	return result;
}

// -----------------------------------------------------------------------

std::vector<Insertion> runBowtie(std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
	std::filesystem::path fastq, unsigned threads, unsigned trimLength)
{
	fs::path m = fs::temp_directory_path() / ("mismatched-" + std::to_string(getpid()) + ".fastq");

	auto result = runBowtieInt(bowtie, bowtieIndex, fastq, threads, trimLength, 1, m);

	if (fs::exists(m))
	{
		if (fs::file_size(m) > 0)
		{
			auto ins_2 = runBowtieInt(bowtie, bowtieIndex, m, threads, 0);

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