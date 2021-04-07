//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

#include <random>

#include <zeep/crypto.hpp>

#include <mailio/smtp.hpp>
#include <mailio/message.hpp>

#include "mrsrc.h"

#include "user-service.hpp"
#include "screen-service.hpp"
#include "db-connection.hpp"

// --------------------------------------------------------------------

const int
	kIterations = 30000,
	kKeyLength = 32;

// --------------------------------------------------------------------

std::unique_ptr<user_service> user_service::s_instance;

void user_service::init(const std::string& smtp_server, uint16_t smtp_port, const std::string& smtp_user, const std::string& smtp_password)
{
	s_instance.reset(new user_service(smtp_server, smtp_port, smtp_user, smtp_password));
}

user_service& user_service::instance()
{
	return *s_instance;
}

zeep::http::user_details user_service::load_user(const std::string& username) const
{
	zeep::http::user_details result;

	try
	{
		pqxx::transaction tx(db_connection::instance());

		auto r = tx.exec1(
			"SELECT password, admin "
			"FROM public.users "
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
		"SELECT id, username, first_name, last_name, email, active, admin FROM public.users"))
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
			"SELECT g.id, g.name, u.username FROM public.groups g LEFT JOIN public.members m ON g.id = m.group_id LEFT JOIN public.users u ON m.user_id = u.id ORDER BY g.name"))
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
	auto row = tx.exec1("SELECT COUNT(*) FROM public.users WHERE username = " + tx.quote(username));
	tx.commit();

	return row[0].as<int>() == 1;
}

// --------------------------------------------------------------------

user user_service::retrieve_user(uint32_t id)
{
	pqxx::transaction tx(db_connection::instance());

	auto row = tx.exec1("SELECT * FROM public.users WHERE id = " + std::to_string(id));
	tx.commit();

	user user;

	user.username = row.at("username").as<std::string>();
	user.id = id;
	user.email = row.at("email").as<std::string>();
	user.firstname = row.at("first_name").as<std::string>("");
	user.lastname = row.at("last_name").as<std::string>("");
	user.admin = row.at("admin").as<bool>();
	user.active = row.at("active").as<bool>();

	pqxx::transaction tx2(db_connection::instance());
	for (auto const& [name]: tx2.stream<std::string>(
			"SELECT g.name FROM public.groups g LEFT JOIN public.members m ON g.id = m.group_id WHERE m.user_id = " + std::to_string(user.id)))
	{
		user.groups.push_back(name);
	}
	tx2.commit();

	return user;
}

user user_service::retrieve_user(const std::string& name)
{
	pqxx::transaction tx(db_connection::instance());

	auto row = tx.exec1("SELECT * FROM public.users WHERE username = " + tx.quote(name));
	tx.commit();

	user user;

	user.username = name;
	user.id = row.at("id").as<uint32_t>();
	user.email = row.at("email").as<std::string>();
	user.firstname = row.at("first_name").as<std::string>("");
	user.lastname = row.at("last_name").as<std::string>("");
	user.admin = row.at("admin").as<bool>();
	user.active = row.at("active").as<bool>();

	pqxx::transaction tx2(db_connection::instance());
	for (auto const& [name]: tx2.stream<std::string>(
			"SELECT g.name FROM public.groups g LEFT JOIN public.members m ON g.id = m.group_id WHERE m.user_id = " + std::to_string(user.id)))
	{
		user.groups.push_back(name);
	}
	tx2.commit();

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
		"INSERT INTO public.users (username, password, email, first_name, last_name, active, admin) "
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
			"UPDATE public.users SET email = " + tx.quote(user.email) + ", "
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
			"UPDATE public.users SET email = " + tx.quote(user.email) + ", " 
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
	tx.exec0("DELETE FROM public.users WHERE id = " + std::to_string(id));
	tx.commit();
}

// --------------------------------------------------------------------

