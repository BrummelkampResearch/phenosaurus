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
#include <filesystem>
#include <thread>

#include "bowtie.hpp"

namespace fs = std::filesystem;
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

int ForkExec(std::vector<const char*>& args, double maxRunTime, std::istream& stdin, std::ostream& stdout, std::ostream& stderr)
{
    if (args.empty() or args.front() == nullptr)
        throw std::runtime_error("No arguments to ForkExec");

    if (args.back() != nullptr)
        args.push_back(nullptr);

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

    while (not errDone and not outDone and not killed)
    {
        char buffer[1024];
        int r;

        while (not outDone)
        {
            r = read(ofd[0], buffer, sizeof(buffer));

            if (r > 0)
                stdout.write(buffer, r);
            else if (r == 0 or errno != EAGAIN)
                outDone = true;
            else
                break;
        }

        while (not errDone)
        {
            r = read(efd[0], buffer, sizeof(buffer));

            if (r > 0)
                stderr.write(buffer, r);
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

                stderr << std::endl
                       << "maximum run time exceeded"
                       << std::endl;
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

    return result;
}

// --------------------------------------------------------------------

std::vector<Insertion> assignInsertions(std::istream& data, const std::vector<Transcript>& transcripts)
{
	std::vector<Insertion> result;

	std::string line;

	while (std::getline(data, line))
	{
		const char* s = line.c_str();

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

		if (chr == INVALID or *s++ != '\t' or *s == '\t')
			continue;

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

		assert(L >= 0);

		auto e = t + transcripts.size();
		t += L;
		while (t < e and t->chrom == chr and t->r.start <= pos)
		{
			if (t->r.end > pos)
			{
				if (VERBOSE)
					std::cerr << "hit " << t->geneName << " " << (strand == t->strand ? "sense" : "anti-sense") << std::endl;

				result.push_back({ pos, t, strand == t->strand });
			}
			
			++t;
		}
	}

	return result;
}

// --------------------------------------------------------------------

std::vector<Insertion> assignInsertions(const std::string& bowtie,
	const std::string& index, const std::string& fastq,
	const std::vector<Transcript>& transcripts)
{
	std::stringstream in, out, err;

	std::vector<const char*> args = {
		bowtie.c_str(),
		


	};

	std::vector<Insertion> result;

	std::thread t([&]()
	{
		result = assignInsertions(out, transcripts);
	});

	int r = ForkExec(args, 0, in, out, err);

	t.join();

	return result;
}
