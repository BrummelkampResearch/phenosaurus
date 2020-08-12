//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <pqxx/pqxx>

#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>

// --------------------------------------------------------------------

struct user;

// --------------------------------------------------------------------

enum class ScreenType
{
	Unspecified,

	IntracellularPhenotype,
	SyntheticLethal,

	// abreviated synonyms
	IP = IntracellularPhenotype,
	SL = SyntheticLethal
};

// --------------------------------------------------------------------

struct screen
{
	std::string name;
	ScreenType type;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::name_value_pair("name", name)
					 & zeep::name_value_pair("type", type);
	}
};

// --------------------------------------------------------------------

class screen_service
{
  public:
	static screen_service& instance();

	std::vector<screen> get_all_screens() const;
	std::vector<screen> get_all_screens_for_type(ScreenType type) const;
	std::vector<screen> get_all_screens_for_user(const user& user) const;
	std::vector<screen> get_all_screens_for_user_and_type(const user& user, ScreenType type) const;

  private:
	screen_service();
};
