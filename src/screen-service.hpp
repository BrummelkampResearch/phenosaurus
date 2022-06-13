//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <zeep/http/html-controller.hpp>
#include <zeep/http/rest-controller.hpp>
#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>

#include "screen-data.hpp"

// --------------------------------------------------------------------

class screen_data_cache
{
  public:
	screen_data_cache(ScreenType type, const std::string &assembly, short trim_length, const std::string &transcript_selection,
		Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd);

	virtual ~screen_data_cache();

	bool is_for(ScreenType type, const std::string &assembly, short trim_length, const std::string &transcript_selection,
		Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd) const
	{
		return m_type == type and m_assembly == assembly and m_trim_length == trim_length and
			   m_transcript_selection == transcript_selection and
		       m_mode == mode and m_cutOverlap == cutOverlap and m_geneStart == geneStart and m_geneEnd == geneEnd;
	}

	bool is_up_to_date() const;

	virtual std::filesystem::path get_cache_file_path(const std::string &screen_name) const = 0;
	virtual bool contains_data_for_screen(const std::string &screen) const = 0;

  protected:
	struct cached_screen
	{
		std::string name;
		bool filled = false;
		bool ignore = false;
		uint8_t file_count = 0;
		uint32_t data_offset = 0;
		uint32_t replicate_offset = 0;
	};

	ScreenType m_type;
	std::string m_assembly;
	short m_trim_length;
	std::string m_transcript_selection;
	Mode m_mode;
	bool m_cutOverlap;
	std::string m_geneStart;
	std::string m_geneEnd;
	std::vector<Transcript> m_transcripts;
	std::vector<cached_screen> m_screens;
};

struct ip_data_point
{
	std::string gene;
	float pv;
	float fcpv;
	float mi;
	int low;
	int high;
	std::optional<int> rank;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
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
	int colour;
	size_t count;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("colour", colour)
		   & zeep::make_nvp("count", count);
	}
};

struct ip_gene_finder_data_point
{
	std::string screen;
	float mi;
	float fcpv;
	int insertions;
	int replicate;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
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

	bool operator<(const similar_data_point &h) const { return distance < h.distance; }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("distance", distance)
		   & zeep::make_nvp("zscore", zscore)
		   & zeep::make_nvp("anti", anti);
	}
};

struct cluster
{
	std::vector<std::string> genes;
	float variance;

	bool operator<(const cluster &c) const { return genes.size() > c.genes.size(); }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar & zeep::make_nvp("genes", genes)
		   & zeep::make_nvp("variance", variance);
	}
};

// --------------------------------------------------------------------

class ip_screen_data_cache : public screen_data_cache
{
  public:
	ip_screen_data_cache(ScreenType type, const std::string &assembly, short trim_length, const std::string &transcript_selection,
		Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd,
		Direction direction);

	~ip_screen_data_cache();

	bool is_for(ScreenType type, std::string &assembly, short trim_length, const std::string &transcript_selection, Mode mode,
		bool cutOverlap, const std::string &geneStart, const std::string &geneEnd,
		Direction direction) const
	{
		return screen_data_cache::is_for(type, assembly, trim_length, transcript_selection, mode, cutOverlap, geneStart, geneEnd) and m_direction == direction;
	}

	bool contains_data_for_screen(const std::string &screen) const override
	{
		auto si = std::find_if(m_screens.begin(), m_screens.end(), [screen](auto &si)
			{ return si.name == screen; });
		return si != m_screens.end();
	}

	std::vector<ip_data_point> data_points(const std::string &screen);
	std::vector<gene_uniqueness> uniqueness(const std::string &screen, float pvCutOff, bool singlesided);

	std::vector<ip_gene_finder_data_point> find_gene(const std::string &gene, const std::set<std::string> &allowedScreens);
	std::vector<similar_data_point> find_similar(const std::string &gene, float pvCutOff, float zscoreCutOff);
	std::vector<cluster> find_clusters(float pvCutOff, size_t minPts, float eps, size_t NNs);

