//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

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
	if (not s_connection)
		s_connection.reset(new pqxx::connection(m_connection_string));
	return *s_connection;
}
