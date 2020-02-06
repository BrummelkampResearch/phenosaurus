// copyright 2020 M.L. Hekkelman, NKI/AVL

#include <iostream>

#include <zeep/http/webapp.hpp>
#include <zeep/rest/controller.hpp>

#include "screenserver.hpp"

namespace fs = std::filesystem;
namespace zh = zeep::http;

// --------------------------------------------------------------------

class ScreenRestController : public zh::rest_controller
{
  public:
	ScreenRestController()
		: zh::rest_controller("ajax")
	{

	}
};

// --------------------------------------------------------------------

class ScreenServer : public zh::webapp
{
  public:
	ScreenServer(const fs::path& screenDir)
		: zh::webapp(fs::current_path() / "docroot")
		, mRestController(new ScreenRestController())
		, mScreenDir(screenDir)
	{
		register_tag_processor<zh::tag_processor_v2>("http://www.hekkelman.com/libzeep/m2");

		add_controller(mRestController);
	
		mount("", &ScreenServer::fishtail);

		mount("css", &ScreenServer::handle_file);
		mount("scripts", &ScreenServer::handle_file);
		mount("fonts", &ScreenServer::handle_file);
	}

	void fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply);

  private:
	ScreenRestController* mRestController;
	fs::path mScreenDir;
};

void ScreenServer::fishtail(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	sub.put("page", "fishtail");

	create_reply_from_template("fishtail.html", sub, reply);
}

// --------------------------------------------------------------------

zh::server* createServer(const fs::path& screenDir)
{
	return new ScreenServer(screenDir);
}