	virtual std::filesystem::path get_cache_file_path(const std::string &screen_name) const override;

  private:
	struct data_point
	{
		float pv;
		float fcpv;
		float mi;
		uint32_t low;
		uint32_t high;
	};

	// size_t index(size_t screen_nr, size_t transcript) const;

	Direction m_direction;
	data_point *m_data;
};

// --------------------------------------------------------------------

struct sl_data_replicate
{
	float binom_fdr;
	float ref_pv[4];
	uint32_t sense, antisense;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar & zeep::make_nvp("binom_fdr", binom_fdr)
		   & zeep::make_nvp("ref_pv", ref_pv)
		   & zeep::make_nvp("sense", sense)
		   & zeep::make_nvp("antisense", antisense);
	}
};

struct sl_data_point
{
	std::string gene;
	float oddsRatio;
	float controlBinom;
	float controlSenseRatio;
	float senseRatio;
	bool consistent;
	std::vector<sl_data_replicate> replicates;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar & zeep::make_nvp("gene", gene)
		   & zeep::make_nvp("odds_ratio", oddsRatio)
		   & zeep::make_nvp("sense_ratio", senseRatio)
		   & zeep::make_nvp("control_binom", controlBinom)
		   & zeep::make_nvp("control_sense_ratio", controlSenseRatio)
		   & zeep::make_nvp("consistent", consistent)
		   & zeep::make_nvp("replicate", replicates);
	}
};

// --------------------------------------------------------------------

struct sl_gene_finder_data_point
{
	std::string screen;
	float senseRatio;
	std::vector<float> senseRatioPerReplicate;
	bool consistent;
	float oddsRatio;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long)
	{
		ar & zeep::make_nvp("screen", screen)
		   & zeep::make_nvp("sense_ratio", senseRatio)
		   & zeep::make_nvp("sense_ratio_list", senseRatioPerReplicate)
		   & zeep::make_nvp("odds_ratio", oddsRatio)
		   & zeep::make_nvp("consistent", consistent);
	}
};

// --------------------------------------------------------------------

class sl_screen_data_cache : public screen_data_cache
{
  public:
	sl_screen_data_cache(const std::string &assembly, short trim_length, const std::string &transcript_selection,
		Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd);

	~sl_screen_data_cache();

	bool contains_data_for_screen(const std::string &screen) const override
	{
		auto si = std::find_if(m_screens.begin(), m_screens.end(), [screen](auto &si)
			{ return si.name == screen; });
		return si != m_screens.end();
	}

	std::vector<sl_data_point> data_points(const std::string &screen);
	// std::vector<gene_uniqueness> uniqueness(const std::string& screen, float pvCutOff);

	std::vector<sl_gene_finder_data_point> find_gene(const std::string &gene, const std::set<std::string> &allowedScreens);
	// std::vector<similar_data_point> find_similar(const std::string& gene, float pvCutOff, float zscoreCutOff);
	// std::vector<cluster> find_clusters(float pvCutOff, size_t minPts, float eps, size_t NNs);

	virtual std::filesystem::path get_cache_file_path(const std::string &screen_name) const override;

  private:

	struct data_point
	{
		float odds_ratio;
		float control_binom;
	};

	struct data_point_replicate
	{
		float binom_fdr;
		uint32_t sense, antisense;
		float pv[4];
	};

	data_point *m_data;
	data_point_replicate *m_replicate_data;
};

// --------------------------------------------------------------------

struct user;

class screen_service
{
  public:
	static void init(const std::string &screen_data_dir, const std::string &transcripts_dir);

	static screen_service &instance();

	const std::filesystem::path &get_screen_data_dir() const { return m_screen_data_dir; }
	const std::filesystem::path &get_transcripts_dir() const { return m_transcripts_dir; }

