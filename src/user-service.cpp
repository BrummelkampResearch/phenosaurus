//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

#include <zeep/crypto.hpp>

#include "user-service.hpp"
#include "screen-service.hpp"
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
	db_connection::instance().register_prepared_statement_factory([](pqxx::connection& connection)
	{
		connection.prepare("screen-is-allowed-for-user",
			R"(
				SELECT 1 FROM auth.users a, screens b
				 WHERE b.name = $1 and a.username = $2
				   AND (b.scientist_id = a.id OR EXISTS
				   	(SELECT * FROM auth.screen_permissions p
					  WHERE p.screen_id = b.id AND p.group_id IN
					   (SELECT group_id FROM auth.members WHERE user_id = a.id
					     UNION
						SELECT id FROM auth.groups WHERE name = 'public')))
		)");

		connection.prepare("allowed-screens-for-user",
			R"(
				SELECT a.name FROM screens a, auth.users b
				 WHERE b.username = $1
				   AND a.scientist_id = b.id
				    OR EXISTS (
						SELECT * FROM auth.screen_permissions p
						 WHERE p.screen_id = a.id
						   AND p.group_id IN (
							SELECT group_id FROM auth.members WHERE user_id = b.id
							 UNION
							SELECT id FROM auth.groups WHERE name = 'public'
						   )
					)
			)");
	});
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
	for (auto const& [id, username, firstname, lastname, email, active, admin]:
		tx.stream<uint32_t, std::string,std::optional<std::string>,std::optional<std::string>,std::string,bool,bool>(
		"SELECT id, username, first_name, last_name, email, active, admin FROM auth.users"))
	{
		users.emplace_back(user{ id, username, firstname.value_or(""), lastname.value_or(""), email, {}, active, admin });
	}

	tx.commit();

	return users;
}

std::vector<group> user_service::get_all_groups()
{
	pqxx::transaction tx(db_connection::instance());

	std::vector<group> groups;

	for (auto const& [id, name, member]:
		tx.stream<uint32_t, std::string, std::optional<std::string>>(
			"SELECT g.id, g.name, u.username FROM auth.groups g LEFT JOIN auth.members m ON g.id = m.group_id LEFT JOIN auth.users u ON m.user_id = u.id ORDER BY g.name"))
	{
		if (groups.empty() or groups.back().id != id)
			groups.emplace_back(group{id, name});

		if (member)
			groups.back().members.push_back(*member);
	}

	tx.commit();

	return groups;
}

bool user_service::user_exists(const std::string& username)
{
	pqxx::transaction tx(db_connection::instance());
	auto row = tx.exec1("SELECT COUNT(*) FROM auth.users WHERE username = " + tx.quote(username));
	tx.commit();

	return row[0].as<int>() == 1;
}

// --------------------------------------------------------------------

std::vector<std::string> user_service::get_groups_for_screen(const std::string& screen_name)
{
	pqxx::transaction tx(db_connection::instance());

	std::vector<std::string> groups;

	for (auto const& [name]: tx.stream<std::string>(
		"SELECT g.name as name FROM auth.groups g LEFT JOIN auth.screen_permissions p ON g.id = p.group_id WHERE p.screen_id = (SELECT id FROM screens WHERE name = " + tx.quote(screen_name) + ")"))
	{
		groups.emplace_back(name);
	}

	tx.commit();

	return groups;
}

void user_service::set_groups_for_screen(const std::string& screen_name, std::vector<std::string> groups)
{
	uint32_t screenID;
	{
		try
		{
			pqxx::transaction tx(db_connection::instance());
			screenID = tx.query_value<uint32_t>("SELECT id FROM screens WHERE name = " + tx.quote(screen_name));
			tx.commit();
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			return;
		}
	}

	auto current = get_groups_for_screen(screen_name);

	if (current != groups)
	{
		std::sort(groups.begin(), groups.end());
		std::sort(current.begin(), current.end());

		std::vector<std::string> diff;
		std::set_difference(
			groups.begin(), groups.end(),
			current.begin(), current.end(),
			std::back_inserter(diff));
		
		for (auto& group: diff)
		{
			pqxx::transaction tx(db_connection::instance());
			tx.exec0(
				"INSERT INTO auth.screen_permissions (screen_id, group_id) "
				"VALUES(" + std::to_string(screenID) + ", " +
							"(SELECT id FROM auth.groups WHERE name = " + tx.quote(group) + "))");
			tx.commit();
		}

		diff.clear();

		std::set_difference(
			current.begin(), current.end(),
			groups.begin(), groups.end(),
			std::back_inserter(diff));

		for (auto& group: diff)
		{
			pqxx::transaction tx(db_connection::instance());
			tx.exec0(
				"DELETE FROM auth.screen_permissions WHERE screen_id = " + tx.quote(screenID) +
					" AND group_id = (SELECT id FROM auth.groups WHERE name = " + tx.quote(group) + ")");
			tx.commit();
		}
	}
}