uint32_t user_service::create_group(const group& group)
{
	pqxx::transaction tx1(db_connection::instance());

	auto r = tx1.exec1(
		"INSERT INTO public.groups (name) "
	 	"VALUES(" + tx1.quote(group.name) + ") "
		"RETURNING id");

	uint32_t group_id = r[0].as<uint32_t>();
	tx1.commit();

	for (auto& m: group.members)
	{
		pqxx::transaction tx(db_connection::instance());
		tx.exec0(
			"INSERT INTO public.members (group_id, user_id) "
			"VALUES(" + std::to_string(group_id) + ", " +
						"(SELECT id FROM public.users WHERE username = " + tx.quote(m) + "))");
		tx.commit();
	}

	return group_id;
}

group user_service::retrieve_group(uint32_t id)
{
	pqxx::transaction tx(db_connection::instance());

	auto row = tx.exec1("SELECT * FROM public.groups WHERE id = " + std::to_string(id));
	tx.commit();

	group group;

	group.name = row.at("name").as<std::string>();
	group.id = id;

	pqxx::transaction tx2(db_connection::instance());
	for (const auto& [member]: tx2.stream<std::string>(
		"SELECT u.username FROM public.members m JOIN public.users u ON m.user_id = u.id WHERE m.group_id = " + std::to_string(id)))
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
		tx.exec0("UPDATE public.groups SET name = " + tx.quote(group.name) + " WHERE id = " + std::to_string(id));
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
				"INSERT INTO public.members (group_id, user_id) "
				"VALUES(" + std::to_string(id) + ", " +
							"(SELECT id FROM public.users WHERE username = " + tx.quote(member) + "))");
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
				"DELETE FROM public.members WHERE group_id = " + tx.quote(id) +
					" AND user_id = (SELECT id FROM public.users WHERE username = " + tx.quote(member) + ")");
			tx.commit();
		}
	}
}

void user_service::delete_group(uint32_t id)
{
	pqxx::transaction tx(db_connection::instance());
	tx.exec0("DELETE FROM public.groups WHERE id = " + std::to_string(id));
	tx.commit();
}

// --------------------------------------------------------------------

