#ifndef application_h__
#define application_h__
#include "boost/leaf.hpp"
#include "console.h"
#include "irc_state.h"
#include "irc_connection.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <map>
#include <vector>
#include <thread>
#include <atomic>

#include "util.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "glad.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <stdio.h>
#include <GLFW/glfw3.h>

struct AppConfig
{
	std::string AppName = "ImIRC Client";
	std::string Theme = "Light";
	uint8_t LogSeverity = 0;
	int WindowWidth = 1024;
	int WindowHeight = 768;
	float HighDPIscaleFactor = 1.0f;
	float BackgroundColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::string DefaultFont;
	std::string DefaultFontBold;
	bool FullScreen = false;
};

struct MenuItem
{
	GLuint resource = 0;
	std::string title;
	float width = 0;
	float height = 0;
	int order = 0;
};

class Application
{
public:
	Application() = default;
	~Application();

	boost::leaf::result<void> Init(int argc, char* argv[], std::string appTitle = "");
	boost::leaf::result<int> AppLoop(bool& running);
	boost::leaf::result<void> render();

	enum class GLFWErrc
	{
		glfw_init_failed = 1,
		glfw_window_create_failed
	};
	enum class GLADErrc
	{
		glad_init_failed = 1,
	};
	enum class ImGUIErrc
	{
		glfw_init_failed = 1,
		opengl_init_failed,
	};
	enum class AppErrc
	{
		CleanExit,
	};

protected:
	boost::leaf::result<void> initGLFW();
	boost::leaf::result<void> initGLAD();
	boost::leaf::result<void> initImGUI();
	boost::leaf::result<void> initLogging();
	boost::leaf::result<void> drawGUI();

	void drawMainMenu();
	void drawServerDialog();
	void drawJoinDialog();
	void drawIdentityDialog();
	void drawChannelOptionsDialog();
	void drawChannelBrowser();
	void openChannelBrowser(int session_idx = -1);
	void openIdentityDialog();
	void openChannelOptionsDialog(int session_idx, const std::string& channel);
	void refreshChannelList();
	void rememberJoinedChannel(const std::string& server_id, const std::string& channel);
	void saveRememberedChannelSettings(const std::string& server_id, const RememberedChannel& channel);
	void joinChannel(int session_idx, const std::string& channel, const RememberedChannel* opts = nullptr);
	void drawServerChannelTree();
	void drawChannelTabs();
	void drawChatPane();
	void drawUserList();
	void drawInputBar();
	void drawConsolePane();
	void openServerDialog(int edit_index = -1);
	void submitChatInput();
	void drawNickCompletionPopup();
	void updateNickCompletionCandidates();
	int onChatInputCallback(ImGuiInputTextCallbackData* data);
	static int chatInputCallbackStub(ImGuiInputTextCallbackData* data);
	ChannelState* activeChatChannel();
	std::vector<std::string> nickCompletionCandidates(const std::string& prefix) const;

	void connectToServer(const ServerConfig& cfg);
	void disconnectServer(int session_idx);
	void pollIrcEvents();
	void handleIrcEvent(const IrcNetEvent& ev);
	std::shared_ptr<IrcConnection> connectionForSession(int session_idx);
	bool isChannelTarget(const std::string& target) const;

	void processLoop();
	float m_imWidth = 0;
	float m_imHeight = 0;

private:
	AppConfig m_appConfig;
	GLFWwindow* m_glfWindow = nullptr;

	AppConsole* m_AppConsole = nullptr;
	std::map<std::string, std::map<float, ImFont*>> m_FontsMap;

	IrcClientState m_irc;
	std::shared_ptr<IrcEventQueue> m_ircEvents;
	std::unordered_map<std::string, std::shared_ptr<IrcConnection>> m_connections;

	bool m_showServerDialog = false;
	bool m_showJoinDialog = false;
	bool m_showChannelBrowser = false;
	bool m_showIdentityDialog = false;
	bool m_showChannelOptionsDialog = false;
	bool m_showConsole = true;
	int m_editServerIndex = -1;
	int m_channelBrowserSession = -1;
	int m_channelBrowserSelected = -1;
	int m_channelBrowserMinUsers = 0;
	int m_identityAuthIndex = 1;
	int m_serverAuthIndex = 0;
	int m_channelOptionsSession = -1;
	ServerConfig m_serverDraft;
	IrcIdentity m_identityDraft;
	RememberedChannel m_channelOptionsDraft;
	RememberedChannel m_joinOptionsDraft;
	std::string m_joinDraft;
	std::string m_chatInput;
	std::string m_autoJoinDraft;
	std::string m_channelBrowserFilter;
	std::string m_channelBrowserMask;
	std::string m_newRememberedChannel;
	float m_leftPaneWidth = 220.0f;
	float m_rightPaneWidth = 160.0f;
	float m_consoleHeight = 180.0f;
	bool m_consoleExpanded = true;
	int m_forceSelectTab = -1;

	int m_chatCursorPos = 0;
	bool m_nickCompletionOpen = false;
	bool m_nickCompletionHovered = false;
	int m_nickCompletionSelected = 0;
	int m_nickCompletionWordStart = 0;
	bool m_nickCompletionHadAt = false;
	std::vector<std::string> m_nickCompletionCandidates;
	std::string m_nickCompletionPrefix;

	boost::asio::io_context m_ioContext;
	std::vector<std::thread> m_workerThreads;
	std::unique_ptr<boost::asio::executor_work_guard<decltype(m_ioContext.get_executor())>> m_dummyWork;
};
#endif // application_h__
