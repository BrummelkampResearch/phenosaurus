// copyright 2020 M.L. Hekkelman, NKI/AVL

#pragma once

int get_terminal_width();
void showVersionInfo();

// -----------------------------------------------------------------------

class progress
{
  public:
	progress(const std::string &action, int64_t max);
	progress(const progress &) = delete;
	progress &operator=(const progress &) = delete;

	// indefinite version, shows ascii spinner
	progress(const std::string &action);

	virtual ~progress();

	void consumed(int64_t n); // consumed is relative
	void set(int64_t n); // progress is absolute

	void message(const std::string &msg);

  private:
	struct progress_impl *m_impl;
};
