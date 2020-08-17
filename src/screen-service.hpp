//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <pqxx/pqxx>

#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/rest-controller.hpp>

#include "screen-data.hpp"

// --------------------------------------------------------------------

struct user;

// --------------------------------------------------------------------

class screen_service
{
  public:
	static void init(const std::string& screen_data_dir);

	static screen_service& instance();

	std::vector<screen> get_all_screens() const;
	std::vector<screen> get_all_screens_for_type(ScreenType type) const;
	std::vector<screen> get_all_screens_for_user(const std::string& user) const;
	std::vector<screen> get_all_screens_for_user_and_type(const std::string& user, ScreenType type) const;

	screen retrieve_screen(const std::string& name);
	void update_screen(const std::string& name, const screen& screen);
	void delete_screen(const std::string& name);

  private:
	screen_service(const std::string& screen_data_dir);

	std::filesystem::path m_screen_data_dir;

	static std::unique_ptr<screen_service> s_instance;
};

// --------------------------------------------------------------------

class screen_admin_html_controller : public zeep::http::html_controller
{
  public:
	screen_admin_html_controller();

	void handle_screen_admin(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
};

// --------------------------------------------------------------------

class screen_admin_rest_controller : public zeep::http::rest_controller
{
  public:
	screen_admin_rest_controller();

	// uint32_t create_screen(const screen& screen);
	screen retrieve_screen(const std::string& name);
	void update_screen(const std::string& name, const screen& screen);
	void delete_screen(const std::string& name);
};
