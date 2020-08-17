//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

#include <filesystem>

#include <zeep/crypto.hpp>

#include "user-service.hpp"
#include "screen-service.hpp"
#include "db-connection.hpp"

namespace fs = std::filesystem;

// --------------------------------------------------------------------

std::unique_ptr<screen_service> screen_service::s_instance;

void screen_service::init(const std::string& screen_data_dir)
{
	assert(not s_instance);
	s_instance.reset(new screen_service(screen_data_dir));
}

screen_service& screen_service::instance()
{
	assert(s_instance);
	return *s_instance;
}

screen_service::screen_service(const std::string& screen_data_dir)
	: m_screen_data_dir(screen_data_dir)
{
	if (not fs::exists(m_screen_data_dir))
		throw std::runtime_error("Screen data directory " + screen_data_dir + " does not exist");
}

std::vector<screen> screen_service::get_all_screens() const
{
	std::vector<screen> result;

	for (auto i = fs::directory_iterator(m_screen_data_dir); i != fs::directory_iterator(); ++i)
	{
		if (not i->is_directory())
			continue;

		std::ifstream manifest(i->path() / "manifest.json");
		if (not manifest.is_open())
			continue;

		zeep::json::element info;
		zeep::json::parse_json(manifest, info);

		screen screen;
		zeep::json::from_element(info, screen);
		
		result.push_back(screen);
	}

	return result;
}

std::vector<screen> screen_service::get_all_screens_for_type(ScreenType type) const
{
	auto screens = get_all_screens();
	screens.erase(std::remove_if(screens.begin(), screens.end(), [type](auto& s) { return s.type != type; }), screens.end());
	return screens;
}

std::vector<screen> screen_service::get_all_screens_for_user(const std::string& user) const
{
	auto screens = get_all_screens();

	auto& user_service = user_service::instance();

	screens.erase(std::remove_if(screens.begin(), screens.end(), [user,&user_service](auto& s) {
		return not user_service.allow_screen_for_user(s.name, user);
	}), screens.end());
	return screens;
}

std::vector<screen> screen_service::get_all_screens_for_user_and_type(const std::string& user, ScreenType type) const
{
	auto screens = get_all_screens_for_user(user);
	screens.erase(std::remove_if(screens.begin(), screens.end(), [type](auto& s) { return s.type != type; }), screens.end());
	return screens;
}

screen screen_service::retrieve_screen(const std::string& name)
{
	std::ifstream manifest(m_screen_data_dir / name / "manifest.json");

	if (not manifest.is_open())
		throw std::runtime_error("No such screen?: " + name);
	
	zeep::json::element info;
	zeep::json::parse_json(manifest, info);

	screen screen;
	zeep::json::from_element(info, screen);

	for (auto& group: user_service::instance().get_groups_for_screen(screen.name))
		screen.groups.push_back(group);

	return screen;
}

void screen_service::update_screen(const std::string& name, const screen& screen)
{
	std::ofstream manifest(m_screen_data_dir / name / "manifest.json");
	if (not manifest.is_open())
		throw std::runtime_error("Could not create manifest file in " + m_screen_data_dir.string());

	zeep::json::element jInfo;
	zeep::json::to_element(jInfo, screen);
	manifest << jInfo;
	manifest.close();

	user_service::instance().set_groups_for_screen(name, screen.groups);
}

void screen_service::delete_screen(const std::string& name)
{
	fs::remove_all(m_screen_data_dir / name);
}

// --------------------------------------------------------------------

screen_admin_html_controller::screen_admin_html_controller()
	: zeep::http::html_controller("/admin")
{
	mount("screens", &screen_admin_html_controller::handle_screen_admin);
}

void screen_admin_html_controller::handle_screen_admin(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply)
{
	zeep::http::scope sub(scope);

	zeep::json::element screens;
	auto s = screen_service::instance().get_all_screens();
	to_element(screens, s);
	sub.put("screens", screens);

	zeep::json::element users;
	auto u = user_service::instance().get_all_users();
	to_element(users, u);
	sub.put("users", users);

	zeep::json::element groups;
	auto g = user_service::instance().get_all_groups();
	to_element(groups, g);
	sub.put("groups", groups);

	get_template_processor().create_reply_from_template("admin-screens.html", sub, reply);
}

// --------------------------------------------------------------------

screen_admin_rest_controller::screen_admin_rest_controller()
	: zeep::http::rest_controller("/admin")
{
	// map_post_request("screen", &screen_admin_rest_controller::create_screen, "screen");
	map_get_request("screen/{id}", &screen_admin_rest_controller::retrieve_screen, "id");
	map_put_request("screen/{id}", &screen_admin_rest_controller::update_screen, "id", "screen");
	map_delete_request("screen/{id}", &screen_admin_rest_controller::delete_screen, "id");
}

// uint32_t screen_admin_rest_controller::create_screen(const screen& screen)
// {
// 	return screen_service::instance().create_screen(screen);
// }

screen screen_admin_rest_controller::retrieve_screen(const std::string& name)
{
	return screen_service::instance().retrieve_screen(name);
}

void screen_admin_rest_controller::update_screen(const std::string& name, const screen& screen)
{
	screen_service::instance().update_screen(name, screen);
}

void screen_admin_rest_controller::delete_screen(const std::string& name)
{
	screen_service::instance().delete_screen(name);
}
