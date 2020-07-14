//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

#include <zeep/crypto.hpp>

#include "user-service.hpp"
#include "db-connection.hpp"

// --------------------------------------------------------------------

const int
	kIterations = 30000,
	kKeyLength = 32;

// --------------------------------------------------------------------

user_service& user_service::instance()
{
	static std::unique_ptr<user_service> s_instance(new user_service());
	return *s_instance;
}

user_service::user_service()
{
}

zeep::http::user_details user_service::load_user(const std::string& username) const
{
	zeep::http::user_details result;

	try
	{
		pqxx::transaction tx(db_connection::instance());

		auto r = tx.exec1(
			"SELECT password, admin "
			"FROM auth.users "
			"WHERE username = " + tx.quote(username) + " AND active = true");
		tx.commit();

		result.username = username;
		result.password = r.at("password").as<std::string>();
		result.roles.insert("USER");
		if (r.at("admin").as<bool>())
			result.roles.insert("ADMIN");

	}
	catch (const std::exception& ex)
	{
		std::cerr << "Error loading user << " << username << ": " << ex.what() << std::endl;
		throw;
	}

	return result;
}

std::string user_service::create_password_hash(const std::string& password)
{
	zeep::http::pbkdf2_sha256_password_encoder enc(kIterations, kKeyLength);
	return enc.encode(password);
}

std::vector<user> user_service::get_all_users()
{
	pqxx::transaction tx(db_connection::instance());

	std::vector<user> users;
	for (auto const& [username, firstname, lastname, email, active, admin]:
		tx.stream<std::string,std::optional<std::string>,std::optional<std::string>,std::string,bool,bool>(
		"SELECT username, first_name, last_name, email, active, admin FROM auth.users"))
	{
		users.emplace_back(user{ username, firstname.value_or(""), lastname.value_or(""), email, active, admin });
	}

	tx.commit();

	return users;
}

bool user_service::user_exists(const std::string& username)
{
	pqxx::transaction tx(db_connection::instance());
	auto row = tx.exec1("SELECT COUNT(*) FROM auth.users WHERE username = " + tx.quote(username));
	tx.commit();

	return row[0].as<int>() == 1;
}

user user_service::get_user(const std::string& username)
{
	pqxx::transaction tx(db_connection::instance());

	auto row = tx.exec1("SELECT * FROM auth.users WHERE username = " + tx.quote(username));
	tx.commit();

	user user;

	user.username = username;
	user.email = row.at("email").as<std::string>();
	user.firstname = row.at("first_name").as<std::string>("");
	user.lastname = row.at("last_name").as<std::string>("");
	user.admin = row.at("admin").as<bool>();
	user.active = row.at("active").as<bool>();

	return user;
}

void user_service::create_user(const std::string& username, const std::string& firstname, const std::string& lastname,
	const std::string& email, const std::string& password, bool active, bool admin)
{
	auto pw = user_service::create_password_hash(password);

	pqxx::transaction tx(db_connection::instance());
	tx.exec0(
		"INSERT INTO auth.users (username, password, email, first_name, last_name, active, admin) "
	 	"VALUES(" + tx.quote(username) + ", " +
					tx.quote(pw) + ", " +
					tx.quote(email) + ", " +
					tx.quote(firstname) + ", " +
					tx.quote(lastname) + ", " +
					tx.quote(active) + ", " +
					tx.quote(admin) + ")");
	tx.commit();
}

void user_service::update_user(const std::string& username, const std::string& firstname, const std::string& lastname,
		const std::string& email, const std::string& password, bool active, bool admin)
{
	pqxx::transaction tx(db_connection::instance());

	if (password.empty())
		tx.exec0(
			"UPDATE auth.users SET email = " + tx.quote(email) + 
							 " first_name = " + tx.quote(firstname) + 
							  " last_name = " + tx.quote(lastname) + 
							  " active = " + tx.quote(active) + 
						   " admin = " + tx.quote(admin) + 
						 " WHERE username = " + tx.quote(username) + ")");
	else
		tx.exec0(
			"UPDATE auth.users SET email = " + tx.quote(email) + 
							   " password = " + tx.quote(user_service::create_password_hash(password)) +
							 " first_name = " + tx.quote(firstname) + 
							  " last_name = " + tx.quote(lastname) + 
							  " active = " + tx.quote(active) + 
						   " admin = " + tx.quote(admin) + 
						 " WHERE username = " + tx.quote(username) + ")");

	tx.commit();
}

void user_service::delete_user(const std::string& username)
{
	pqxx::transaction tx(db_connection::instance());
	tx.exec0("DELETE FROM auth.users WHERE username = " + tx.quote(username));
	tx.commit();
}

// --------------------------------------------------------------------

bool user_service::isValidUsername(const std::string& name)
{
	std::regex rx("^[a-z0-9_]{4,30}$", std::regex::icase);
	return std::regex_match(name, rx);
}

bool user_service::isValidPassword(const std::string& password)
{
	int special = 0, numbers = 0, upper = 0, lower = 0;
	for (auto ch: password)
	{
		if (std::isupper(ch))
			++upper;
		else if (std::islower(ch))
			++lower;
		else if (std::isdigit(ch))
			++numbers;
		else if (std::ispunct(ch))
			++special;
	}

	return password.length() >= 6 and upper and lower and numbers and special;
}

bool user_service::isValidEmail(const std::string& email)
{
	std::regex rx(R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))", std::regex::icase);
	return std::regex_match(email, rx);
}
