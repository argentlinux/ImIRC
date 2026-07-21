#ifndef console_h__
#define console_h__
#include "imgui.h"

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <vector>
#include <string>


class AppConsole : public boost::log::sinks::basic_formatted_sink_backend<char,
	boost::log::sinks::combine_requirements<
	boost::log::sinks::synchronized_feeding,
	boost::log::sinks::flushing,
	boost::log::sinks::formatted_records
	>::type
>, std::enable_shared_from_this<AppConsole>
{
	/*
		TODO: add hook to some sort of external command list/executor 
	*/
public:
	typedef std::map<std::string, std::function<void(std::string)>> CommandsMap;
	AppConsole(CommandsMap commands);
	~AppConsole();
	void consume(boost::log::record_view const& rec, string_type const& formatted_message);

	void flush();
	void stop();
	// Portable helpers
	static int   Stricmp(const char* s1, const char* s2) { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
	static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
	static char* Strdup(const char* s) { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = malloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
	static void  Strtrim(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

	void ClearLog();
	void AddLog(std::string logline, ImVec4 color = {0,0,0,1});
	ImVec2 Draw(const char* title, bool* p_open);
	// Draw console contents inside an already-open ImGui window/child (no Begin/End).
	void DrawEmbedded(bool show_input = true);
	void ExecCommand(const char* command_line);
	int TextEditCallback(ImGuiInputTextCallbackData* data);

	struct LogItem
	{
		ImVec4 color;
		std::string message;
	};
private:
	void write_data();

	size_t                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
	ImGuiTextFilter       Filter;
	bool                  AutoScroll;
	bool                  ScrollToBottom;
	std::vector<LogItem> m_Items;
	//std::vector<std::string> m_Commands;
	std::vector<std::string> m_History;
	CommandsMap m_Commands;
	char m_InputBuffer[256] = { 0 };
};
#endif // console_h__
