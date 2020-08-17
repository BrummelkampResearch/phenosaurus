#pragma once

#include <mutex>

#include <zeep/http/error-handler.hpp>

#include <pqxx/pqxx>

class db_connection
{
  public:
	using prepared_statement_factory = std::function<void(pqxx::connection&)>;

	static void init(const std::string& connection_string);
	static db_connection& instance();

	static pqxx::work start_transaction()
	{
		return pqxx::work(instance());
	}

	pqxx::connection& get_connection();

	operator pqxx::connection&()
	{
		return get_connection();
	}

	void reset();

	void register_prepared_statement_factory(prepared_statement_factory&& psf)
	{
		m_prepared_statement_factories.emplace_back(std::move(psf));
	}

  private:
	db_connection(const db_connection&) = delete;
	db_connection& operator=(const db_connection&) = delete;

	db_connection(const std::string& connectionString);

	std::string m_connection_string;

	static std::unique_ptr<db_connection> s_instance;
	static thread_local std::unique_ptr<pqxx::connection> s_connection;

	std::list<prepared_statement_factory> m_prepared_statement_factories;
};

// --------------------------------------------------------------------

class db_error_handler : public zeep::http::error_handler
{
  public:

	virtual bool create_error_reply(const zeep::http::request& req, std::exception_ptr eptr, zeep::http::reply& reply);
};

