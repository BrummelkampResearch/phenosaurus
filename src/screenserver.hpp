// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <filesystem>

#include <zeep/http/server.hpp>

zeep::http::server* createServer(const std::filesystem::path& docroot, const std::filesystem::path& screenDir,
	const std::string& secret);
