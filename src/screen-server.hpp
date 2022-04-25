// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <filesystem>

#include <zeep/http/server.hpp>

zeep::http::server* createServer(const std::filesystem::path& docroot,
	const std::filesystem::path& screenDir,
	const std::filesystem::path &transcriptDir,
	const std::string& secret, const std::string& context_name);
