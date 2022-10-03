/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
	user retrieve_user_by_email(const std::string &email);
	void update_user(uint32_t id, const user& user);
	void delete_user(uint32_t id);

	uint32_t create_group(const group& group);
	group retrieve_group(uint32_t id);
	void update_group(uint32_t id, group group);
	void delete_group(uint32_t id);

	static bool isValidUsername(const std::string& name);
	static bool isValidPassword(const std::string& password);
	static bool isValidEmail(const std::string& email);

	bool isExistingEmail(const std::string &email);

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
