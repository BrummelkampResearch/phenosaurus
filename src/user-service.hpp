//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <pqxx/pqxx>

#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>

// --------------------------------------------------------------------

struct user
{
	std::string username;
	std::string firstname;
	std::string lastname;
	std::string email;
	bool active;
	bool admin;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("username", username)
		   & zeep::name_value_pair("firstname", firstname)
		   & zeep::name_value_pair("lastname", lastname)
		   & zeep::name_value_pair("email", email)
		   & zeep::name_value_pair("active", active)
		   & zeep::name_value_pair("admin", admin);
	}
};

// --------------------------------------------------------------------

class user_service : public zeep::http::user_service
{
  public:

	static user_service& instance();

	/// Validate the authorization, returns the validated user. Throws unauthorized_exception in case of failure
	virtual zeep::http::user_details load_user(const std::string& username) const;

	// create a password hash
	static std::string create_password_hash(const std::string& password);

	// --------------------------------------------------------------------

	bool user_exists(const std::string& username);
	std::vector<user> get_all_users();
	user get_user(const std::string& username);

	void create_user(const std::string& username, const std::string& firstname, const std::string& lastname,
		const std::string& email, const std::string& password, bool active, bool admin);
	void update_user(const std::string& username, const std::string& firstname, const std::string& lastname,
		const std::string& email, const std::string& password, bool active, bool admin);
	void delete_user(const std::string& username);

	static bool isValidUsername(const std::string& name);
	static bool isValidPassword(const std::string& password);
	static bool isValidEmail(const std::string& email);

  private:

	user_service();
};
