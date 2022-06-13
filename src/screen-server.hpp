// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <filesystem>

#include <zeep/http/server.hpp>

// Regular (internal) server
zeep::http::server* createServer(const std::filesystem::path& docroot,
	const std::filesystem::path& screenDir,
	const std::filesystem::path &transcriptDir,
	const std::string& secret, const std::string& context_name);

// Limited public server
zeep::http::server* createPublicServer(const std::filesystem::path& docroot,
	const std::filesystem::path& screenDir,
	const std::filesystem::path &transcriptDir,
	const std::string& context_name);