bool user_service::allow_screen_for_user(const std::string& screen, const std::string& user)
{
	pqxx::transaction tx(db_connection::instance());
	auto r = tx.exec_prepared("screen-is-allowed-for-user", screen, user);

	bool allowed = r.empty() == false;
	tx.commit();

	if (not allowed)
		allowed = screen_service::instance().is_owner(screen, user);

	return allowed;
}

std::set<std::string> user_service::allowed_screens_for_user(const std::string& user)
{
	auto tx = db_connection::start_transaction();

	std::set<std::string> result;

	auto r = tx.exec_prepared("allowed-screens-for-user", user);
	for (auto row: r)
		result.insert(row[0].as<std::string>());
	tx.commit();

	return result;
}

// --------------------------------------------------------------------

user user_service::retrieve_user(uint32_t id)
{
	pqxx::transaction tx(db_connection::instance());

	auto row = tx.exec1("SELECT * FROM auth.users WHERE id = " + std::to_string(id));
	tx.commit();

	user user;

	user.username = row.at("username").as<std::string>();
	user.id = id;
	user.email = row.at("email").as<std::string>();
	user.firstname = row.at("first_name").as<std::string>("");
	user.lastname = row.at("last_name").as<std::string>("");
	user.admin = row.at("admin").as<bool>();
	user.active = row.at("active").as<bool>();

	return user;
}

uint32_t user_service::create_user(const user& user)
{
	if (not user.password or user.password->empty() or not isValidPassword(*user.password))
		throw std::runtime_error("Invalid password");

	if (not isValidUsername(user.username))
		throw std::runtime_error("Invalid username");

	if (user_exists(user.username))
		throw std::runtime_error("User already exists");
	
	if (not isValidEmail(user.email))
		throw std::runtime_error("Invalid e-mail address");

	auto pw = user_service::create_password_hash(*user.password);

	pqxx::transaction tx(db_connection::instance());
	auto r = tx.exec1(
		"INSERT INTO auth.users (username, password, email, first_name, last_name, active, admin) "
	 	"VALUES(" + tx.quote(user.username) + ", " +
					tx.quote(pw) + ", " +
					tx.quote(user.email) + ", " +
					tx.quote(user.firstname) + ", " +
					tx.quote(user.lastname) + ", " +
					tx.quote(user.active) + ", " +
					tx.quote(user.admin) + ") "
		"RETURNING id");

	uint32_t id = r[0].as<uint32_t>();
	tx.commit();

	return id;
}

void user_service::update_user(uint32_t id, const user& user)
{
	pqxx::transaction tx(db_connection::instance());

	if (not user.password)
		tx.exec0(
			"UPDATE auth.users SET email = " + tx.quote(user.email) + ", "
							 " first_name = " + tx.quote(user.firstname) + ", " 
							  " last_name = " + tx.quote(user.lastname) + ", "
							  " active = " + tx.quote(user.active) + ", " 
						   " admin = " + tx.quote(user.admin) + 
						 " WHERE id = " + std::to_string(id));
	else if (user.password->empty() or not isValidPassword(*user.password))
		throw std::runtime_error("Invalid password");
	else
	{
		tx.exec0(
			"UPDATE auth.users SET email = " + tx.quote(user.email) + ", " 
							   " password = " + tx.quote(user_service::create_password_hash(*user.password)) + ", "
							 " first_name = " + tx.quote(user.firstname) + ", " 
							  " last_name = " + tx.quote(user.lastname) + ", " 
							  " active = " + tx.quote(user.active) + ", " 
						   " admin = " + tx.quote(user.admin) + 
						 " WHERE id = " + std::to_string(id));
	}

	tx.commit();
}

void user_service::delete_user(uint32_t id)
{
	pqxx::transaction tx(db_connection::instance());
	tx.exec0("DELETE FROM auth.users WHERE id = " + std::to_string(id));
	tx.commit();
}

// --------------------------------------------------------------------

uint32_t user_service::create_group(const group& group)
{
	pqxx::transaction tx1(db_connection::instance());

	auto r = tx1.exec1(
		"INSERT INTO auth.groups (name) "
	 	"VALUES(" + tx1.quote(group.name) + ") "
		"RETURNING id");

	uint32_t group_id = r[0].as<uint32_t>();
	tx1.commit();

	for (auto& m: group.members)
	{
		pqxx::transaction tx(db_connection::instance());
		tx.exec0(
			"INSERT INTO auth.members (group_id, user_id) "
			"VALUES(" + std::to_string(group_id) + ", " +
						"(SELECT id FROM auth.users WHERE username = " + tx.quote(m) + "))");
		tx.commit();
	}

	return group_id;
}

group user_service::retrieve_group(uint32_t id)
{
	pqxx::transaction tx(db_connection::instance());

	auto row = tx.exec1("SELECT * FROM auth.groups WHERE id = " + std::to_string(id));
	tx.commit();

	group group;

	group.name = row.at("name").as<std::string>();
	group.id = id;

	pqxx::transaction tx2(db_connection::instance());
	for (const auto& [member]: tx2.stream<std::string>(
		"SELECT u.username FROM auth.members m JOIN auth.users u ON m.user_id = u.id WHERE m.group_id = " + std::to_string(id)))
	{
		group.members.push_back(member);
	}

	return group;
}

