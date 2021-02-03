//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/rest-controller.hpp>

// --------------------------------------------------------------------

struct group
{
	uint32_t id;
	std::string name;
	std::vector<std::string> members;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("id", id)
		   & zeep::name_value_pair("name", name)
		   & zeep::name_value_pair("members", members);
	}
};

// --------------------------------------------------------------------

struct user
{
	uint32_t id;
	std::string username;
	std::string firstname;
	std::string lastname;
	std::string email;
	std::optional<std::string> password;
	bool active;
	bool admin;
	std::vector<std::string> groups;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("id", id)
		   & zeep::name_value_pair("username", username)
		   & zeep::name_value_pair("firstname", firstname)
		   & zeep::name_value_pair("lastname", lastname)
		   & zeep::name_value_pair("email", email)
		   & zeep::name_value_pair("active", active)
		   & zeep::name_value_pair("password", password)
		   & zeep::name_value_pair("admin", admin)
		   & zeep::name_value_pair("groups", groups);
	}
};

// --------------------------------------------------------------------

class user_service : public zeep::http::user_service
{
  public:

	static void init(const std::string& smtp_server, uint16_t smtp_port, const std::string& smtp_user = "", const std::string& smtp_password = "");

	static user_service& instance();

	/// Validate the authorization, returns the validated user. Throws unauthorized_exception in case of failure
	virtual zeep::http::user_details load_user(const std::string& username) const;

	// create a password hash
	static std::string create_password_hash(const std::string& password);

	// --------------------------------------------------------------------

	bool user_exists(const std::string& username);
	std::vector<user> get_all_users();
	std::vector<group> get_all_groups();

	// --------------------------------------------------------------------
	
	uint32_t create_user(const user& user);
	user retrieve_user(uint32_t id);
	user retrieve_user(const std::string& name);
	void update_user(uint32_t id, const user& user);
	void delete_user(uint32_t id);

	uint32_t create_group(const group& group);
	group retrieve_group(uint32_t id);
	void update_group(uint32_t id, group group);
	void delete_group(uint32_t id);

	static bool isValidUsername(const std::string& name);
	static bool isValidPassword(const std::string& password);
	static bool isValidEmail(const std::string& email);

	/// Update the password to \a password for the user with e-mail address in \a email
	void send_new_password_for(const std::string& email);

	static std::string generate_password();

  private:

	user_service(const std::string& smtp_server, uint16_t smtp_port, const std::string& smtp_user, const std::string& smtp_password)
		: m_smtp_server(smtp_server), m_smtp_port(smtp_port), m_smtp_user(smtp_user), m_smtp_password(smtp_password) {}

	void fill_allowed_screens_cache();

	std::mutex m_mutex;
	std::map<std::string,std::set<std::string>> m_allowed_screens_per_user_cache;

	// for sending out new passwords
	std::string m_smtp_server;
	unsigned short m_smtp_port;
	std::string m_smtp_user;
	std::string m_smtp_password;

	static std::unique_ptr<user_service> s_instance;
};

// --------------------------------------------------------------------

class user_service_html_controller : public zeep::http::html_controller
{
  public:
	user_service_html_controller();

	void handle_reset_password(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
};

// --------------------------------------------------------------------

class user_admin_html_controller : public zeep::http::html_controller
{
  public:
	user_admin_html_controller();

	void handle_user_admin(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
	void handle_group_admin(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
};

// --------------------------------------------------------------------

class user_admin_rest_controller : public zeep::http::rest_controller
{
  public:
	user_admin_rest_controller();

	uint32_t create_user(const user& user);
	user retrieve_user(uint32_t id);
	void update_user(uint32_t id, const user& user);
	void delete_user(uint32_t id);

	uint32_t create_group(const group& group);
	group retrieve_group(uint32_t id);
	void update_group(uint32_t id, const group& group);
	void delete_group(uint32_t id);
};
