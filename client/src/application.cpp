#include "application.h"
#include "util.h"
#include "GLFW/glfw3.h"

// imgui
#include "misc/cpp/imgui_stdlib.h"
// boost
#include "boost/program_options.hpp"
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/algorithm/string.hpp>

// std
#include <filesystem>
#include <iostream>
#include <string>
#include <algorithm>
#include <sstream>
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef IMGUI_ENABLE_FREETYPE
#include "misc/freetype/imgui_freetype.h"
#endif

#include <iostream>

#if _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#include <shellapi.h>
#endif

#define BYTE char

Application::~Application()
{
	for (auto& [id, conn] : m_connections)
	{
		if (conn)
			conn->stop();
	}
	m_connections.clear();

	if (m_dummyWork)
		m_dummyWork.reset();
	m_ioContext.stop();
	for (auto& t : m_workerThreads)
	{
		if (t.joinable())
			t.join();
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	if (m_glfWindow != NULL)
	{
		glfwDestroyWindow(m_glfWindow);
	}
	glfwTerminate();
}

boost::leaf::result<void> Application::Init(int argc, char* argv[], std::string appTitle)
{

	namespace po = boost::program_options;
	po::options_description desc("Allowed Options");
	desc.add_options()("help,h", "produces help message")
		(
			"theme", po::value<std::string>(&m_appConfig.Theme)->default_value("light")->implicit_value("light"), "theme to use(light,dark)")
		("loglevel", po::value<uint8_t>(&m_appConfig.LogSeverity)->default_value(0)->implicit_value(0), "log level: 0 = trace, 1 = debug, 2 = info, 3 = warning, 4 = error, 5 = fatal")
		("default_font", po::value<std::string>(&m_appConfig.DefaultFont)->default_value("segoe-ui.ttf"), "default font to use(see fonts dir)")
		("default_font_bold", po::value<std::string>(&m_appConfig.DefaultFontBold)->default_value("segoe-ui.ttf"), "default bold font to use(see fonts dir)")
		("fullscreen", po::value<bool>(&m_appConfig.FullScreen)->default_value(false), "Full screen mode")
		;

	po::variables_map varmap;
	po::store(po::parse_command_line(argc, argv, desc), varmap);
	po::notify(varmap);

	if (varmap.count("help"))
	{
		std::cout << desc << "\n";
		return boost::leaf::new_error(AppErrc::CleanExit);
	}
	if (varmap.count("loglevel"))
	{
		if (m_appConfig.LogSeverity < 0 || m_appConfig.LogSeverity > 5)
		{
			std::cout << "invalid log severity, value must be between 0 and 5";
			return boost::leaf::new_error(AppErrc::CleanExit);
		}
	}


	m_appConfig.AppName = appTitle;

	

	// setup custom commands
	AppConsole::CommandsMap cmdsMap;
	cmdsMap["TEST"] = [&](std::string cmd)
	{
		BOOST_LOG_TRIVIAL(info) << "executing " << cmd << " command";
	};


	// setup logger sink
	// boost log does the refcounting and destruction here
	// otherwise we end up with double delete
	m_AppConsole = new AppConsole(cmdsMap);

	BOOST_LEAF_CHECK(initLogging());
	BOOST_LEAF_CHECK(initGLFW());
	BOOST_LEAF_CHECK(initGLAD());
	BOOST_LEAF_CHECK(initImGUI());
	
	
	glEnable(GL_DEBUG_OUTPUT);


	m_dummyWork = std::make_unique<boost::asio::executor_work_guard<decltype (m_ioContext.get_executor())>>(m_ioContext.get_executor());
	for (int i = 0; i < 4; i++)
	{
		m_workerThreads.push_back(std::thread(&boost::asio::io_context::run, &m_ioContext));
	}

	{
		std::string err;
		if (!IrcConfig::loadIdentity(m_irc.identity, &err))
			BOOST_LOG_TRIVIAL(warning) << "Could not load identity.json: " << err;
		if (!IrcConfig::loadServers(m_irc.servers, &err))
			BOOST_LOG_TRIVIAL(warning) << "Could not load servers.json: " << err;
		else
			BOOST_LOG_TRIVIAL(info) << "Loaded " << m_irc.servers.size() << " server profile(s) from " << IrcConfig::serversPath();
	}

	m_ircEvents = std::make_shared<IrcEventQueue>();

	return {};
}

boost::leaf::result<int> Application::AppLoop(bool& running)
{
#ifdef USE_GLFW

	while (!glfwWindowShouldClose(m_glfWindow) && running)
	{
		render();
	}
#endif

	return {};
}

boost::leaf::result<void> Application::initGLFW()
{
#ifdef USE_GLFW

	glfwSetErrorCallback([](int err, const char* desc) {
		BOOST_LOG_TRIVIAL(error) << "GLFW Error: " << err << " Description: " << desc;
		});

	if (!glfwInit())
	{
		return boost::leaf::new_error(GLFWErrc::glfw_init_failed);
	}
	else
	{
		//BOOST_LOG_TRIVIAL(info) << "GLFW initialized";
	}

	glfwWindowHint(GLFW_DOUBLEBUFFER, 1);
	glfwWindowHint(GLFW_DEPTH_BITS, 24);
	glfwWindowHint(GLFW_STENCIL_BITS, 8);

	//glfwWindowHint(GLFW_DECORATED, false);

	// adjust these values depending on the OpenGL supported by your GPU driver
	std::string glsl_version = "";
#ifdef __APPLE__
	// GL 3.2 + GLSL 150
	glsl_version = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	// required on Mac OS
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#elif __linux__
	// GL 3.2 + GLSL 150
	glsl_version = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#elif _WIN32
	// GL 3.0 + GLSL 130
	glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif

	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef _WIN32
	// if it's a HighDPI monitor, try to scale everything
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	float xscale, yscale;
	glfwGetMonitorContentScale(monitor, &xscale, &yscale);
	//BOOST_LOG_TRIVIAL(info) << "Monitor scale: " << xscale << "x" << yscale;
	if (xscale > 1 || yscale > 1)
	{
		m_appConfig.HighDPIscaleFactor = xscale;
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
	}
#elif __APPLE__
	// to prevent 1200x800 from becoming 2400x1600
	// and some other weird resizings
	glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif
#endif
	// const GLFWvidmode *mode = glfwGetVideoMode(monitor);
	// glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	if (m_appConfig.FullScreen)
	{
// #ifdef USE_GLFW
// 		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
// 		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
// 		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
// 		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
// 		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);


// 		m_glfWindow = glfwCreateWindow(mode->width,  // mode->width,
// 			mode->height, // mode->height,
// 			m_appConfig.AppName.c_str(),
// 			monitor, // monitor
// 			NULL);
// 		glfwSetWindowPos(m_glfWindow, 0, 0);


// #endif
	}
	else
	{
#ifdef USE_GLFW

		m_glfWindow = glfwCreateWindow(m_appConfig.WindowWidth,  // mode->width,
			m_appConfig.WindowHeight, // mode->height,
			m_appConfig.AppName.c_str(),
			NULL, // monitor
			NULL);
#endif
	}
#ifdef USE_GLFW

	if (!m_glfWindow)
	{
		BOOST_LOG_TRIVIAL(error) << "Couldn't create a GLFW window";

		return boost::leaf::new_error(GLFWErrc::glfw_window_create_failed);
	}

	glfwSetWindowPos(m_glfWindow, 100, 100);
	glfwSetWindowSizeLimits(m_glfWindow, static_cast<int>(900 * m_appConfig.HighDPIscaleFactor),
		static_cast<int>(500 * m_appConfig.HighDPIscaleFactor), GLFW_DONT_CARE, GLFW_DONT_CARE);

	// watch window resizing
	glfwSetWindowUserPointer(m_glfWindow, this);
	glfwSetFramebufferSizeCallback(m_glfWindow, [](GLFWwindow* window, int width, int height) {
		Application* pthis = (Application*)glfwGetWindowUserPointer(window);
		glViewport(0, 0, width, height);

		pthis->render();
		});


	glfwMakeContextCurrent(m_glfWindow);
	// VSync
	glfwSwapInterval(1);

	bool err = gladLoadGL() == 0;

	//BOOST_LOG_TRIVIAL(info) << "OpenGL from GLFW" << glfwGetWindowAttrib(m_glfWindow, GLFW_CONTEXT_VERSION_MAJOR) << "."
	//	<< glfwGetWindowAttrib(m_glfWindow, GLFW_CONTEXT_VERSION_MINOR);
#endif
	return {};
}

boost::leaf::result<void> Application::initGLAD()
{
	// load all OpenGL function pointers with glad
	// without it not all the OpenGL functions will be available,
	// such as glGetString(GL_RENDERER), and application might just segfault
#ifdef USE_GLFW

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		BOOST_LOG_TRIVIAL(error) << "Couldn't initialize GLAD";

		return boost::leaf::new_error(GLADErrc::glad_init_failed);
	}
	else
	{
		//BOOST_LOG_TRIVIAL(info) << "GLAD initialized";
	}

	//BOOST_LOG_TRIVIAL(info) << "OpenGL renderer: " << glGetString(GL_RENDERER);
	//BOOST_LOG_TRIVIAL(info) << "OpenGL from glad " << GLVersion.major << "." << GLVersion.minor;
#endif
	return {};
}

boost::leaf::result<void> Application::initImGUI()
{

	IMGUI_CHECKVERSION();

	// main context
	ImGui::CreateContext();
	//ImGui::SetCurrentContext(mainCtx);
	ImGuiIO& io = ImGui::GetIO();

	// setImGuiStyle(highDPIscaleFactor);

	// setup platform/renderer bindings
#ifdef USE_GLFW

	if (!ImGui_ImplGlfw_InitForOpenGL(m_glfWindow, true))
	{
		BOOST_LOG_TRIVIAL(error) << "ImGUI GLFW Init Failed";
		return boost::leaf::new_error(ImGUIErrc::glfw_init_failed);
	}
	if (!ImGui_ImplOpenGL3_Init())
	{
		BOOST_LOG_TRIVIAL(error) << "ImGUI OpenGL Init Failed";
		return boost::leaf::new_error(ImGUIErrc::opengl_init_failed);
	}
#endif




	io.Fonts->Clear();
	io.Fonts->AddFontDefault();
	namespace fs = std::filesystem;
	std::filesystem::path path = std::filesystem::path(boost::dll::program_location().remove_filename().string()) / "fonts";
	int maxnum = 200;
	int count = 0;
	if (fs::exists(path) && fs::is_directory(path))
	{
		for (const auto& entry : fs::directory_iterator(path))
		{
			if (entry.path().extension().string() == ".ttf")
			{
				std::string fontpath = entry.path().string();
				std::string fontname = entry.path().filename().string();
				for (float i = 12.0f; i < 48.0f; i += 2.0f)
				{
					m_FontsMap[fontname][i] = io.Fonts->AddFontFromFileTTF(fontpath.c_str(), i * m_appConfig.HighDPIscaleFactor, NULL, NULL);
					count++;
				}
			}

			if (count >= maxnum)
				break;
		}
	}
	else
	{
		BOOST_LOG_TRIVIAL(warning) << "Fonts directory not found, using default font: " << path.string();
	}

	return {};
}