void user_service::update_group(uint32_t id, group group)
{
	auto current = retrieve_group(id);

	if (current.name != group.name)
	{
		pqxx::transaction tx(db_connection::instance());
		tx.exec0("UPDATE auth.groups SET name = " + tx.quote(group.name) + " WHERE id = " + std::to_string(id));
		tx.commit();
	}

	if (current.members != group.members)
	{
		std::sort(group.members.begin(), group.members.end());
		std::sort(current.members.begin(), current.members.end());

		std::vector<std::string> diff;
		std::set_difference(
			group.members.begin(), group.members.end(),
			current.members.begin(), current.members.end(),
			std::back_inserter(diff));
		
		for (auto& member: diff)
		{
			pqxx::transaction tx(db_connection::instance());
			tx.exec0(
				"INSERT INTO auth.members (group_id, user_id) "
				"VALUES(" + std::to_string(id) + ", " +
							"(SELECT id FROM auth.users WHERE username = " + tx.quote(member) + "))");
			tx.commit();
		}

		diff.clear();

		std::set_difference(
			current.members.begin(), current.members.end(),
			group.members.begin(), group.members.end(),
			std::back_inserter(diff));

		for (auto& member: diff)
		{
			pqxx::transaction tx(db_connection::instance());
			tx.exec0(
				"DELETE FROM auth.members WHERE group_id = " + tx.quote(id) +
					" AND user_id = (SELECT id FROM auth.users WHERE username = " + tx.quote(member) + ")");
			tx.commit();
		}
	}
}

void user_service::delete_group(uint32_t id)
{
	pqxx::transaction tx(db_connection::instance());
	tx.exec0("DELETE FROM auth.groups WHERE id = " + std::to_string(id));
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
			upper = 1;
		else if (std::islower(ch))
			lower = 1;
		else if (std::isdigit(ch))
			numbers = 1;
		else if (std::ispunct(ch))
			special = 1;
	}

	return password.length() >= 6 and (upper + lower + numbers + special) >= 3;
}

bool user_service::isValidEmail(const std::string& email)
{
	std::regex rx(R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))", std::regex::icase);
	return std::regex_match(email, rx);
}

// --------------------------------------------------------------------

user_admin_html_controller::user_admin_html_controller()
	: zeep::http::html_controller("/admin")
{
	mount("users", &user_admin_html_controller::handle_user_admin);
	mount("groups", &user_admin_html_controller::handle_group_admin);
}

void user_admin_html_controller::handle_user_admin(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	zeep::http::scope sub(scope);

	zeep::json::element users;
	auto u = user_service::instance().get_all_users();
	to_element(users, u);
	sub.put("users", users);

	get_template_processor().create_reply_from_template("admin-users.html", sub, reply);
}

void user_admin_html_controller::handle_group_admin(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	zeep::http::scope sub(scope);

	zeep::json::element users;
	auto u = user_service::instance().get_all_users();
	to_element(users, u);
	sub.put("users", users);

	zeep::json::element groups;
	auto g = user_service::instance().get_all_groups();
	to_element(groups, g);
	sub.put("groups", groups);

	get_template_processor().create_reply_from_template("admin-groups.html", sub, reply);
}

// --------------------------------------------------------------------

user_admin_rest_controller::user_admin_rest_controller()
	: zeep::http::rest_controller("/admin")
{
	map_post_request("user", &user_admin_rest_controller::create_user, "user");
	map_get_request("user/{id}", &user_admin_rest_controller::retrieve_user, "id");
	map_put_request("user/{id}", &user_admin_rest_controller::update_user, "id", "user");
	map_delete_request("user/{id}", &user_admin_rest_controller::delete_user, "id");

	map_post_request("group", &user_admin_rest_controller::create_group, "group");
	map_get_request("group/{id}", &user_admin_rest_controller::retrieve_group, "id");
	map_put_request("group/{id}", &user_admin_rest_controller::update_group, "id", "group");
	map_delete_request("group/{id}", &user_admin_rest_controller::delete_group, "id");
}

uint32_t user_admin_rest_controller::create_user(const user& user)
{
	return user_service::instance().create_user(user);
}

user user_admin_rest_controller::retrieve_user(uint32_t id)
{
	return user_service::instance().retrieve_user(id);
}

void user_admin_rest_controller::update_user(uint32_t id, const user& user)
{
	user_service::instance().update_user(id, user);
}

void user_admin_rest_controller::delete_user(uint32_t id)
{
	user_service::instance().delete_user(id);
}

uint32_t user_admin_rest_controller::create_group(const group& group)
{
	return user_service::instance().create_group(group);
}

group user_admin_rest_controller::retrieve_group(uint32_t id)
{
	return user_service::instance().retrieve_group(id);
}

void user_admin_rest_controller::update_group(uint32_t id, const group& group)
{
	user_service::instance().update_group(id, group);
}

void user_admin_rest_controller::delete_group(uint32_t id)
{
	user_service::instance().delete_group(id);
}

