
#include <string>
#include <iostream>


#include "application.h"
#include "boost/program_options.hpp"
#include "boost/leaf.hpp"

int main(int argc, char* argv[])
{
	// leaving this here JustinCase: https://www.boost.org/doc/libs/1_58_0/libs/log/doc/html/log/rationale/why_crash_on_term.html 
	//boost::filesystem::path::imbue(std::locale("C"));

	bool running = true;
	Application app;
	return boost::leaf::try_handle_all(
		[&]() -> boost::leaf::result<int>
		{
			BOOST_LEAF_CHECK(app.Init(argc, argv, "ImIRC Client"));
			BOOST_LEAF_CHECK(app.AppLoop(running));
			exit(0);
			return 0;
		},
		[](Application::AppErrc e)
		{
			if (e == Application::AppErrc::CleanExit)
			{
				BOOST_LOG_TRIVIAL(info) << "Clean Exit";
				return 0;
			}
			else
			{
				BOOST_LOG_TRIVIAL(error) << "exit with error";
				return -1;
			}
		},
		[](boost::program_options::error& e)
		{
			BOOST_LOG_TRIVIAL(error) << e.what();
			return -1;
		},
		[](boost::leaf::error_info const& unmatched)
		{
			// catch unmatched
			BOOST_LOG_TRIVIAL(error) <<
				"Unknown failure detected" << std::endl <<
				"Cryptic diagnostic information follows" << std::endl <<
				unmatched;
			return -1;
		}
		);
	
}