	std::vector<screen_info> get_all_screens() const;
	std::vector<screen_info> get_all_screens_for_type(ScreenType type) const;
	std::vector<screen_info> get_all_screens_for_user(const std::string &user) const;
	std::vector<screen_info> get_all_screens_for_user_and_type(const std::string &user, ScreenType type) const;
	std::vector<screen_info> get_all_public_screens_for_type(ScreenType type) const;
	
	// return list of allowed screens based on user info (name, groups)
	std::set<std::string> get_allowed_screens_for_user(const user &user) const;

	bool exists(const std::string &name) const noexcept;
	static bool is_valid_name(const std::string &name);

	screen_info retrieve_screen(const std::string &name) const;
	bool is_owner(const std::string &name, const std::string &username) const;
	bool is_allowed(const std::string &screenname, const std::string &username) const;

	std::unique_ptr<ScreenData> create_screen(const screen_info &screen);
	void update_screen(const std::string &name, const screen_info &screen);
	void delete_screen(const std::string &name);

	template<typename ScreenDataType>
	std::unique_ptr<ScreenDataType> load_screen(const std::string &screen)
	{
		return std::make_unique<ScreenDataType>(m_screen_data_dir / screen);
	}

	std::shared_ptr<ip_screen_data_cache> get_screen_data(ScreenType type,
		const std::string &assembly, short trim_length, const std::string &transcript_selection,
		Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd,
		Direction direction);

	std::shared_ptr<sl_screen_data_cache> get_screen_data(
		const std::string &assembly, short trim_length, const std::string &transcript_selection,
		Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd);

	// std::vector<ip_data_point> get_data_points(const ScreenType type, const std::string &screen, const std::string &assembly, short trim_length, const std::string &transcript_selection,
	// 	Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd, Direction direction);

	// std::vector<sl_data_point> get_data_points(const std::string &screen, std::string control, const std::string &assembly, short trim_length, const std::string &transcript_selection,
	// 	Mode mode, bool cutOverlap, const std::string &geneStart, const std::string &geneEnd);

	void screen_mapped(const std::unique_ptr<ScreenData> &screen);

	// configurable transcripts
	std::vector<std::string> get_all_transcripts() const;

  private:
	screen_service(const std::string &screen_data_dir, const std::string &transcripts_dir);

	std::filesystem::path m_screen_data_dir, m_transcripts_dir;
	std::mutex m_mutex;
	std::list<std::shared_ptr<ip_screen_data_cache>> m_ip_data_cache;
	std::list<std::shared_ptr<sl_screen_data_cache>> m_sl_data_cache;

	static std::unique_ptr<screen_service> s_instance;
};

template<>
inline std::unique_ptr<ScreenData> screen_service::load_screen<ScreenData>(const std::string &screen)
{
	return ScreenData::load(m_screen_data_dir / screen);
}

// --------------------------------------------------------------------

class screen_html_controller : public zeep::http::html_controller
{
  public:
	screen_html_controller();

	void handle_screen_user(const zeep::http::request &request, const zeep::http::scope &scope, zeep::http::reply &reply);
	void handle_create_screen_user(const zeep::http::request &request, const zeep::http::scope &scope, zeep::http::reply &reply);
	void handle_edit_screen_user(const zeep::http::request &request, const zeep::http::scope &scope, zeep::http::reply &reply);
	void handle_screen_table(const zeep::http::request &request, const zeep::http::scope &scope, zeep::http::reply &reply);
};

// --------------------------------------------------------------------

class screen_rest_controller : public zeep::http::rest_controller
{
  public:
	screen_rest_controller();

	std::string create_screen(const screen_info &screen);
	screen_info retrieve_screen(const std::string &name);
	void update_screen(const std::string &name, const screen_info &screen);
	void delete_screen(const std::string &name);

	bool validateFastQFile(const std::string &filename);
	bool validateScreenName(const std::string &name);

	void map_screen(const std::string &screen, const std::string &assembly);
};
