// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <filesystem>

#include <zeep/http/server.hpp>

zeep::http::server* createServer(const std::filesystem::path& screenDir);
