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

class screen_data_cache
{
  public:
	screen_data_cache(const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);
	virtual ~screen_data_cache();

  protected:


	std::vector<Transcript> m_transcripts;

};

struct ip_data_point
{
	int gene_id;
	std::string gene_name;
	float pv;
	float fcpv;
	float mi;
	int low;
	int high;
	int unique_color;
	int rank;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("geneID", gene_id)
		   & zeep::make_nvp("geneName", gene_name)
		   & zeep::make_nvp("pv", pv)
		   & zeep::make_nvp("fcpv", fcpv)
		   & zeep::make_nvp("mi", mi)
		   & zeep::make_nvp("low", low)
		   & zeep::make_nvp("high", high)
		   & zeep::make_nvp("rank", rank)
		   & zeep::make_nvp("uniqueColor", unique_color);
	}
};

class ip_screen_data_cache
{
  public:
	ip_screen_data_cache(const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	std::vector<ip_data_point> data_points(const std::string& screen);

};

// --------------------------------------------------------------------

class screen_service
{
  public:
	static void init(const std::string& screen_data_dir);

	static screen_service& instance();

	std::vector<screen_info> get_all_screens() const;
	std::vector<screen_info> get_all_screens_for_type(ScreenType type) const;
	std::vector<screen_info> get_all_screens_for_user(const std::string& user) const;
	std::vector<screen_info> get_all_screens_for_user_and_type(const std::string& user, ScreenType type) const;

	screen_info retrieve_screen(const std::string& name);
	void update_screen(const std::string& name, const screen_info& screen);
	void delete_screen(const std::string& name);

	std::shared_ptr<ip_screen_data_cache> get_ip_screen_data(const std::string& assembly,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

  private:
	screen_service(const std::string& screen_data_dir);

	std::filesystem::path m_screen_data_dir;
	std::mutex m_mutex;
	std::list<std::shared_ptr<ip_screen_data_cache>> m_ip_data_cache;

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
	screen_info retrieve_screen(const std::string& name);
	void update_screen(const std::string& name, const screen_info& screen);
	void delete_screen(const std::string& name);
};

// --------------------------------------------------------------------

class screen_user_html_controller : public zeep::http::html_controller
{
  public:
	screen_user_html_controller();

	void handle_screen_user(const zeep::http::request& request, const zeep::http::scope& scope, zeep::http::reply& reply);
};

// --------------------------------------------------------------------

class screen_user_rest_controller : public zeep::http::rest_controller
{
  public:
	screen_user_rest_controller();

	// uint32_t create_screen(const screen& screen);
	screen_info retrieve_screen(const std::string& name);
	void update_screen(const std::string& name, const screen_info& screen);
	void delete_screen(const std::string& name);
};
