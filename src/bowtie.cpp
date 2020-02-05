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
// #include <thread>
#include <functional>

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

void assignInsertions(const char* line, const std::vector<Transcript>& transcripts,
	std::vector<Insertions>& insertions)
{
	if (VERBOSE >= 2)
	{
		static size_t n = 0;
		if (++n % 100000 == 0)
		{
			std::cout << '.';
			std::cout.flush();

			if (n % 6000000 == 0)
				std::cout << ' ' << std::setw(8) << n << std::endl;
		}
	}

	const char* s = line;

	// skip first field
	s = strchr(s, '\t');
	if (s == nullptr)
		throw std::runtime_error("Invalid input file");

	// this should be strand
	char strand = *++s;
	s = strchr(s + 1, '\t');
	if ((strand != '+' and strand != '-') or s == nullptr)
		throw std::runtime_error("Invalid input file");
	
	// next is chromosome
	CHROM chr = INVALID;
	if (*++s == 'c' and s[1] == 'h' and s[2] == 'r')
	{
		s += 3;
		switch (*s++)
		{
			case '1':
				if (*s >= '0' and *s <= '9')
					chr = static_cast<CHROM>(10 + *s++ - '0');
				else
					chr = CHR_1;
				break;

			case '2':
				if (*s >= '0' and *s <= '3')
					chr = static_cast<CHROM>(20 + *s++ - '0');
				else
					chr = CHR_2;
				break;
			
			case '3':	chr = CHR_3;	break;
			case '4':	chr = CHR_4;	break;
			case '5':	chr = CHR_5;	break;
			case '6':	chr = CHR_6;	break;
			case '7':	chr = CHR_7;	break;
			case '8':	chr = CHR_8;	break;
			case '9':	chr = CHR_9;	break;
			case 'X':	chr = CHR_X;	break;
			case 'Y':	chr = CHR_Y;	break;
		}
	}

	if (chr != INVALID and *s++ == '\t' and *s != '\t')
	{
		long pos = strtoul(s, const_cast<char**>(&s), 10);
		if (*s != '\t')
			throw std::runtime_error("Invalid input file");
		
		// we have a valid hit at chr:pos, see if it matches a transcript

		long L = 0, R = transcripts.size() - 1;
		auto t = transcripts.data();

		while (L <= R)
		{
			auto i = (L + R) / 2;
			auto ti = t + i;
			long d = ti->chrom - chr;
			if (d == 0)
				d = ti->r.start - pos;
			if (d >= 0)
				R = i - 1;
			else
				L = i + 1;
		}

		auto e = t + transcripts.size();
		t += L > 0 ? L - 1 : L;
		while (t < e and t->chrom == chr and t->r.start <= pos)
		{
			if (t->r.end > pos)
			{
				if (VERBOSE >= 3)
					std::cerr << "hit " << t->geneName << " " << (strand == t->strand ? "sense" : "anti-sense") << std::endl;

				// insertions.push_back({ pos, t, strand == t->strand });
				if (strand == t->strand)
					insertions[t - transcripts.data()].sense.insert(pos);
				else
					insertions[t - transcripts.data()].antiSense.insert(pos);
			}
			
			++t;
		}
	}
}

// --------------------------------------------------------------------

Insertion parseLine(const char* line)
{
	// result
	Insertion result = { INVALID, 0, '+' };

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

	if (result.chr != INVALID and *s++ == '\t' and *s != '\t')
	{
		result.pos = strtoul(s, const_cast<char**>(&s), 10);
		if (*s != '\t')
			throw std::runtime_error("Invalid input file");
	}

	return result;
}

// --------------------------------------------------------------------

std::vector<Insertions> assignInsertions(std::istream& data, const std::vector<Transcript>& transcripts)
{
	std::vector<Insertions> result(transcripts.size());

	std::string line;

	while (std::getline(data, line))
		assignInsertions(line.c_str(), transcripts, result);

	return result;
}

// --------------------------------------------------------------------

