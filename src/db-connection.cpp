//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "db-connection.hpp"

// --------------------------------------------------------------------

std::unique_ptr<db_connection> db_connection::s_instance;
thread_local std::unique_ptr<pqxx::connection> db_connection::s_connection;

void db_connection::init(const std::string& connection_string)
{
	s_instance.reset(new db_connection(connection_string));
}

db_connection& db_connection::instance()
{
	return *s_instance;
}

// --------------------------------------------------------------------

db_connection::db_connection(const std::string& connectionString)
	: m_connection_string(connectionString)
{
}

pqxx::connection& db_connection::get_connection()
{
	static std::mutex sLock;
	std::unique_lock lock(sLock);

	if (not s_connection)
	{
		s_connection.reset(new pqxx::connection(m_connection_string));

		for (auto& psf: m_prepared_statement_factories)
			psf(*s_connection);
	}

	return *s_connection;
}

void db_connection::reset()
{
	s_connection.reset();
}

// --------------------------------------------------------------------

bool db_error_handler::create_error_reply(const zeep::http::request& req, std::exception_ptr eptr, zeep::http::reply& reply)
{
	try
	{
		std::rethrow_exception(eptr);
	}
	catch (pqxx::broken_connection& ex)
	{
		std::cerr << ex.what() << std::endl;
		db_connection::instance().reset();
	}
	catch (...)
	{
	}
	
	return false;
}
