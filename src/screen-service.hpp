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
	screen_data_cache(const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd);
	virtual ~screen_data_cache();

	bool is_for(const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd) const
	{
		return m_assembly == assembly and m_trim_length == trim_length and
			m_mode == mode and m_cutOverlap == cutOverlap and m_geneStart == geneStart and m_geneEnd == geneEnd;
	}

  protected:

	std::string m_assembly;
	short m_trim_length;
	Mode m_mode;
	bool m_cutOverlap;
	std::string m_geneStart;
	std::string m_geneEnd;
	std::vector<Transcript> m_transcripts;
};

struct ip_data_point
{
	std::string gene;
	float pv;
	float fcpv;
	float mi;
	int low;
	int high;
	int rank;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("pv", pv)
		   & zeep::make_nvp("fcpv", fcpv)
		   & zeep::make_nvp("mi", mi)
		   & zeep::make_nvp("low", low)
		   & zeep::make_nvp("high", high)
		   & zeep::make_nvp("rank", rank);
	}
};

struct gene_uniqueness
{
	std::string gene;
	int color;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("colour", color);
	}
};

struct gene_finder_data_point
{
	std::string screen;
	float mi;
	float fcpv;
	int insertions;
	int replicate;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("screen", screen)
		   & zeep::make_nvp("mi", mi)
		   & zeep::make_nvp("fcpv", fcpv)
		   & zeep::make_nvp("insertions", insertions)
		   & zeep::make_nvp("replicate", replicate);
	}
};

struct similar_data_point
{
	std::string gene;
	float distance;
	float zscore;
	bool anti;

	bool operator<(const similar_data_point& h) const { return distance < h.distance; }

	template<typename Archive>
	void serialize(Archive& ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("distance", distance)
		   & zeep::make_nvp("zscore", zscore)
		   & zeep::make_nvp("anti", anti);
	}
};

class ip_screen_data_cache : public screen_data_cache
{
  public:
	ip_screen_data_cache(const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);
	~ip_screen_data_cache();

	bool is_for(std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction) const
	{
		return screen_data_cache::is_for(assembly, trim_length, mode, cutOverlap, geneStart, geneEnd) and m_direction == direction;
	}

	bool contains_data_for_screen(const std::string& screen) const
	{
		auto si = std::find_if(m_screens.begin(), m_screens.end(), [screen](auto& si) { return si.first == screen; });
		return si != m_screens.end() and si->second;
	}

	std::vector<ip_data_point> data_points(const std::string& screen);
	std::vector<gene_uniqueness> uniqueness(const std::string& screen, float pvCutOff);
	std::vector<gene_finder_data_point> find_gene(const std::string& gene);
	std::vector<similar_data_point> find_similar(const std::string& gene, float pvCutOff, float zscoreCutOff);

  private:

	struct data_point
	{
		float		pv;
		float		fcpv;
		float		mi;
		uint32_t	low;
		uint32_t	high;
	};

	size_t index(size_t screen_nr, size_t transcript) const;

	Direction m_direction;
	data_point* m_data;
	std::vector<std::pair<std::string,bool>> m_screens;
};

// --------------------------------------------------------------------

class screen_service
{
  public:
	static void init(const std::string& screen_data_dir);

	static screen_service& instance();

	const std::filesystem::path& get_screen_data_dir() const		{ return m_screen_data_dir; }

	std::vector<screen_info> get_all_screens() const;
	std::vector<screen_info> get_all_screens_for_type(ScreenType type) const;
	std::vector<screen_info> get_all_screens_for_user(const std::string& user) const;
	std::vector<screen_info> get_all_screens_for_user_and_type(const std::string& user, ScreenType type) const;

	screen_info retrieve_screen(const std::string& name);
	void update_screen(const std::string& name, const screen_info& screen);
	void delete_screen(const std::string& name);

	std::shared_ptr<ip_screen_data_cache> get_ip_screen_data(const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd,
		Direction direction);

	std::vector<ip_data_point> get_ip_data_points(const std::string& screen, const std::string& assembly, short trim_length,
		Mode mode, bool cutOverlap, const std::string& geneStart, const std::string& geneEnd, Direction direction);

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