std::vector<Insertions> assignInsertions(const std::string& bowtie,
	const std::string& index, const std::string& fastq,
	const std::vector<Transcript>& transcripts,
	size_t nrOfThreads)
{
	auto p = std::to_string(nrOfThreads); 

	std::vector<const char*> args = {
		bowtie.c_str(),
		"-p", p.c_str(),
		index.c_str(),
		fastq.c_str(),
		"--max", "/tmp/max.fastq",
		"-m", "1",
		"-v", "1",
		nullptr
	};

	if (not fs::exists(args.front()))
		throw std::runtime_error("The executable '"s + args.front() + "' does not seem to exist");

	// ready to roll
	double startTime = system_time();

	int ifd[2], ofd[2], efd[2], err;

	err = pipe(ifd); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe(ofd); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe(efd); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));

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

	// handle stdin, if any
	close(ifd[0]);



	// std::thread thread([&stdin, ifd, args]()
	// {
	//     char buffer[1024];

	//     while (not stdin.eof())
	//     {
	//         std::streamsize k = io::read(stdin, buffer, sizeof(buffer));

	//         if (k <= -1)
	//             break;

	//         const char* b = buffer;

	//         while (k > 0)
	//         {
	//             int r = write(ifd[1], b, k);
	//             if (r > 0)
	//                 b += r, k -= r;
	//             else if (r < 0 and errno != EAGAIN)
	//                 throw std::runtime_error("Error writing to command "s + args.front());
	//         }
	//     }

	//     close(ifd[1]);
	// });

	// make stdout and stderr non-blocking
	int flags;

	close(ofd[1]);
	flags = fcntl(ofd[0], F_GETFL, 0);
	fcntl(ofd[0], F_SETFL, flags | O_NONBLOCK);

	close(efd[1]);
	flags = fcntl(efd[0], F_GETFL, 0);
	fcntl(efd[0], F_SETFL, flags | O_NONBLOCK);

	// OK, so now the executable is started and the pipes are set up
	// read from the pipes until done.

	bool errDone = false, outDone = false, killed = false;
	double maxRunTime = 0;

	const size_t kBufferSize = 4096;
	char buffer[kBufferSize + 1] = {};
	int remaining = 0;

	std::vector<Insertions> insertions(transcripts.size());

	while (not errDone and not outDone and not killed)
	{
		while (not outDone)
		{
			int r = read(ofd[0], buffer + remaining, kBufferSize - remaining);
			if (r >= 0 and r < kBufferSize)
				buffer[r + remaining] = 0;

			if (r > 0)
			{
				auto s = buffer;
				auto e = buffer + remaining + r;
				while (s < e)
				{
					auto l = strchr(s, '\n');
					assert(l < e);

					if (l == nullptr)
					{
						remaining = e - s;
						memmove(buffer, s, remaining);
						buffer[remaining] = 0;
						break;
					}

					assignInsertions(s, transcripts, insertions);
					s = l + 1;
				}
			}
			else if (r == 0 or errno != EAGAIN)
			{
				if (remaining > 0)
					assignInsertions(buffer, transcripts, insertions);
				outDone = true;
			}
			else
				break;
		}

		while (not errDone)
		{
			char errBuffer[1024];
			int r = read(efd[0], errBuffer, sizeof(errBuffer));

			if (r > 0)
				std::cerr.write(errBuffer, r);
				// stderr.write(buffer, r);
			else if (r == 0 and errno != EAGAIN)
				errDone = true;
			else
				break;
		}

		if (not errDone and not outDone)
		{
			if (not killed and maxRunTime > 0 and startTime + maxRunTime < system_time())
			{
				kill(pid, SIGKILL);
				killed = true;

				std::cerr << std::endl
						  << "maximum run time exceeded" << std::endl;
			}
			else
				sleep(1);
		}
	}

	// thread.join();

	close(ofd[0]);
	close(efd[0]);

	// no zombies please, removed the WNOHANG. the forked application should really stop here.
	int status = 0;
	waitpid(pid, &status, 0);

	int result = -1;
	if (WIFEXITED(status))
		result = WEXITSTATUS(status);

	// return result;

	return insertions;

	// std::vector<Insertion> result;



	// // int out, err;
	// // int r = ForkExec(args, 0, -1, out, err);


	// if (r != 0)
	// {
	// 	std::cerr << "Error running bowtie" << std::endl
	// 			  << err.str() << std::endl;
	// }

	// return result;
}

// --------------------------------------------------------------------

struct counting_filter
{
	counting_filter() = delete;
	counting_filter(progress& p) : m_progress(p) {}
	
	counting_filter(const counting_filter& cf)
		: m_progress(const_cast<progress&>(cf.m_progress)) {}

	counting_filter& operator=(const counting_filter& cf) = delete;

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

std::vector<Insertion> runBowtie(std::filesystem::path bowtie, std::filesystem::path bowtieIndex,
	std::filesystem::path fastq, unsigned threads, unsigned readLength,
	int maxmismatch, std::filesystem::path mismatchfile)
{
	if (readLength)
		throw std::runtime_error("Sorry, not implemented yet");

	auto p = std::to_string(threads);
	auto v = std::to_string(maxmismatch);

	std::vector<const char*> args = {
		bowtie.c_str(),
		"-m", "1",
		"-v", v.c_str(),
		"--best",
		"-p", p.c_str(),
		bowtieIndex.c_str(),
		// fastq.c_str(),
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

	err = pipe(ifd); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe(ofd); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));
	err = pipe(efd); if (err < 0) throw std::runtime_error("Pipe error: "s + strerror(errno));

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

	// handle stdin, if any
	close(ifd[0]);

	boost::thread thread([&fastq, fd = ifd[1]]()
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

		counting_filter cf(p);
		in.push(cf);
		
		in.push(file);

		char buffer[1024];

	    while (not in.eof())
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

		std::cerr << "input done" << std::endl;

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
			if (r == 0)
				break;
			
			if (r > 0)
			{
				std::cerr.write(buffer, r);
				continue;
			}

			if (errno == EAGAIN)
				continue;
			
			break;
		}
	});

	char buffer[8192];
	std::string line;
	std::vector<Insertion> result;

	for (;;)
	{
		int r = read(ofd[0], buffer, sizeof(buffer));

		if (r == 0)
			break;
		
		if (r < 0)
		{
			if (errno == EAGAIN)
				continue;
			break;
		}

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
				auto ins = parseLine(line.c_str());
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

			line.clear();
		}
	}

	// should not happen... bowtie output is always terminated with a newline, right?
	if (not line.empty())
	{
		try
		{
			auto ins = parseLine(line.c_str());
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
	std::filesystem::path fastq, unsigned threads, unsigned readLength)
{
	fs::path m = fs::temp_directory_path() / ("mismatched-" + std::to_string(getpid()) + ".fastq");

	auto result = runBowtie(bowtie, bowtieIndex, fastq, threads, readLength, 1, m);

	if (fs::exists(m))
	{
		if (fs::file_size(m) > 0)
		{
			auto ins_2 = runBowtie(bowtie, bowtieIndex, m, threads, readLength, 0, m);

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