bool user_service::isValidUsername(const std::string& name)
{
	std::regex rx("^[-a-z0-9_.]{4,30}$", std::regex::icase);
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

std::string user_service::generate_password()
{
	const bool
		includeDigits = true,
		includeSymbols = true,
		includeCapitals = true,
		noAmbiguous = true;
	const int length = 10;

	std::random_device rng;

	std::string result;

	std::set<std::string> kAmbiguous{ "B", "8", "G", "6", "I", "1", "l", "0", "O", "Q", "D", "S", "5", "Z", "2" };

	std::vector<std::string> vowels{ "a", "ae", "ah", "ai", "e", "ee", "ei", "i", "ie", "o", "oh", "oo", "u" };
	std::vector<std::string> consonants{ "b", "c", "ch", "d", "f", "g", "gh", "h", "j", "k", "l", "m", "n", "ng", "p", "ph", "qu", "r", "s", "sh", "t", "th", "v", "w", "x", "y", "z" };

	bool vowel = rng();
	bool wasVowel = false, hasDigits = false, hasSymbols = false, hasCapitals = false;

	for (;;)
	{
		if (result.length() >= length)
		{
			if (result.length() > length or
					includeDigits != hasDigits or
					includeSymbols != hasSymbols or
					includeCapitals != hasCapitals) {
				result.clear();
				hasDigits = hasSymbols = hasCapitals = false;
				continue;
			}

			break;
		}

		std::string s;
		if (vowel)
		{
			do
				s = vowels[rng() % vowels.size()];
			while (wasVowel and s.length() > 1);
		}
		else
			s = consonants[rng() % consonants.size()];

		if (s.length() + result.length() > length)
			continue;

		if (noAmbiguous and kAmbiguous.count(s))
			continue;

		if (includeCapitals and (result.length() == s.length() or vowel == false) and (rng() % 10) < 2)
		{
			for (auto& ch: s)
				ch = std::toupper(ch);
			hasCapitals = true;
		}
		result += s;

		if (vowel and (wasVowel or s.length() > 1 or (rng() % 10) > 3))
		{
			vowel = false;
			wasVowel = true;
		}
		else
		{
			wasVowel = vowel;
			vowel = true;
		}

		if (hasDigits == false and includeDigits and (rng() % 10) < 3)
		{
			std::string ch;
			do ch = (rng() % 10) + '0';
			while (noAmbiguous and kAmbiguous.count(ch));

			result += ch;
			hasDigits = true;
		}
		else if (hasSymbols == false and includeSymbols and (rng() % 10) < 2)
		{
			std::vector<char> kSymbols
					{
							'!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+',
							',', '-', '.', '/', ':', ';', '<', '=', '>', '?', '@',
							'[', '\\', ']', '^', '_', '`', '{', '|', '}', '~',
					};

			result += kSymbols[rng() % kSymbols.size()];
			hasSymbols = true;
		}
	}

	return result;
}

void user_service::send_new_password_for(const std::string& email)
{
	std::cerr << "Request reset password for " << email << std::endl;
	std::string newPassword = generate_password();
	std::string newPasswordHash = create_password_hash(newPassword);

	std::cerr << "Reset password for " << email << " to " << newPasswordHash << std::endl;

	// --------------------------------------------------------------------
	
	pqxx::transaction tx(db_connection::instance());
	tx.exec0("UPDATE public.users SET password = " + tx.quote(newPasswordHash) + " WHERE email = " + tx.quote(email));

	// --------------------------------------------------------------------

	mailio::message msg;
	// msg.add_from(mailio::mail_address("Phenosaurus User Management Service", "phenosaurus@nki.nl"));
	msg.add_from(mailio::mail_address("Phenosaurus User Management Service", "maarten@hekkelman.com"));
	msg.add_recipient(mailio::mail_address("Phenosaurus user", email));
	msg.subject("New password for Phenosaurus");

	std::ostringstream content;

	mrsrc::istream is("reset-password-mail.txt");

	std::string line;
	while (std::getline(is, line))
	{
		auto i = line.find("^1");
		if (i != std::string::npos)
			line.replace(i, 2, newPassword);
		content << line << std::endl;
	}

	msg.content(content.str());
	msg.content_type(mailio::mime::media_type_t::TEXT, "plain", "utf-8");
	msg.content_transfer_encoding(mailio::mime::content_transfer_encoding_t::BINARY);

	if (m_smtp_port == 25)
	{
		mailio::smtp conn(m_smtp_server, m_smtp_port);
		conn.authenticate(m_smtp_user, m_smtp_password, m_smtp_user.empty() ? mailio::smtp::auth_method_t::NONE : mailio::smtp::auth_method_t::LOGIN);
		conn.submit(msg);	
	}
	else if (m_smtp_port == 465)
	{
		mailio::smtps conn(m_smtp_server, m_smtp_port);
		conn.authenticate(m_smtp_user, m_smtp_password, m_smtp_user.empty() ? mailio::smtps::auth_method_t::NONE : mailio::smtps::auth_method_t::LOGIN);
		conn.submit(msg);	
	}
	else if (m_smtp_port == 587 and not m_smtp_user.empty())
	{
		mailio::smtps conn(m_smtp_server, m_smtp_port);
		conn.authenticate(m_smtp_user, m_smtp_password, mailio::smtps::auth_method_t::START_TLS);
		conn.submit(msg);	
	}
	else
		throw std::runtime_error("Unable to send message, smtp configuration error");

	// --------------------------------------------------------------------
	// Sending the new password succeeded

	tx.commit();
}

// --------------------------------------------------------------------

user_service_html_controller::user_service_html_controller()
{
	mount("reset-password", &user_service_html_controller::handle_reset_password);
}

void user_service_html_controller::handle_reset_password(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	if (request.get_method() == "GET")
		get_template_processor().create_reply_from_template("reset-password", scope, reply);
	else
	{
		try
		{
			auto email = request.get_parameter("email");

			user_service::instance().send_new_password_for(email);
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
		}

		reply = zeep::http::reply::redirect("login");
	}
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