boost::leaf::result<void> Application::initLogging()
{
	namespace logging = boost::log;
	namespace keywords = boost::log::keywords;
	namespace attrs = boost::log::attributes;

	logging::register_simple_formatter_factory<logging::trivial::severity_level, char>("Severity");

	boost::log::add_common_attributes();
	auto core = boost::log::core::get();

	core->add_global_attribute("UTCTimeStamp", boost::log::attributes::utc_clock());
	std::stringstream logpath;
	logpath << "./logs/" << m_appConfig.AppName << "_%Y-%m-%d.%3N.log";

	auto x = boost::log::add_file_log(
		boost::log::keywords::file_name = logpath.str(),
		// boost::log::keywords::rotation_size         = 1 * 1024 * 1024, // 1k
		boost::log::keywords::target = "./logs/", boost::log::keywords::min_free_space = 100 * 1024 * 1024,
		boost::log::keywords::max_size = 2 * 1024 * 1024,
		// boost::log::keywords::time_based_rotation   =
		// boost::log::sinks::file::rotation_at_time_point(boost::gregorian::greg_day(01)),
		boost::log::keywords::scan_method = boost::log::sinks::file::scan_matching,
		boost::log::keywords::format =
		"[UTC: %UTCTimeStamp%] [LOCAL: %TimeStamp%] [%Severity%] [PID: %ProcessID%] [TID: %ThreadID%] :: %Message%",
		boost::log::keywords::auto_flush = true);

	logging::add_console_log(std::cout,
		boost::log::keywords::format =
		"(%TimeStamp%) [%Severity%] [PID: %ProcessID%] [TID: %ProcessID%]:: %Message%");

	logging::core::get()->set_filter(logging::trivial::severity >=
		logging::trivial::severity_level(m_appConfig.LogSeverity));

	return {};
}

