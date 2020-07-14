#pragma once

#include <mutex>

#include <pqxx/pqxx>

class db_connection
{
  public:
	static void init(const std::string& connection_string);
	static db_connection& instance();

	pqxx::connection& get_connection();

	operator pqxx::connection&()
	{
		return get_connection();
	}

  private:
	db_connection(const db_connection&) = delete;
	db_connection& operator=(const db_connection&) = delete;

	db_connection(const std::string& connectionString);

	std::string m_connection_string;

	static std::unique_ptr<db_connection> s_instance;
	static thread_local std::unique_ptr<pqxx::connection> s_connection;
};