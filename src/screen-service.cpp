//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "config.hpp"

#include <zeep/crypto.hpp>

#include "user-service.hpp"
#include "screen-service.hpp"
#include "db-connection.hpp"

// --------------------------------------------------------------------

screen_service& screen_service::instance()
{
    static std::unique_ptr<screen_service> sInstance;
    if (not sInstance)
        sInstance.reset(new screen_service());
    return *sInstance;
}

screen_service::screen_service()
{
}

std::vector<screen> screen_service::get_all_screens() const
{
    pqxx::work tx(db_connection::instance());

    std::vector<screen> result;

    const zeep::value_serializer<ScreenType> s;

    for (const auto[name, type, cell_line, description, long_description, ignore, scientist, created ]:
            tx.stream<std::string, std::string, std::string, std::string, std::string, bool, std::string, std::optional<std::string>>(
        R"(SELECT name, screen_type, cell_line, description, long_description, ignored,
            (SELECT username FROM auth.users WHERE id = scientist_id), trim(both '"' from to_json(screen_date)::text) AS created FROM screens)"))
    {
        screen info;

        if (type == "IP")
            info.type = ScreenType::IntracellularPhenotype;
        else if (type == "SL")
            info.type = ScreenType::SyntheticLethal;
        else
            continue;

        info.name = name;
	    info.cell_line = cell_line;
	    info.description = description;
	    info.long_description = long_description;
	    info.ignore = ignore;
	    info.scientist = scientist;

        if (created)
    	    info.created = zeep::value_serializer<boost::posix_time::ptime>::from_string(*created);

        result.push_back(info);
    }

    return result;
}

std::vector<screen> screen_service::get_all_screens_for_type(ScreenType type) const
{
    pqxx::work tx(db_connection::instance());

    std::vector<screen> result;

    std::string sType = type == ScreenType::IntracellularPhenotype ? "PS" : "SL";

    for (const auto[name]: tx.stream<std::string>("SELECT name FROM screens WHERE screen_type = " + tx.quote(sType)))
    {
        result.push_back(screen{name, type});
    }

    return result;
}

std::vector<screen> screen_service::get_all_screens_for_user(const user& user) const
{
    pqxx::work tx(db_connection::instance());

    std::vector<screen> result;

    const zeep::value_serializer<ScreenType> s;

    for (const auto[name, type]: tx.stream<std::string, std::string>("SELECT name, screen_type FROM screens WHERE "))
    {
        result.push_back(screen{name, s.from_string(type)});
    }

    return result;
}

std::vector<screen> screen_service::get_all_screens_for_user_and_type(const user& user, ScreenType type) const
{
    pqxx::work tx(db_connection::instance());

    std::vector<screen> result;

    for (const auto[name]: tx.stream<std::string>("SELECT name FROM screens"))
    {
        result.push_back(screen{name, type});
    }

    return result;
}