boost::leaf::result<void> Application::render()
{
	// the frame starts with a clean scene
	int display_w = 0, display_h = 0;
	glfwGetFramebufferSize(m_glfWindow, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(m_appConfig.BackgroundColor[0], m_appConfig.BackgroundColor[1],
		m_appConfig.BackgroundColor[2], m_appConfig.BackgroundColor[3]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// continuous rendering, even if window is not visible or minimized
	//ImGui::SetCurrentContext(mainCtx);
	//ImGuiIO& mainIO = ImGui::GetIO();

	drawGUI();
	/*int display_w, display_h;
	glfwGetFramebufferSize(m_glfWindow, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glScissor(0, 0, display_w, display_h);
	*/
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	glfwSwapBuffers(m_glfWindow);
	glfwPollEvents();
	return {};
}






void Application::processLoop()
{
	std::chrono::steady_clock::time_point m_LastPollTick = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point m_Last10MSTick = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point m_Last100MSTick = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point m_Last200MSTick = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point m_Last500MSTick = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point m_Last1000MSTick = std::chrono::steady_clock::now();
	
	while (true)
	{
		std::chrono::steady_clock::time_point tnow = std::chrono::steady_clock::now();
		uint64_t delta10MS = std::chrono::duration_cast<std::chrono::milliseconds>(tnow - m_Last10MSTick).count();
		uint64_t delta100MS = std::chrono::duration_cast<std::chrono::milliseconds>(tnow - m_Last100MSTick).count();
		uint64_t delta200MS = std::chrono::duration_cast<std::chrono::milliseconds>(tnow - m_Last200MSTick).count();
		uint64_t delta500MS = std::chrono::duration_cast<std::chrono::milliseconds>(tnow - m_Last500MSTick).count();
		uint64_t delta1000MS = std::chrono::duration_cast<std::chrono::milliseconds>(tnow - m_Last1000MSTick).count();


		if (delta10MS >= 10)
		{
			m_Last10MSTick = tnow;

			// check status and stop conv

		

		}
		if (delta100MS >= 100)
		{
			// 100ms tick
			m_Last100MSTick = tnow;
			
			

		}
		if (delta200MS >= 200)
		{
			// 200ms tick
			m_Last200MSTick = tnow;

		}
		if (delta500MS >= 500)
		{
			// 500ms tick
			m_Last500MSTick = tnow;
		}
		if (delta1000MS >= 1000)
		{
			// 1000ms tick
			m_Last1000MSTick = tnow;

		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

boost::leaf::result<void> Application::drawGUI()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	int width = 0, height = 0;
	glfwGetWindowSize(m_glfWindow, &width, &height);

	ImGui::StyleColorsLight();

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowBorderSize = 0.0f;
	style.WindowRounding = 0.0f;

	int glfw_width = 0, glfw_height = 0;
	glfwGetFramebufferSize(m_glfWindow, &glfw_width, &glfw_height);
	float imWidth = (float)glfw_width;
	float imHeight = (float)glfw_height;
	m_imWidth = imWidth;
	m_imHeight = imHeight;
	ImGui::SetNextWindowSize(ImVec2((float)width, (float)height));
	ImGui::SetNextWindowPos(ImVec2(0, 0));

	ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_MenuBar;

	ImGui::Begin("##ImIRCRoot", nullptr, root_flags);

	pollIrcEvents();

	drawMainMenu();
	drawServerDialog();
	drawIdentityDialog();
	drawJoinDialog();
	drawChannelOptionsDialog();
	drawChannelBrowser();

	const float console_bar = ImGui::GetFrameHeightWithSpacing();
	float console_body = m_showConsole && m_consoleExpanded ? m_consoleHeight : 0.0f;
	const float bottom_reserve = console_bar + console_body;
	const float main_height = ImGui::GetContentRegionAvail().y - bottom_reserve;
	const float avail_w = ImGui::GetContentRegionAvail().x;

	m_leftPaneWidth = std::clamp(m_leftPaneWidth, 140.0f, avail_w * 0.45f);
	m_rightPaneWidth = std::clamp(m_rightPaneWidth, 100.0f, avail_w * 0.4f);

	ImGui::BeginChild("##MainArea", ImVec2(0, main_height), ImGuiChildFlags_Borders);

	ImGui::BeginChild("##LeftPane", ImVec2(m_leftPaneWidth, 0), ImGuiChildFlags_Borders);
	drawServerChannelTree();
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::Button("##vsplit_l", ImVec2(4.0f, -1));
	if (ImGui::IsItemActive())
		m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;

	ImGui::SameLine();
	const float center_w = ImGui::GetContentRegionAvail().x - m_rightPaneWidth - 8.0f;
	ImGui::BeginChild("##CenterPane", ImVec2(std::max(120.0f, center_w), 0), ImGuiChildFlags_Borders);
	drawChannelTabs();
	const float input_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
	ImGui::BeginChild("##ChatScroll", ImVec2(0, -input_h), ImGuiChildFlags_None);
	drawChatPane();
	ImGui::EndChild();
	drawInputBar();
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::Button("##vsplit_r", ImVec2(4.0f, -1));
	if (ImGui::IsItemActive())
		m_rightPaneWidth -= ImGui::GetIO().MouseDelta.x;

	ImGui::SameLine();
	ImGui::BeginChild("##RightPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
	drawUserList();
	ImGui::EndChild();

	ImGui::EndChild(); // MainArea

	drawConsolePane();

	ImGui::End(); // root

	return {};
}

void Application::drawMainMenu()
{
	if (!ImGui::BeginMenuBar())
		return;

	if (ImGui::BeginMenu("Server"))
	{
		if (ImGui::MenuItem("Connect / Add..."))
			openServerDialog(-1);
		if (ImGui::BeginMenu("Connect to"))
		{
			if (m_irc.servers.empty())
				ImGui::MenuItem("(no saved servers)", nullptr, false, false);
			for (int i = 0; i < (int)m_irc.servers.size(); ++i)
			{
				const auto& s = m_irc.servers[(size_t)i];
				std::string label = s.name + " (" + s.host + ")";
				if (ImGui::MenuItem(label.c_str()))
				{
					connectToServer(s);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit server"))
		{
			for (int i = 0; i < (int)m_irc.servers.size(); ++i)
			{
				if (ImGui::MenuItem(m_irc.servers[(size_t)i].name.c_str()))
					openServerDialog(i);
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		bool can_disconnect = m_irc.active_tab >= 0
			&& m_irc.active_tab < (int)m_irc.open_tabs.size();
		if (ImGui::MenuItem("Disconnect", nullptr, false, can_disconnect))
		{
			const OpenTab& tab = m_irc.open_tabs[(size_t)m_irc.active_tab];
			disconnectServer(tab.session_idx);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Account"))
	{
		if (ImGui::MenuItem("Identity / NickServ..."))
			openIdentityDialog();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Channel"))
	{
		bool has_session = !m_irc.sessions.empty()
			&& m_irc.active_tab >= 0
			&& m_irc.active_tab < (int)m_irc.open_tabs.size();
		bool connected = false;
		int si = -1;
		int ci = -1;
		if (has_session)
		{
			si = m_irc.open_tabs[(size_t)m_irc.active_tab].session_idx;
			ci = m_irc.open_tabs[(size_t)m_irc.active_tab].channel_idx;
			connected = si >= 0 && si < (int)m_irc.sessions.size() && m_irc.sessions[(size_t)si].connected;
		}
		if (ImGui::MenuItem("Join...", nullptr, false, has_session && connected))
		{
			m_joinDraft.clear();
			m_joinOptionsDraft = RememberedChannel{};
			m_showJoinDialog = true;
		}
		if (ImGui::MenuItem("Browse channels...", nullptr, false, connected))
			openChannelBrowser(si);
		const bool on_channel = connected && ci >= 0;
		if (ImGui::MenuItem("Channel options / ChanServ...", nullptr, false, on_channel))
		{
			const auto& ch = m_irc.sessions[(size_t)si].channels[(size_t)ci];
			openChannelOptionsDialog(si, ch.name);
		}
		if (ImGui::BeginMenu("ChanServ", on_channel))
		{
			const std::string& chname = m_irc.sessions[(size_t)si].channels[(size_t)ci].name;
			ServerConfig* scfg = nullptr;
			for (auto& cfg : m_irc.servers)
			{
				if (cfg.id == m_irc.sessions[(size_t)si].config_id)
				{
					scfg = &cfg;
					break;
				}
			}
			const RememberedChannel* rc = scfg ? IrcConfig::findRememberedChannel(*scfg, chname) : nullptr;
			if (ImGui::MenuItem("Identify...", nullptr, false, rc && !rc->chanserv_password.empty()))
			{
				if (auto conn = connectionForSession(si))
					conn->chanServIdentify(chname, rc->chanserv_password);
			}
			if (ImGui::MenuItem("OP me", nullptr, false, true))
			{
				if (auto conn = connectionForSession(si))
					conn->chanServOp(chname);
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem("Console log", nullptr, &m_showConsole);
		if (ImGui::MenuItem(m_consoleExpanded ? "Collapse console" : "Expand console", nullptr, false, m_showConsole))
			m_consoleExpanded = !m_consoleExpanded;
		ImGui::EndMenu();
	}

	ImGui::EndMenuBar();
}

void Application::openIdentityDialog()
{
	m_identityDraft = m_irc.identity;
	switch (m_identityDraft.default_auth)
	{
	case IrcAuthMethod::None: m_identityAuthIndex = 0; break;
	case IrcAuthMethod::SaslPlain: m_identityAuthIndex = 2; break;
	default: m_identityAuthIndex = 1; break;
	}
	m_showIdentityDialog = true;
}

void Application::drawIdentityDialog()
{
	if (!m_showIdentityDialog)
		return;

	ImGui::OpenPopup("Identity");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal("Identity", &m_showIdentityDialog, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::TextWrapped("Default nick used for reserved accounts. Server profiles can override these.");
	ImGui::InputText("Nick", &m_identityDraft.nick);
	ImGui::InputText("Alt nick", &m_identityDraft.alt_nick);
	ImGui::InputText("Account (blank = nick)", &m_identityDraft.account);
	ImGui::InputText("Username", &m_identityDraft.username);
	ImGui::InputText("Real name", &m_identityDraft.realname);
	ImGui::InputText("NickServ / SASL password", &m_identityDraft.nickserv_password, ImGuiInputTextFlags_Password);

	const char* auth_items[] = { "None", "NickServ IDENTIFY", "SASL PLAIN" };
	ImGui::Combo("Auth method", &m_identityAuthIndex, auth_items, IM_ARRAYSIZE(auth_items));
	ImGui::Checkbox("GHOST if nick is taken", &m_identityDraft.ghost_on_nick_taken);
	ImGui::TextDisabled("Uses NickServ GHOST then reclaim when primary nick is in use.");

	if (!m_identityDraft.nick_history.empty())
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Recent nicks");
		if (ImGui::BeginListBox("##NickHistory", ImVec2(-FLT_MIN, 6 * ImGui::GetTextLineHeightWithSpacing())))
		{
			int remove_idx = -1;
			for (int i = 0; i < (int)m_identityDraft.nick_history.size(); ++i)
			{
				ImGui::PushID(i);
				const bool selected = (m_identityDraft.nick == m_identityDraft.nick_history[(size_t)i]);
				if (ImGui::Selectable(m_identityDraft.nick_history[(size_t)i].c_str(), selected))
					m_identityDraft.nick = m_identityDraft.nick_history[(size_t)i];
				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Use as nick"))
						m_identityDraft.nick = m_identityDraft.nick_history[(size_t)i];
					if (ImGui::MenuItem("Remove"))
						remove_idx = i;
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			ImGui::EndListBox();
			if (remove_idx >= 0)
				m_identityDraft.nick_history.erase(m_identityDraft.nick_history.begin() + remove_idx);
		}
	}

	ImGui::Separator();
	if (ImGui::Button("Save", ImVec2(100, 0)))
	{
		if (m_identityAuthIndex == 0)
			m_identityDraft.default_auth = IrcAuthMethod::None;
		else if (m_identityAuthIndex == 2)
			m_identityDraft.default_auth = IrcAuthMethod::SaslPlain;
		else
			m_identityDraft.default_auth = IrcAuthMethod::NickServ;

		IrcConfig::rememberNick(m_identityDraft, m_identityDraft.nick);
		m_irc.identity = m_identityDraft;
		std::string err;
		if (!IrcConfig::saveIdentity(m_irc.identity, &err))
			BOOST_LOG_TRIVIAL(error) << "Failed to save identity: " << err;
		else
			BOOST_LOG_TRIVIAL(info) << "Saved identity to " << IrcConfig::identityPath();
		m_showIdentityDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 0)))
	{
		m_showIdentityDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Application::openServerDialog(int edit_index)
{
	m_editServerIndex = edit_index;
	if (edit_index >= 0 && edit_index < (int)m_irc.servers.size())
	{
		m_serverDraft = m_irc.servers[(size_t)edit_index];
	}
	else
	{
		m_serverDraft = ServerConfig{};
		m_serverDraft.id = IrcConfig::makeId();
		m_serverDraft.name = "New Server";
		m_serverDraft.host = "irc.libera.chat";
		m_serverDraft.port = 6697;
		m_serverDraft.use_tls = true;
		m_serverDraft.auth_method = IrcAuthMethod::Inherit;
		m_serverDraft.auto_join_remembered = true;
	}
	m_autoJoinDraft.clear();
	for (size_t i = 0; i < m_serverDraft.auto_join.size(); ++i)
	{
		if (i) m_autoJoinDraft += ", ";
		m_autoJoinDraft += m_serverDraft.auto_join[i];
	}
	m_newRememberedChannel.clear();
	switch (m_serverDraft.auth_method)
	{
	case IrcAuthMethod::None: m_serverAuthIndex = 1; break;
	case IrcAuthMethod::NickServ: m_serverAuthIndex = 2; break;
	case IrcAuthMethod::SaslPlain: m_serverAuthIndex = 3; break;
	default: m_serverAuthIndex = 0; break;
	}
	m_showServerDialog = true;
}

void Application::drawServerDialog()
{
	if (!m_showServerDialog)
		return;

	ImGui::OpenPopup("Server");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal("Server", &m_showServerDialog, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::InputText("Name", &m_serverDraft.name);
	ImGui::InputText("Host", &m_serverDraft.host);
	ImGui::InputInt("Port", &m_serverDraft.port);
	ImGui::Checkbox("TLS", &m_serverDraft.use_tls);
	ImGui::InputText("Nick (blank = identity)", &m_serverDraft.nick);
	ImGui::InputText("Alt nick (blank = identity)", &m_serverDraft.alt_nick);
	ImGui::InputText("Account (blank = identity)", &m_serverDraft.account);
	ImGui::InputText("Username (blank = identity)", &m_serverDraft.username);
	ImGui::InputText("Real name (blank = identity)", &m_serverDraft.realname);
	ImGui::InputText("Server password", &m_serverDraft.password, ImGuiInputTextFlags_Password);
	ImGui::InputText("NickServ password (blank = identity)", &m_serverDraft.nickserv_password, ImGuiInputTextFlags_Password);
	const char* server_auth_items[] = { "Inherit identity", "None", "NickServ IDENTIFY", "SASL PLAIN" };
	ImGui::Combo("Auth method", &m_serverAuthIndex, server_auth_items, IM_ARRAYSIZE(server_auth_items));
	ImGui::Checkbox("GHOST if nick is taken", &m_serverDraft.ghost_on_nick_taken);
	ImGui::InputText("Auto-join (comma-separated)", &m_autoJoinDraft);
	ImGui::Checkbox("Also auto-join remembered channels", &m_serverDraft.auto_join_remembered);

	ImGui::Separator();
	ImGui::TextUnformatted("Remembered channels");
	ImGui::TextDisabled("Keys, ChanServ, and auto-join settings for each channel.");
	if (ImGui::BeginTable("##RememberedChannels", 6,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("CS Pass", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 28.0f);
		ImGui::TableSetupColumn("OP", ImGuiTableColumnFlags_WidthFixed, 28.0f);
		ImGui::TableSetupColumn("AJ", ImGuiTableColumnFlags_WidthFixed, 28.0f);
		ImGui::TableHeadersRow();

		int remove_idx = -1;
		for (int i = 0; i < (int)m_serverDraft.remembered_channels.size(); ++i)
		{
			RememberedChannel& rc = m_serverDraft.remembered_channels[(size_t)i];
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##name", &rc.name);
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("rc_ctx");
			if (ImGui::BeginPopup("rc_ctx"))
			{
				if (ImGui::MenuItem("Remove"))
					remove_idx = i;
				ImGui::EndPopup();
			}
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##key", &rc.key, ImGuiInputTextFlags_Password);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##cspass", &rc.chanserv_password, ImGuiInputTextFlags_Password);
			ImGui::TableNextColumn();
			ImGui::Checkbox("##id", &rc.chanserv_identify);
			ImGui::TableNextColumn();
			ImGui::Checkbox("##op", &rc.chanserv_op);
			ImGui::TableNextColumn();
			ImGui::Checkbox("##aj", &rc.auto_join);
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (remove_idx >= 0)
			m_serverDraft.remembered_channels.erase(m_serverDraft.remembered_channels.begin() + remove_idx);
	}
	ImGui::SetNextItemWidth(180);
	ImGui::InputText("##newrc", &m_newRememberedChannel);
	ImGui::SameLine();
	if (ImGui::Button("Add channel") && !m_newRememberedChannel.empty())
	{
		if (m_newRememberedChannel[0] != '#' && m_newRememberedChannel[0] != '&')
			m_newRememberedChannel = "#" + m_newRememberedChannel;
		IrcConfig::upsertRememberedChannel(m_serverDraft, m_newRememberedChannel);
		m_newRememberedChannel.clear();
	}
	if (!m_serverDraft.remembered_channels.empty() && ImGui::SmallButton("Clear remembered"))
		m_serverDraft.remembered_channels.clear();

	ImGui::Separator();
	if (ImGui::Button("Save", ImVec2(100, 0)))
	{
		m_serverDraft.auto_join.clear();
		std::stringstream ss(m_autoJoinDraft);
		std::string part;
		while (std::getline(ss, part, ','))
		{
			boost::algorithm::trim(part);
			if (!part.empty())
				m_serverDraft.auto_join.push_back(part);
		}
		if (m_serverDraft.port <= 0 || m_serverDraft.port > 65535)
			m_serverDraft.port = 6667;
		if (m_serverAuthIndex == 1)
			m_serverDraft.auth_method = IrcAuthMethod::None;
		else if (m_serverAuthIndex == 2)
			m_serverDraft.auth_method = IrcAuthMethod::NickServ;
		else if (m_serverAuthIndex == 3)
			m_serverDraft.auth_method = IrcAuthMethod::SaslPlain;
		else
			m_serverDraft.auth_method = IrcAuthMethod::Inherit;

		if (m_editServerIndex >= 0 && m_editServerIndex < (int)m_irc.servers.size())
			m_irc.servers[(size_t)m_editServerIndex] = m_serverDraft;
		else
			m_irc.servers.push_back(m_serverDraft);

		std::string err;
		if (!IrcConfig::saveServers(m_irc.servers, &err))
			BOOST_LOG_TRIVIAL(error) << "Failed to save servers: " << err;
		else
			BOOST_LOG_TRIVIAL(info) << "Saved servers to " << IrcConfig::serversPath();

		m_showServerDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save & Connect", ImVec2(140, 0)))
	{
		m_serverDraft.auto_join.clear();
		std::stringstream ss(m_autoJoinDraft);
		std::string part;
		while (std::getline(ss, part, ','))
		{
			boost::algorithm::trim(part);
			if (!part.empty())
				m_serverDraft.auto_join.push_back(part);
		}
		if (m_serverAuthIndex == 1)
			m_serverDraft.auth_method = IrcAuthMethod::None;
		else if (m_serverAuthIndex == 2)
			m_serverDraft.auth_method = IrcAuthMethod::NickServ;
		else if (m_serverAuthIndex == 3)
			m_serverDraft.auth_method = IrcAuthMethod::SaslPlain;
		else
			m_serverDraft.auth_method = IrcAuthMethod::Inherit;

		if (m_editServerIndex >= 0 && m_editServerIndex < (int)m_irc.servers.size())
			m_irc.servers[(size_t)m_editServerIndex] = m_serverDraft;
		else
			m_irc.servers.push_back(m_serverDraft);
		IrcConfig::saveServers(m_irc.servers);
		connectToServer(m_serverDraft);
		m_showServerDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 0)))
	{
		m_showServerDialog = false;
		ImGui::CloseCurrentPopup();
	}

	if (m_editServerIndex >= 0)
	{
		ImGui::Separator();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
		if (ImGui::Button("Delete", ImVec2(100, 0)))
		{
			m_irc.servers.erase(m_irc.servers.begin() + m_editServerIndex);
			IrcConfig::saveServers(m_irc.servers);
			m_showServerDialog = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::PopStyleColor();
	}

	ImGui::EndPopup();
}

void Application::drawJoinDialog()
{
	if (!m_showJoinDialog)
		return;

	ImGui::OpenPopup("Join Channel");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::BeginPopupModal("Join Channel", &m_showJoinDialog, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::InputText("Channel", &m_joinDraft);
	ImGui::InputText("Key (+k)", &m_joinOptionsDraft.key, ImGuiInputTextFlags_Password);
	ImGui::Separator();
	ImGui::TextUnformatted("ChanServ");
	ImGui::InputText("Password", &m_joinOptionsDraft.chanserv_password, ImGuiInputTextFlags_Password);
	ImGui::Checkbox("Identify on join", &m_joinOptionsDraft.chanserv_identify);
	ImGui::Checkbox("Request OP on join", &m_joinOptionsDraft.chanserv_op);
	ImGui::Checkbox("Auto-join next time", &m_joinOptionsDraft.auto_join);

	if (ImGui::Button("Join", ImVec2(100, 0)))
	{
		if (!m_joinDraft.empty()
			&& m_irc.active_tab >= 0
			&& m_irc.active_tab < (int)m_irc.open_tabs.size())
		{
			if (m_joinDraft[0] != '#' && m_joinDraft[0] != '&')
				m_joinDraft = "#" + m_joinDraft;
			m_joinOptionsDraft.name = m_joinDraft;
			if (!m_joinOptionsDraft.chanserv_password.empty())
				m_joinOptionsDraft.chanserv_identify = true;
			const int session_idx = m_irc.open_tabs[(size_t)m_irc.active_tab].session_idx;
			joinChannel(session_idx, m_joinDraft, &m_joinOptionsDraft);
		}
		m_showJoinDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 0)))
	{
		m_showJoinDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Application::openChannelOptionsDialog(int session_idx, const std::string& channel)
{
	if (session_idx < 0 || session_idx >= (int)m_irc.sessions.size() || channel.empty())
		return;
	m_channelOptionsSession = session_idx;
	m_channelOptionsDraft = RememberedChannel{ .name = channel };
	const std::string& sid = m_irc.sessions[(size_t)session_idx].config_id;
	for (auto& cfg : m_irc.servers)
	{
		if (cfg.id != sid)
			continue;
		if (const auto* rc = IrcConfig::findRememberedChannel(cfg, channel))
			m_channelOptionsDraft = *rc;
		break;
	}
	m_showChannelOptionsDialog = true;
}

void Application::drawChannelOptionsDialog()
{
	if (!m_showChannelOptionsDialog)
		return;

	ImGui::OpenPopup("Channel Options");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::BeginPopupModal("Channel Options", &m_showChannelOptionsDialog, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::Text("Channel: %s", m_channelOptionsDraft.name.c_str());
	ImGui::InputText("Key (+k)", &m_channelOptionsDraft.key, ImGuiInputTextFlags_Password);
	ImGui::Separator();
	ImGui::TextUnformatted("ChanServ");
	ImGui::InputText("Password", &m_channelOptionsDraft.chanserv_password, ImGuiInputTextFlags_Password);
	ImGui::Checkbox("Identify on join", &m_channelOptionsDraft.chanserv_identify);
	ImGui::Checkbox("Request OP on join", &m_channelOptionsDraft.chanserv_op);
	ImGui::Checkbox("Auto-join", &m_channelOptionsDraft.auto_join);
	if (!m_channelOptionsDraft.chanserv_password.empty() && !m_channelOptionsDraft.chanserv_identify)
		ImGui::TextDisabled("Tip: enable Identify on join to use the ChanServ password automatically.");

	ImGui::Separator();
	if (ImGui::Button("Save", ImVec2(100, 0)))
	{
		if (m_channelOptionsSession >= 0 && m_channelOptionsSession < (int)m_irc.sessions.size())
		{
			const std::string& sid = m_irc.sessions[(size_t)m_channelOptionsSession].config_id;
			if (!m_channelOptionsDraft.chanserv_password.empty())
				m_channelOptionsDraft.chanserv_identify = true;
			saveRememberedChannelSettings(sid, m_channelOptionsDraft);
		}
		m_showChannelOptionsDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Identify now", ImVec2(120, 0)))
	{
		if (auto conn = connectionForSession(m_channelOptionsSession))
		{
			if (!m_channelOptionsDraft.chanserv_password.empty())
				conn->chanServIdentify(m_channelOptionsDraft.name, m_channelOptionsDraft.chanserv_password);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("OP now", ImVec2(80, 0)))
	{
		if (auto conn = connectionForSession(m_channelOptionsSession))
			conn->chanServOp(m_channelOptionsDraft.name);
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(100, 0)))
	{
		m_showChannelOptionsDialog = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Application::openChannelBrowser(int session_idx)
{
	if (session_idx < 0)
	{
		if (m_irc.active_tab >= 0 && m_irc.active_tab < (int)m_irc.open_tabs.size())
			session_idx = m_irc.open_tabs[(size_t)m_irc.active_tab].session_idx;
	}
	if (session_idx < 0 || session_idx >= (int)m_irc.sessions.size())
		return;
	if (!m_irc.sessions[(size_t)session_idx].connected)
		return;

	m_channelBrowserSession = session_idx;
	m_channelBrowserSelected = -1;
	m_channelBrowserFilter.clear();
	m_showChannelBrowser = true;

	// Auto-refresh if we have no list yet
	if (m_irc.sessions[(size_t)session_idx].channel_list.empty()
		&& !m_irc.sessions[(size_t)session_idx].channel_list_loading)
	{
		refreshChannelList();
	}
}

void Application::refreshChannelList()
{
	if (m_channelBrowserSession < 0 || m_channelBrowserSession >= (int)m_irc.sessions.size())
		return;
	ServerSession& sess = m_irc.sessions[(size_t)m_channelBrowserSession];
	auto conn = connectionForSession(m_channelBrowserSession);
	if (!conn || !sess.connected)
		return;

	sess.channel_list.clear();
	sess.channel_list_loading = true;
	m_channelBrowserSelected = -1;
	conn->listChannels(m_channelBrowserMask);
	IrcState::appendSystem(sess.status, m_channelBrowserMask.empty()
		? "Requesting channel list..."
		: ("Requesting channel list (" + m_channelBrowserMask + ")..."));
}

void Application::drawChannelBrowser()
{
	if (!m_showChannelBrowser)
		return;

	ImGui::OpenPopup("Channel Browser");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal("Channel Browser", &m_showChannelBrowser, ImGuiWindowFlags_None))
		return;

	if (m_channelBrowserSession < 0 || m_channelBrowserSession >= (int)m_irc.sessions.size())
	{
		ImGui::TextUnformatted("No connected server selected.");
		if (ImGui::Button("Close"))
		{
			m_showChannelBrowser = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return;
	}

	ServerSession& sess = m_irc.sessions[(size_t)m_channelBrowserSession];
	ImGui::Text("Server: %s", sess.display_name.c_str());
	ImGui::Separator();

	ImGui::SetNextItemWidth(180);
	ImGui::InputTextWithHint("##listmask", "Server mask (e.g. *linux*)", &m_channelBrowserMask);
	ImGui::SameLine();
	const bool can_refresh = sess.connected && !sess.channel_list_loading;
	if (ImGui::Button("Refresh") && can_refresh)
		refreshChannelList();
	if (sess.channel_list_loading)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Loading… (%zu)", sess.channel_list.size());
	}
	else
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%zu channels", sess.channel_list.size());
	}

	ImGui::SetNextItemWidth(220);
	ImGui::InputTextWithHint("##chfilter", "Filter name/topic…", &m_channelBrowserFilter);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(80);
	ImGui::InputInt("Min users", &m_channelBrowserMinUsers);
	if (m_channelBrowserMinUsers < 0)
		m_channelBrowserMinUsers = 0;

	// Build filtered index list
	std::vector<int> visible;
	visible.reserve(sess.channel_list.size());
	for (int i = 0; i < (int)sess.channel_list.size(); ++i)
	{
		const auto& e = sess.channel_list[(size_t)i];
		if (e.users < m_channelBrowserMinUsers)
			continue;
		if (!m_channelBrowserFilter.empty())
		{
			if (!boost::algorithm::icontains(e.name, m_channelBrowserFilter)
				&& !boost::algorithm::icontains(e.topic, m_channelBrowserFilter))
				continue;
		}
		visible.push_back(i);
	}

	ImGui::Separator();
	const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	ImGui::BeginChild("##chlist", ImVec2(0, -footer), ImGuiChildFlags_Borders);

	if (ImGui::BeginTable("##channels", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
		| ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed, 160.0f);
		ImGui::TableSetupColumn("Users", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Topic", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

		// Optional sort by users/name when not loading
		if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
		{
			if (specs->SpecsDirty && specs->SpecsCount > 0 && !sess.channel_list_loading)
			{
				const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
				std::sort(visible.begin(), visible.end(), [&](int a, int b)
					{
						const auto& ea = sess.channel_list[(size_t)a];
						const auto& eb = sess.channel_list[(size_t)b];
						int cmp = 0;
						if (spec.ColumnIndex == 0)
							cmp = ea.name.compare(eb.name);
						else if (spec.ColumnIndex == 1)
							cmp = (ea.users > eb.users) - (ea.users < eb.users);
						else
							cmp = ea.topic.compare(eb.topic);
						return spec.SortDirection == ImGuiSortDirection_Ascending ? (cmp < 0) : (cmp > 0);
					});
				specs->SpecsDirty = false;
			}
		}

		ImGuiListClipper clipper;
		clipper.Begin((int)visible.size());
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const int idx = visible[(size_t)row];
				const auto& e = sess.channel_list[(size_t)idx];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const bool selected = (m_channelBrowserSelected == idx);
				ImGui::PushID(idx);
				if (ImGui::Selectable(e.name.c_str(), selected,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
				{
					m_channelBrowserSelected = idx;
					if (ImGui::IsMouseDoubleClicked(0))
					{
						joinChannel(m_channelBrowserSession, e.name);
						m_showChannelBrowser = false;
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				ImGui::Text("%d", e.users);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(e.topic.c_str());
			}
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();

	const bool have_sel = m_channelBrowserSelected >= 0
		&& m_channelBrowserSelected < (int)sess.channel_list.size();
	if (ImGui::Button("Join", ImVec2(100, 0)) && have_sel)
	{
		joinChannel(m_channelBrowserSession, sess.channel_list[(size_t)m_channelBrowserSelected].name);
		m_showChannelBrowser = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Join by name…", ImVec2(120, 0)))
	{
		m_joinDraft.clear();
		m_showJoinDialog = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Close", ImVec2(100, 0)))
	{
		m_showChannelBrowser = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Application::drawServerChannelTree()
{
	ImGui::TextUnformatted("Servers");
	ImGui::Separator();

	if (ImGui::Button("+ Connect", ImVec2(-1, 0)))
		openServerDialog(-1);

	ImGui::BeginChild("##ServerTree", ImVec2(0, 0), ImGuiChildFlags_None);

	// Saved profiles (not yet connected sessions)
	if (ImGui::TreeNodeEx("Profiles", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)m_irc.servers.size(); ++i)
		{
			const auto& s = m_irc.servers[(size_t)i];
			ImGui::PushID(i);
			bool selected = false;
			if (ImGui::Selectable(s.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
			{
				if (ImGui::IsMouseDoubleClicked(0))
				{
					connectToServer(s);
					m_forceSelectTab = m_irc.active_tab;
				}
			}
			if (ImGui::BeginPopupContextItem("profile_ctx"))
			{
				if (ImGui::MenuItem("Connect"))
				{
					connectToServer(s);
					m_forceSelectTab = m_irc.active_tab;
				}
				if (ImGui::MenuItem("Edit"))
					openServerDialog(i);
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	for (int si = 0; si < (int)m_irc.sessions.size(); ++si)
	{
		ServerSession& sess = m_irc.sessions[(size_t)si];
		ImGui::PushID(1000 + si);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
		std::string label = sess.display_name;
		if (sess.connecting)
			label += " [connecting]";
		else if (!sess.connected)
			label += " [offline]";
		bool open = ImGui::TreeNodeEx(label.c_str(), flags);

		if (ImGui::BeginPopupContextItem("sess_ctx"))
		{
			if ((sess.connected || sess.connecting) && ImGui::MenuItem("Disconnect"))
				disconnectServer(si);
			if (!sess.connected && !sess.connecting && ImGui::MenuItem("Reconnect"))
			{
				for (const auto& cfg : m_irc.servers)
				{
					if (cfg.id == sess.config_id)
					{
						connectToServer(cfg);
						m_forceSelectTab = m_irc.active_tab;
						break;
					}
				}
			}
			if (ImGui::MenuItem("Join channel..."))
			{
				m_joinDraft.clear();
				m_joinOptionsDraft = RememberedChannel{};
				m_showJoinDialog = true;
				IrcState::ensureTab(m_irc, si, -1);
				m_forceSelectTab = m_irc.active_tab;
			}
			if (sess.connected && ImGui::MenuItem("Browse channels..."))
				openChannelBrowser(si);
			ImGui::EndPopup();
		}

		if (open)
		{
			const bool status_sel = (m_irc.selected_tree_session == si && m_irc.selected_tree_channel == -1);
			if (ImGui::Selectable("(status)", status_sel))
			{
				IrcState::selectTab(m_irc, si, -1);
				m_forceSelectTab = m_irc.active_tab;
			}

			for (int ci = 0; ci < (int)sess.channels.size(); ++ci)
			{
				ChannelState& ch = sess.channels[(size_t)ci];
				ImGui::PushID(ci);
				std::string ch_label = ch.name;
				if (ch.unread)
					ch_label = "* " + ch_label;
				const bool sel = (m_irc.selected_tree_session == si && m_irc.selected_tree_channel == ci);
				if (ImGui::Selectable(ch_label.c_str(), sel))
				{
					IrcState::selectTab(m_irc, si, ci);
					m_forceSelectTab = m_irc.active_tab;
				}
				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Channel options / ChanServ..."))
						openChannelOptionsDialog(si, ch.name);
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}

			// Quick-join from remembered channel history
			const ServerConfig* scfg = nullptr;
			for (const auto& cfg : m_irc.servers)
			{
				if (cfg.id == sess.config_id)
				{
					scfg = &cfg;
					break;
				}
			}
			if (scfg && !scfg->remembered_channels.empty() && ImGui::TreeNode("Remembered"))
			{
				for (int ri = 0; ri < (int)scfg->remembered_channels.size(); ++ri)
				{
					const RememberedChannel& rch = scfg->remembered_channels[(size_t)ri];
					bool already = IrcState::channelIndex(sess, rch.name) >= 0;
					ImGui::PushID(2000 + ri);
					std::string label = rch.name;
					if (!rch.key.empty())
						label += " [key]";
					if (rch.chanserv_identify)
						label += " [cs]";
					if (ImGui::Selectable(label.c_str(), false, already ? ImGuiSelectableFlags_Disabled : 0))
					{
						if (sess.connected)
							joinChannel(si, rch.name, &rch);
					}
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Channel options..."))
							openChannelOptionsDialog(si, rch.name);
						if (ImGui::MenuItem("Join", nullptr, false, sess.connected && !already))
							joinChannel(si, rch.name, &rch);
						ImGui::EndPopup();
					}
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	ImGui::EndChild();
}

void Application::drawChannelTabs()
{
	if (m_irc.open_tabs.empty())
	{
		ImGui::TextDisabled("No open channels — connect to a server to begin.");
		return;
	}

	if (ImGui::BeginTabBar("##ChannelTabs",
		ImGuiTabBarFlags_Reorderable
		| ImGuiTabBarFlags_AutoSelectNewTabs
		| ImGuiTabBarFlags_FittingPolicyScroll))
	{
		int close_idx = -1;
		for (int i = 0; i < (int)m_irc.open_tabs.size(); ++i)
		{
			const OpenTab& tab = m_irc.open_tabs[(size_t)i];
			if (tab.session_idx < 0 || tab.session_idx >= (int)m_irc.sessions.size())
				continue;
			ServerSession& sess = m_irc.sessions[(size_t)tab.session_idx];
			ChannelState* ch = IrcState::getChannel(sess, tab.channel_idx);
			if (!ch)
				continue;

			std::string title = ch->name;
			if (tab.channel_idx < 0)
				title = sess.display_name + " / status";
			else
				title = sess.display_name + " / " + ch->name;
			// Stable ID so drag-reorder survives title/unread changes
			title += "###tab_" + std::to_string(tab.session_idx) + "_" + std::to_string(tab.channel_idx);

			bool open = true;
			ImGuiTabItemFlags flags = 0;
			if (m_forceSelectTab == i)
			{
				flags |= ImGuiTabItemFlags_SetSelected;
				m_forceSelectTab = -1;
			}
			if (ImGui::BeginTabItem(title.c_str(), &open, flags))
			{
				m_irc.active_tab = i;
				m_irc.selected_tree_session = tab.session_idx;
				m_irc.selected_tree_channel = tab.channel_idx;
				ch->unread = false;
				ImGui::EndTabItem();
			}
			if (!open)
				close_idx = i;
		}
		ImGui::EndTabBar();

		if (close_idx >= 0)
		{
			m_irc.open_tabs.erase(m_irc.open_tabs.begin() + close_idx);
			if (m_irc.active_tab >= (int)m_irc.open_tabs.size())
				m_irc.active_tab = (int)m_irc.open_tabs.size() - 1;
		}
	}
}

void Application::drawChatPane()
{
	if (m_irc.open_tabs.empty()
		|| m_irc.active_tab < 0
		|| m_irc.active_tab >= (int)m_irc.open_tabs.size())
	{
		ImGui::TextWrapped("Connect to a server from the Server menu or double-click a profile on the left.");
		return;
	}

	const OpenTab& tab = m_irc.open_tabs[(size_t)m_irc.active_tab];
	ServerSession& sess = m_irc.sessions[(size_t)tab.session_idx];
	ChannelState* ch = IrcState::getChannel(sess, tab.channel_idx);
	if (!ch)
		return;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
	for (const auto& msg : ch->messages)
	{
		ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		switch (msg.type)
		{
		case ChatMessageType::System:
			color = ImVec4(0.35f, 0.45f, 0.55f, 1.0f);
			break;
		case ChatMessageType::Join:
			color = ImVec4(0.2f, 0.55f, 0.25f, 1.0f);
			break;
		case ChatMessageType::Part:
		case ChatMessageType::Quit:
			color = ImVec4(0.7f, 0.35f, 0.2f, 1.0f);
			break;
		case ChatMessageType::Notice:
			color = ImVec4(0.55f, 0.35f, 0.7f, 1.0f);
			break;
		case ChatMessageType::Action:
			color = ImVec4(0.55f, 0.25f, 0.55f, 1.0f);
			break;
		default:
			break;
		}

		ImGui::PushStyleColor(ImGuiCol_Text, color);
		if (msg.type == ChatMessageType::System || msg.nick.empty())
			ImGui::TextWrapped("[%s] %s", msg.timestamp.c_str(), msg.text.c_str());
		else if (msg.type == ChatMessageType::Action)
			ImGui::TextWrapped("[%s] * %s %s", msg.timestamp.c_str(), msg.nick.c_str(), msg.text.c_str());
		else
			ImGui::TextWrapped("[%s] <%s> %s", msg.timestamp.c_str(), msg.nick.c_str(), msg.text.c_str());
		ImGui::PopStyleColor();
	}
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
		ImGui::SetScrollHereY(1.0f);
	ImGui::PopStyleVar();
}

void Application::drawUserList()
{
	ImGui::TextUnformatted("Users");
	ImGui::Separator();

	if (m_irc.open_tabs.empty()
		|| m_irc.active_tab < 0
		|| m_irc.active_tab >= (int)m_irc.open_tabs.size())
	{
		ImGui::TextDisabled("—");
		return;
	}

	const OpenTab& tab = m_irc.open_tabs[(size_t)m_irc.active_tab];
	ServerSession& sess = m_irc.sessions[(size_t)tab.session_idx];
	ChannelState* ch = IrcState::getChannel(sess, tab.channel_idx);
	if (!ch || tab.channel_idx < 0)
	{
		ImGui::TextDisabled("(no channel)");
		return;
	}

	ImGui::TextDisabled("%zu nick(s)", ch->users.size());
	ImGui::BeginChild("##UserList", ImVec2(0, 0), ImGuiChildFlags_None);
	for (const auto& user : ch->users)
	{
		std::string label = user.prefixes + user.nick;
		ImGui::PushID(user.nick.c_str());
		if (ImGui::Selectable(label.c_str()))
		{
			if (m_chatInput.empty())
				m_chatInput = user.nick + ": ";
			else
			{
				if (m_chatInput.back() != ' ')
					m_chatInput.push_back(' ');
				m_chatInput += user.nick + " ";
			}
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
}

ChannelState* Application::activeChatChannel()
{
	if (m_irc.open_tabs.empty()
		|| m_irc.active_tab < 0
		|| m_irc.active_tab >= (int)m_irc.open_tabs.size())
		return nullptr;
	const OpenTab& tab = m_irc.open_tabs[(size_t)m_irc.active_tab];
	if (tab.session_idx < 0 || tab.session_idx >= (int)m_irc.sessions.size())
		return nullptr;
	if (tab.channel_idx < 0)
		return nullptr;
	return IrcState::getChannel(m_irc.sessions[(size_t)tab.session_idx], tab.channel_idx);
}

std::vector<std::string> Application::nickCompletionCandidates(const std::string& prefix) const
{
	std::vector<std::string> out;
	if (m_irc.open_tabs.empty()
		|| m_irc.active_tab < 0
		|| m_irc.active_tab >= (int)m_irc.open_tabs.size())
		return out;
	const OpenTab& tab = m_irc.open_tabs[(size_t)m_irc.active_tab];
	if (tab.session_idx < 0 || tab.session_idx >= (int)m_irc.sessions.size() || tab.channel_idx < 0)
		return out;
	const ChannelState* ch = IrcState::getChannel(m_irc.sessions[(size_t)tab.session_idx], tab.channel_idx);
	if (!ch)
		return out;

	for (const auto& user : ch->users)
	{
		if (prefix.empty() || boost::istarts_with(user.nick, prefix))
			out.push_back(user.nick);
	}
	std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b)
		{
			return boost::ilexicographical_compare(a, b);
		});
	return out;
}

namespace
{
	bool isNickWordBoundary(char c)
	{
		return c == ' ' || c == '\t' || c == ',' || c == ';' || c == ':' || c == '!'
			|| c == '?' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}'
			|| c == '"' || c == '\'' || c == '<' || c == '>';
	}

	void locateNickWord(const char* buf, int cursor, int& word_start, int& word_end, bool& had_at)
	{
		word_end = cursor;
		word_start = cursor;
		had_at = false;
		while (word_start > 0 && !isNickWordBoundary(buf[word_start - 1]) && buf[word_start - 1] != '@')
			--word_start;
		if (word_start > 0 && buf[word_start - 1] == '@')
		{
			had_at = true;
			--word_start;
		}
	}

	void applyNickInsert(std::string& text, int word_start, int word_end, bool had_at, const std::string& nick)
	{
		std::string insert;
		if (had_at)
			insert = "@" + nick + " ";
		else if (word_start == 0)
			insert = nick + ": ";
		else
			insert = nick + " ";
		text.replace((size_t)word_start, (size_t)(word_end - word_start), insert);
	}
}

int Application::chatInputCallbackStub(ImGuiInputTextCallbackData* data)
{
	return static_cast<Application*>(data->UserData)->onChatInputCallback(data);
}

int Application::onChatInputCallback(ImGuiInputTextCallbackData* data)
{
	if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways)
	{
		m_chatCursorPos = data->CursorPos;
		return 0;
	}

	if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
	{
		if (!m_nickCompletionOpen || m_nickCompletionCandidates.empty())
			return 0;
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			m_nickCompletionSelected = (m_nickCompletionSelected
				+ (int)m_nickCompletionCandidates.size() - 1)
				% (int)m_nickCompletionCandidates.size();
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			m_nickCompletionSelected = (m_nickCompletionSelected + 1)
				% (int)m_nickCompletionCandidates.size();
		}
		return 0;
	}

	if (data->EventFlag != ImGuiInputTextFlags_CallbackCompletion)
		return 0;

	int word_start = 0;
	int word_end = 0;
	bool had_at = false;
	locateNickWord(data->Buf, data->CursorPos, word_start, word_end, had_at);

	const int prefix_start = had_at ? word_start + 1 : word_start;
	const std::string prefix(data->Buf + prefix_start, data->Buf + word_end);
	auto candidates = nickCompletionCandidates(prefix);
	if (candidates.empty())
	{
		m_nickCompletionOpen = false;
		return 0;
	}

	int pick = 0;
	if (m_nickCompletionOpen
		&& m_nickCompletionPrefix == prefix
		&& !m_nickCompletionCandidates.empty())
	{
		pick = (m_nickCompletionSelected + 1) % (int)candidates.size();
	}

	m_nickCompletionCandidates = std::move(candidates);
	m_nickCompletionSelected = pick;
	m_nickCompletionPrefix = prefix;
	m_nickCompletionWordStart = word_start;
	m_nickCompletionHadAt = had_at;
	m_nickCompletionOpen = m_nickCompletionCandidates.size() > 1;

	const std::string& nick = m_nickCompletionCandidates[(size_t)pick];
	std::string insert;
	if (had_at)
		insert = "@" + nick + " ";
	else if (word_start == 0)
		insert = nick + ": ";
	else
		insert = nick + " ";

	data->DeleteChars(word_start, word_end - word_start);
	data->InsertChars(data->CursorPos, insert.c_str());

	if (m_nickCompletionCandidates.size() == 1)
		m_nickCompletionOpen = false;

	return 0;
}

void Application::updateNickCompletionCandidates()
{
	if (!activeChatChannel())
	{
		m_nickCompletionOpen = false;
		m_nickCompletionCandidates.clear();
		return;
	}

	const int cursor = std::clamp(m_chatCursorPos, 0, (int)m_chatInput.size());
	int word_start = 0;
	int word_end = 0;
	bool had_at = false;
	locateNickWord(m_chatInput.c_str(), cursor, word_start, word_end, had_at);

	if (!had_at)
		return;

	const int prefix_start = word_start + 1;
	const std::string prefix = m_chatInput.substr((size_t)prefix_start,
		(size_t)std::max(0, word_end - prefix_start));

	auto candidates = nickCompletionCandidates(prefix);
	if (candidates.empty())
	{
		m_nickCompletionOpen = false;
		m_nickCompletionCandidates.clear();
		return;
	}

	if (prefix != m_nickCompletionPrefix)
		m_nickCompletionSelected = 0;
	m_nickCompletionCandidates = std::move(candidates);
	m_nickCompletionPrefix = prefix;
	m_nickCompletionWordStart = word_start;
	m_nickCompletionHadAt = true;
	m_nickCompletionOpen = true;
	if (m_nickCompletionSelected >= (int)m_nickCompletionCandidates.size())
		m_nickCompletionSelected = 0;
}

void Application::drawNickCompletionPopup()
{
	m_nickCompletionHovered = false;
	if (!m_nickCompletionOpen || m_nickCompletionCandidates.empty())
		return;

	const ImVec2 input_min = ImGui::GetItemRectMin();
	const float popup_w = std::max(180.0f, ImGui::GetItemRectSize().x);
	const float line_h = ImGui::GetTextLineHeightWithSpacing();
	const int visible = std::min(8, (int)m_nickCompletionCandidates.size());
	const float popup_h = visible * line_h + ImGui::GetStyle().WindowPadding.y * 2.0f;

	ImGui::SetNextWindowPos(ImVec2(input_min.x, input_min.y - popup_h - 4.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h), ImGuiCond_Always);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

	if (!ImGui::Begin("##NickCompletion", nullptr, flags))
	{
		ImGui::End();
		return;
	}

	m_nickCompletionHovered = ImGui::IsWindowHovered();
	for (int i = 0; i < (int)m_nickCompletionCandidates.size(); ++i)
	{
		const bool selected = (i == m_nickCompletionSelected);
		if (ImGui::Selectable(m_nickCompletionCandidates[(size_t)i].c_str(), selected))
		{
			const std::string& nick = m_nickCompletionCandidates[(size_t)i];
			const int cursor = std::clamp(m_chatCursorPos, 0, (int)m_chatInput.size());
			int ws = 0, we = 0;
			bool had_at = false;
			locateNickWord(m_chatInput.c_str(), cursor, ws, we, had_at);
			applyNickInsert(m_chatInput, ws, we, had_at, nick);
			m_nickCompletionOpen = false;
		}
		if (selected)
			ImGui::SetScrollHereY();
	}
	ImGui::End();
}

void Application::drawInputBar()
{
	if (m_irc.open_tabs.empty())
		return;

	ImGui::Separator();
	ImGui::SetNextItemWidth(-1);
	const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_CallbackCompletion
		| ImGuiInputTextFlags_CallbackHistory
		| ImGuiInputTextFlags_CallbackAlways;

	const bool submitted = ImGui::InputText("##ChatInput", &m_chatInput, flags,
		&Application::chatInputCallbackStub, this);
	const bool input_active = ImGui::IsItemActive();

	if (input_active)
		updateNickCompletionCandidates();

	drawNickCompletionPopup();

	if (!input_active && !m_nickCompletionHovered)
		m_nickCompletionOpen = false;

	if (input_active && m_nickCompletionOpen && ImGui::IsKeyPressed(ImGuiKey_Escape))
		m_nickCompletionOpen = false;

	if (submitted)
	{
		m_nickCompletionOpen = false;
		submitChatInput();
		ImGui::SetKeyboardFocusHere(-1);
	}
}

void Application::submitChatInput()
{
	if (m_chatInput.empty()
		|| m_irc.open_tabs.empty()
		|| m_irc.active_tab < 0
		|| m_irc.active_tab >= (int)m_irc.open_tabs.size())
		return;

	const OpenTab& tab = m_irc.open_tabs[(size_t)m_irc.active_tab];
	ServerSession& sess = m_irc.sessions[(size_t)tab.session_idx];
	ChannelState* ch = IrcState::getChannel(sess, tab.channel_idx);
	if (!ch)
		return;

	auto conn = connectionForSession(tab.session_idx);
	const std::string nick = sess.current_nick.empty() ? "me" : sess.current_nick;
	std::string input = m_chatInput;
	m_chatInput.clear();

	if (!input.empty() && input[0] == '/')
	{
		std::string cmd = input.substr(1);
		std::string args;
		const auto sp = cmd.find(' ');
		if (sp != std::string::npos)
		{
			args = cmd.substr(sp + 1);
			cmd = cmd.substr(0, sp);
		}
		boost::algorithm::to_lower(cmd);

		if (!conn)
		{
			IrcState::appendSystem(*ch, "Not connected.");
			return;
		}

		if (cmd == "join" && !args.empty())
		{
			std::string channel;
			std::string key;
			const auto rsp = args.find(' ');
			if (rsp == std::string::npos)
				channel = args;
			else
			{
				channel = args.substr(0, rsp);
				key = args.substr(rsp + 1);
				boost::algorithm::trim(key);
			}
			if (channel[0] != '#' && channel[0] != '&')
				channel = "#" + channel;
			RememberedChannel opts{ .name = channel, .key = key };
			for (const auto& cfg : m_irc.servers)
			{
				if (cfg.id != sess.config_id)
					continue;
				if (const auto* rc = IrcConfig::findRememberedChannel(cfg, channel))
				{
					opts = *rc;
					if (!key.empty())
						opts.key = key;
				}
				break;
			}
			joinChannel(tab.session_idx, channel, &opts);
		}
		else if (cmd == "part")
		{
			std::string channel = (tab.channel_idx >= 0) ? ch->name : "";
			std::string reason;
			if (!args.empty())
			{
				if (args[0] == '#' || args[0] == '&')
				{
					const auto rsp = args.find(' ');
					channel = args.substr(0, rsp);
					if (rsp != std::string::npos)
						reason = args.substr(rsp + 1);
				}
				else
					reason = args;
			}
			if (!channel.empty())
				conn->part(channel, reason);
		}
		else if (cmd == "msg" || cmd == "query")
		{
			const auto rsp = args.find(' ');
			if (rsp == std::string::npos)
				return;
			const std::string target = args.substr(0, rsp);
			const std::string text = args.substr(rsp + 1);
			conn->privmsg(target, text);
			ChannelState* q = IrcState::findOrCreateChannel(sess, target, true);
			IrcState::appendMessage(*q, ChatMessageType::Normal, nick, text, false);
			IrcState::ensureTab(m_irc, tab.session_idx, IrcState::channelIndex(sess, target));
			m_forceSelectTab = m_irc.active_tab;
		}
		else if (cmd == "me" && tab.channel_idx >= 0)
		{
			conn->privmsg(ch->name, std::string("\001ACTION ") + args + "\001");
			IrcState::appendMessage(*ch, ChatMessageType::Action, nick, args, false);
		}
		else if (cmd == "nick" && !args.empty())
		{
			conn->nick(args);
		}
		else if (cmd == "quit")
		{
			disconnectServer(tab.session_idx);
		}
		else if (cmd == "quote" || cmd == "raw")
		{
			conn->sendRaw(args);
		}
		else
		{
			// Pass through as raw IRC command
			conn->sendRaw(input.substr(1));
			IrcState::appendSystem(*ch, "→ " + input);
		}
		return;
	}

	if (tab.channel_idx < 0)
	{
		IrcState::appendSystem(*ch, "Cannot send to status — join a channel or use /msg");
		return;
	}
	if (!conn)
	{
		IrcState::appendSystem(*ch, "Not connected.");
		return;
	}

	conn->privmsg(ch->name, input);
	IrcState::appendMessage(*ch, ChatMessageType::Normal, nick, input, false);
}

void Application::connectToServer(const ServerConfig& cfg)
{
	// Refresh from saved profiles and mark currently open channels for auto-join.
	ServerConfig profile = cfg;
	for (const auto& s : m_irc.servers)
	{
		if (s.id == cfg.id)
		{
			profile = s;
			break;
		}
	}

	const int existing = IrcState::findSessionByConfig(m_irc, cfg.id);
	if (existing >= 0)
	{
		for (const auto& ch : m_irc.sessions[(size_t)existing].channels)
		{
			if (ch.is_query || ch.name.empty())
				continue;
			IrcConfig::rememberChannel(m_irc.servers, cfg.id, ch.name);
		}
		for (const auto& s : m_irc.servers)
		{
			if (s.id == cfg.id)
			{
				profile = s;
				break;
			}
		}
	}

	ServerConfig effective = IrcConfig::resolveForConnect(profile, m_irc.identity);
	if (effective.host.empty() || effective.nick.empty())
	{
		BOOST_LOG_TRIVIAL(error) << "Server host and nick are required (set Account → Identity or server nick)";
		return;
	}

	// Stop existing connection for this profile
	auto it = m_connections.find(cfg.id);
	if (it != m_connections.end() && it->second)
		it->second->stop();

	IrcState::createSession(m_irc, effective);
	auto conn = std::make_shared<IrcConnection>(m_ioContext, effective, m_ircEvents);
	m_connections[cfg.id] = conn;
	conn->start();

	std::string join_preview;
	for (size_t i = 0; i < effective.auto_join.size() && i < 5; ++i)
	{
		if (i) join_preview += ", ";
		join_preview += effective.auto_join[i];
	}
	if (effective.auto_join.size() > 5)
		join_preview += ", …";

	BOOST_LOG_TRIVIAL(info) << "Connecting to " << effective.host << ":" << effective.port
		<< " as " << effective.nick
		<< (effective.auth_method == IrcAuthMethod::SaslPlain ? " (SASL)"
			: effective.auth_method == IrcAuthMethod::NickServ ? " (NickServ)" : "")
		<< (join_preview.empty() ? "" : (" — auto-join: " + join_preview));
}

void Application::joinChannel(int session_idx, const std::string& channel, const RememberedChannel* opts)
{
	if (session_idx < 0 || session_idx >= (int)m_irc.sessions.size() || channel.empty())
		return;
	ServerSession& sess = m_irc.sessions[(size_t)session_idx];
	auto conn = connectionForSession(session_idx);
	if (!conn || !sess.connected)
	{
		IrcState::appendSystem(sess.status, "Not connected.");
		return;
	}

	RememberedChannel settings{ .name = channel };
	if (opts)
		settings = *opts;
	settings.name = channel;

	for (auto& cfg : m_irc.servers)
	{
		if (cfg.id != sess.config_id)
			continue;
		if (auto* rc = IrcConfig::upsertRememberedChannel(cfg, channel))
		{
			// Preserve existing ChanServ settings unless the caller provided overrides.
			if (opts)
			{
				if (!opts->key.empty())
					rc->key = opts->key;
				if (!opts->chanserv_password.empty())
				{
					rc->chanserv_password = opts->chanserv_password;
					rc->chanserv_identify = opts->chanserv_identify || !opts->chanserv_password.empty();
				}
				else if (opts->chanserv_identify)
					rc->chanserv_identify = true;
				if (opts->chanserv_op)
					rc->chanserv_op = true;
			}
			// Joining means we want this channel back on the next connect.
			rc->auto_join = true;
			settings = *rc;
		}
		IrcConfig::saveServers(m_irc.servers, nullptr);
		break;
	}

	conn->upsertRememberedChannel(settings);
	conn->join(channel, settings.key);
	IrcState::appendSystem(sess.status, "Joining " + channel + "...");
}

void Application::saveRememberedChannelSettings(const std::string& server_id, const RememberedChannel& channel)
{
	if (server_id.empty() || channel.name.empty())
		return;
	for (auto& cfg : m_irc.servers)
	{
		if (cfg.id != server_id)
			continue;
		if (auto* rc = IrcConfig::upsertRememberedChannel(cfg, channel.name))
			*rc = channel;
		IrcConfig::saveServers(m_irc.servers, nullptr);
		if (auto it = m_connections.find(server_id); it != m_connections.end() && it->second)
			it->second->upsertRememberedChannel(channel);
		BOOST_LOG_TRIVIAL(info) << "Saved channel options for " << channel.name;
		return;
	}
}

void Application::rememberJoinedChannel(const std::string& server_id, const std::string& channel)
{
	if (IrcConfig::rememberChannel(m_irc.servers, server_id, channel))
		BOOST_LOG_TRIVIAL(info) << "Remembered channel " << channel << " (auto-join on connect)";
}

void Application::disconnectServer(int session_idx)
{
	if (session_idx < 0 || session_idx >= (int)m_irc.sessions.size())
		return;
	const std::string id = m_irc.sessions[(size_t)session_idx].config_id;
	auto it = m_connections.find(id);
	if (it != m_connections.end() && it->second)
	{
		it->second->stop();
		m_connections.erase(it);
	}
	IrcState::disconnectSession(m_irc, session_idx);
}

std::shared_ptr<IrcConnection> Application::connectionForSession(int session_idx)
{
	if (session_idx < 0 || session_idx >= (int)m_irc.sessions.size())
		return nullptr;
	auto it = m_connections.find(m_irc.sessions[(size_t)session_idx].config_id);
	if (it == m_connections.end())
		return nullptr;
	return it->second;
}

bool Application::isChannelTarget(const std::string& target) const
{
	return !target.empty() && (target[0] == '#' || target[0] == '&' || target[0] == '+' || target[0] == '!');
}

void Application::pollIrcEvents()
{
	if (!m_ircEvents)
		return;
	for (const auto& ev : m_ircEvents->drain())
		handleIrcEvent(ev);
}

void Application::handleIrcEvent(const IrcNetEvent& ev)
{
	const int si = IrcState::findSessionByConfig(m_irc, ev.config_id);
	if (si < 0)
		return;
	ServerSession& sess = m_irc.sessions[(size_t)si];

	auto markUnreadIfNeeded = [&](ChannelState& ch)
		{
			const bool viewing = (m_irc.selected_tree_session == si
				&& m_irc.selected_tree_channel >= 0
				&& IrcState::getChannel(sess, m_irc.selected_tree_channel) == &ch);
			if (!viewing)
				ch.unread = true;
		};

	switch (ev.type)
	{
	case IrcNetEventType::Status:
	case IrcNetEventType::Numeric:
		IrcState::appendSystem(sess.status, ev.text);
		break;

	case IrcNetEventType::Connected:
		sess.connecting = true;
		IrcState::appendSystem(sess.status, ev.text);
		break;

	case IrcNetEventType::Registered:
		sess.connecting = false;
		sess.connected = true;
		if (!ev.nick.empty())
		{
			sess.current_nick = ev.nick;
			IrcConfig::rememberNick(m_irc.identity, ev.nick);
		}
		IrcState::appendSystem(sess.status, "Registered as " + sess.current_nick
			+ (ev.text.empty() ? "" : (" — " + ev.text)));
		break;

	case IrcNetEventType::Disconnected:
	case IrcNetEventType::Error:
		sess.connected = false;
		sess.connecting = false;
		IrcState::appendSystem(sess.status, ev.text);
		m_connections.erase(ev.config_id);
		break;

	case IrcNetEventType::Join:
	{
		ChannelState* ch = IrcState::findOrCreateChannel(sess, ev.channel, false);
		if (boost::iequals(ev.nick, sess.current_nick))
		{
			IrcState::appendSystem(*ch, "Joined " + ev.channel);
			IrcState::addOrUpdateUser(*ch, ChannelUser{ .nick = ev.nick });
			const int ci = IrcState::channelIndex(sess, ev.channel);
			IrcState::ensureTab(m_irc, si, ci);
			m_forceSelectTab = m_irc.active_tab;
			rememberJoinedChannel(sess.config_id, ev.channel);
		}
		else
		{
			IrcState::appendMessage(*ch, ChatMessageType::Join, ev.nick, "joined " + ev.channel, false);
			IrcState::addOrUpdateUser(*ch, ChannelUser{ .nick = ev.nick });
			IrcState::sortUsers(*ch);
		}
		break;
	}

	case IrcNetEventType::Part:
	{
		int ci = IrcState::channelIndex(sess, ev.channel);
		if (ci < 0)
			break;
		ChannelState& ch = sess.channels[(size_t)ci];
		if (boost::iequals(ev.nick, sess.current_nick))
		{
			IrcState::appendSystem(ch, "You left " + ev.channel
				+ (ev.text.empty() ? "" : (" (" + ev.text + ")")));
			if (IrcConfig::forgetChannelAutoJoin(m_irc.servers, sess.config_id, ev.channel))
				BOOST_LOG_TRIVIAL(info) << "Disabled auto-join for " << ev.channel;
		}
		else
		{
			IrcState::appendMessage(ch, ChatMessageType::Part, ev.nick,
				"left" + (ev.text.empty() ? "" : (" (" + ev.text + ")")), false);
			IrcState::removeUser(ch, ev.nick);
		}
		break;
	}

	case IrcNetEventType::Quit:
		for (auto& ch : sess.channels)
		{
			bool present = false;
			for (const auto& u : ch.users)
			{
				if (boost::iequals(u.nick, ev.nick))
				{
					present = true;
					break;
				}
			}
			if (!present)
				continue;
			IrcState::removeUser(ch, ev.nick);
			IrcState::appendMessage(ch, ChatMessageType::Quit, ev.nick,
				"quit" + (ev.text.empty() ? "" : (" (" + ev.text + ")")), false);
		}
		break;

	case IrcNetEventType::NickChange:
		if (boost::iequals(ev.nick, sess.current_nick))
		{
			sess.current_nick = ev.text;
			IrcConfig::rememberNick(m_irc.identity, ev.text);
		}
		for (auto& ch : sess.channels)
		{
			bool present = false;
			for (const auto& u : ch.users)
			{
				if (boost::iequals(u.nick, ev.nick))
				{
					present = true;
					break;
				}
			}
			if (!present)
				continue;
			IrcState::renameUser(ch, ev.nick, ev.text);
			IrcState::appendMessage(ch, ChatMessageType::Nick, ev.nick, "is now known as " + ev.text, false);
			IrcState::sortUsers(ch);
		}
		IrcState::appendSystem(sess.status, ev.nick + " is now known as " + ev.text);
		break;

	case IrcNetEventType::Topic:
	{
		ChannelState* ch = IrcState::findOrCreateChannel(sess, ev.channel, false);
		IrcState::appendSystem(*ch, "Topic: " + ev.text);
		break;
	}

	case IrcNetEventType::Names:
	{
		const std::string key = IrcState::channelKey(ev.channel);
		auto& pending = sess.pending_names[key];
		for (const auto& tok : ev.nicks)
		{
			ChannelUser user = IrcState::parseNamespacedNick(tok);
			if (user.nick.empty())
				continue;
			bool found = false;
			for (auto& existing : pending)
			{
				if (boost::iequals(existing.nick, user.nick))
				{
					existing = user;
					found = true;
					break;
				}
			}
			if (!found)
				pending.push_back(std::move(user));
		}
		break;
	}

	case IrcNetEventType::NamesEnd:
	{
		ChannelState* ch = IrcState::findOrCreateChannel(sess, ev.channel, false);
		const std::string key = IrcState::channelKey(ev.channel);
		auto it = sess.pending_names.find(key);
		if (it != sess.pending_names.end())
		{
			ch->users = std::move(it->second);
			sess.pending_names.erase(it);
			IrcState::sortUsers(*ch);
		}
		break;
	}

	case IrcNetEventType::WhoEntry:
	{
		ChannelState* ch = IrcState::findOrCreateChannel(sess, ev.channel, false);
		IrcState::addOrUpdateUser(*ch, ChannelUser{ .nick = ev.nick, .prefixes = ev.text });
		IrcState::sortUsers(*ch);
		break;
	}

	case IrcNetEventType::ListStart:
		sess.channel_list.clear();
		sess.channel_list_loading = true;
		IrcState::appendSystem(sess.status, "Receiving channel list...");
		break;

	case IrcNetEventType::ListEntry:
		sess.channel_list.push_back(ChannelListEntry{
			.name = ev.channel,
			.users = ev.user_count,
			.topic = ev.text,
		});
		break;

	case IrcNetEventType::ListEnd:
		sess.channel_list_loading = false;
		std::sort(sess.channel_list.begin(), sess.channel_list.end(),
			[](const ChannelListEntry& a, const ChannelListEntry& b) { return a.users > b.users; });
		IrcState::appendSystem(sess.status,
			"Channel list complete (" + std::to_string(sess.channel_list.size()) + " channels)");
		break;

	case IrcNetEventType::Privmsg:
	case IrcNetEventType::Notice:
	{
		const bool action = (ev.type == IrcNetEventType::Privmsg && ev.numeric == 1);
		const bool notice = (ev.type == IrcNetEventType::Notice);
		std::string target = ev.channel;
		bool query = false;
		if (!isChannelTarget(target))
		{
			// PRIVMSG to us — open query with sender; NOTICE to us may go status
			if (boost::iequals(target, sess.current_nick))
			{
				target = ev.nick;
				query = true;
			}
		}

		ChannelState* ch = nullptr;
		if (notice && !isChannelTarget(ev.channel) && !query)
		{
			ch = &sess.status;
		}
		else
		{
			ch = IrcState::findOrCreateChannel(sess, target, query || !isChannelTarget(target));
			if (query)
			{
				const int ci = IrcState::channelIndex(sess, target);
				if (IrcState::findTab(m_irc, si, ci) < 0)
					IrcState::ensureTab(m_irc, si, ci);
			}
		}

		ChatMessageType mtype = notice ? ChatMessageType::Notice
			: (action ? ChatMessageType::Action : ChatMessageType::Normal);
		IrcState::appendMessage(*ch, mtype, ev.nick, ev.text, false);
		markUnreadIfNeeded(*ch);
		break;
	}
	}
}

void Application::drawConsolePane()
{
	if (!m_showConsole)
		return;

	ImGui::Separator();
	if (ImGui::ArrowButton("##console_toggle", m_consoleExpanded ? ImGuiDir_Down : ImGuiDir_Right))
		m_consoleExpanded = !m_consoleExpanded;
	ImGui::SameLine();
	ImGui::TextUnformatted("Console");
	ImGui::SameLine();
	ImGui::TextDisabled("(drag edge below to resize when expanded)");

	if (!m_consoleExpanded)
		return;

	ImGui::Button("##hsplit_console", ImVec2(-1, 4));
	if (ImGui::IsItemActive())
	{
		m_consoleHeight -= ImGui::GetIO().MouseDelta.y;
		m_consoleHeight = std::clamp(m_consoleHeight, 80.0f, m_imHeight * 0.6f);
	}

	ImGui::BeginChild("##ConsolePane", ImVec2(0, m_consoleHeight), ImGuiChildFlags_Borders);
	if (m_AppConsole)
		m_AppConsole->DrawEmbedded(true);
	ImGui::EndChild();
}
