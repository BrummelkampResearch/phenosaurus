// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

#include <functional>

// --------------------------------------------------------------------

void parallel_for(size_t N, std::function<void(size_t)>&& f);

// --------------------------------------------------------------------

int get_terminal_width();
void showVersionInfo();
std::string get_user_name();


