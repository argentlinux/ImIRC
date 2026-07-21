#include "console.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/algorithm/string.hpp>

AppConsole::AppConsole(CommandsMap commands)
{
	using namespace boost::log;
	typedef sinks::synchronous_sink< AppConsole > sink_t;
	ClearLog();
	HistoryPos = -1;

	// help, history and clear are in-built
	m_Commands = commands;
	m_Commands["HELP"] = [&](std::string cmd) 
	{
		AddLog("> Commands:");
		for (auto& it : m_Commands)
		{
			std::stringstream ss;
			ss << "- " << it.first;
			AddLog(ss.str());
		}
	};
	m_Commands["HISTORY"] = [&](std::string cmd) 
	{
		size_t first = m_History.size() - 10;
		for (size_t i = first > 0 ? first : 0; i < m_History.size(); i++)
		{
			std::stringstream ss;
			ss << i << ": " << m_History[i];
			AddLog(ss.str());
		}
	};
	m_Commands["CLEAR"] = [&](std::string cmd) 
	{
		ClearLog();
	};

	AutoScroll = true;
	ScrollToBottom = false;

	boost::shared_ptr< boost::log::core > core = boost::log::core::get();

	boost::shared_ptr< AppConsole> backend(this);
	boost::shared_ptr< sink_t > sink(new sink_t(backend));
	sink->set_formatter
	(
		boost::log::expressions::format("[%1%] [%2%] [TID: %3%] %4%")
		% boost::log::expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
		% boost::log::trivial::severity
		% boost::log::expressions::attr<boost::log::attributes::current_thread_id::value_type>("ThreadID")
		//% boost::log::expressions::attr< unsigned int >("ThreadID")
		% boost::log::expressions::smessage
	);
	core->add_sink(sink);
	boost::log::core::get()->set_filter(
		boost::log::trivial::severity >= boost::log::trivial::info
	);

}

AppConsole::~AppConsole()
{

}

void AppConsole::consume(boost::log::record_view const& rec, string_type const& formatted_message)
{
	ImVec4 color = { 0,0,0,1 };
	for (auto& it : rec.attribute_values())
	{
		std::string attrname = it.first.string();
		if (attrname == "Severity")
		{
			auto severity = it.second.extract<boost::log::trivial::severity_level>();
			if (severity == boost::log::trivial::severity_level::debug)
			{
				color = { 0.0396f, 0.800f, 0.990f , 1.0f };
			}

			else if (severity == boost::log::trivial::severity_level::trace)
			{
			}
			else if (severity == boost::log::trivial::severity_level::info)
			{
			}
			else if (severity == boost::log::trivial::severity_level::warning)
			{
				color = { 0.920f, 0.815f, 0.0184f , 1.0f };
			}
			else if (severity == boost::log::trivial::severity_level::error)
			{
				color = { 0.920f, 0.469f, 0.0184f , 1.0f };
			}
			else if (severity == boost::log::trivial::severity_level::fatal)
			{
				color = { 1.0f, 0.0f, 0.0f , 1.0f };
			}
			break;
		}
	}
	AddLog(formatted_message, color);
}

void AppConsole::flush()
{

}

void AppConsole::stop()
{

}

void AppConsole::ClearLog()
{
	m_Items.clear();
}

void AppConsole::AddLog(std::string logline, ImVec4 color)
{
	m_Items.push_back({ .color = color, .message=logline });
}

void AppConsole::DrawEmbedded(bool show_input)
{
	if (ImGui::BeginPopup("Options"))
	{
		ImGui::Checkbox("Auto-scroll", &AutoScroll);
		ImGui::EndPopup();
	}

	if (ImGui::Button("Options"))
		ImGui::OpenPopup("Options");

	ImGui::SameLine();
	Filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
	ImGui::SameLine();

	if (ImGui::SmallButton("Clear"))
		ClearLog();
	ImGui::SameLine();
	bool copy_to_clipboard = ImGui::SmallButton("Copy");

	ImGui::Separator();

	const float footer_height_to_reserve = show_input
		? (ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing())
		: 0.0f;
	ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	if (ImGui::BeginPopupContextWindow())
	{
		if (ImGui::Selectable("Clear")) ClearLog();
		ImGui::EndPopup();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
	if (copy_to_clipboard)
		ImGui::LogToClipboard();
	for (int i = 0; i < (int)m_Items.size(); i++)
	{
		const std::string& item = m_Items[(size_t)i].message;
		if (!Filter.PassFilter(item.c_str()))
			continue;

		ImGui::PushStyleColor(ImGuiCol_Text, m_Items[(size_t)i].color);
		ImGui::TextUnformatted(item.c_str());
		ImGui::PopStyleColor();
	}
	if (copy_to_clipboard)
		ImGui::LogFinish();

	if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
		ImGui::SetScrollHereY(1.0f);
	ScrollToBottom = false;

	ImGui::PopStyleVar();
	ImGui::EndChild();

	if (!show_input)
		return;

	ImGui::Separator();

	bool reclaim_focus = false;
	ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_CallbackCompletion
		| ImGuiInputTextFlags_CallbackHistory;
	if (ImGui::InputText("Input", m_InputBuffer, IM_ARRAYSIZE(m_InputBuffer), input_text_flags,
		[](ImGuiInputTextCallbackData* data) -> int
		{
			AppConsole* appc = (AppConsole*)data->UserData;
			return appc->TextEditCallback(data);
		}, (void*)this))
	{
		char* s = m_InputBuffer;
		Strtrim(s);
		if (s[0])
			ExecCommand(s);
		strcpy(s, "");
		reclaim_focus = true;
	}

	ImGui::SetItemDefaultFocus();
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1);
}

ImVec2 AppConsole::Draw(const char* title, bool* p_open)
{
	if (!ImGui::Begin(title, p_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
	{
		ImVec2 size = ImGui::GetWindowSize();
		ImGui::End();
		return size;
	}
	ImVec2 size = ImGui::GetWindowSize();
	DrawEmbedded(true);
	ImGui::End();
	return size;
}

void AppConsole::ExecCommand(const char* command_line)
{
	{
		std::stringstream ss;
		ss << "# " << command_line;
		AddLog(ss.str());
	}
	// Insert into history. First find match and delete it so it can be pushed to the back.
	// This isn't trying to be smart or optimal.
	HistoryPos = -1;
	for (int i = (int)m_History.size() - 1; i >= 0; i--)
		if (Stricmp(m_History[i].c_str(), command_line) == 0)
		{

			m_History.erase(m_History.begin() + i);
			break;
		}
	m_History.push_back(Strdup(command_line));

	
		bool foundCmd = false;
		for (auto& it : m_Commands)
		{
			std::string cmdLine(command_line);
			boost::to_upper(cmdLine);
			std::string cmdStr = it.first;
			boost::to_upper(cmdStr);
			if (cmdLine.rfind(cmdStr, 0) != std::string::npos)
			{
				if(it.second)
					it.second(cmdLine);
				foundCmd = true;
				break;
			}
		}
		if(!foundCmd)
		{
			std::stringstream ss;
			ss << "> Unknown command: " << command_line;
			AddLog(ss.str());
		}
		
	

	// On command input, we scroll to bottom even if AutoScroll==false
	ScrollToBottom = true;
}

int AppConsole::TextEditCallback(ImGuiInputTextCallbackData* data)
{
	//AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackCompletion:
	{
		// Example of TEXT COMPLETION

		// Locate beginning of current word
		const char* word_end = data->Buf + data->CursorPos;
		const char* word_start = word_end;
		while (word_start > data->Buf)
		{
			const char c = word_start[-1];
			if (c == ' ' || c == '\t' || c == ',' || c == ';')
				break;
			word_start--;
		}

		// Build a list of candidates
		std::vector<std::string> candidates;
		for (auto& it: m_Commands)
		{
			std::string partialWord(word_start, (int)(word_end - word_start));
			boost::to_upper(partialWord);
			std::string cmdStr = it.first;
			boost::to_upper(cmdStr);
			if(cmdStr.rfind(partialWord,0) != std::string::npos)
				candidates.push_back(it.first);
			
		}
		if (candidates.size() == 0)
		{
			// No match
			//AddLog("No match for \n");
		}
		else if (candidates.size() == 1)
		{
			// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
			data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
			data->InsertChars(data->CursorPos, candidates[0].c_str());
			data->InsertChars(data->CursorPos, " ");
		}
		else
		{
			// Multiple matches. Complete as much as we can..
			// So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
			int match_len = (int)(word_end - word_start);
			for (;;)
			{
				int c = 0;
				bool all_candidates_matches = true;
				for (int i = 0; i < candidates.size() && all_candidates_matches; i++)
					if (i == 0)
						c = toupper(candidates[i][match_len]);
					else if (c == 0 || c != toupper(candidates[i][match_len]))
						all_candidates_matches = false;
				if (!all_candidates_matches)
					break;
				match_len++;
			}

			if (match_len > 0)
			{
				data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
				data->InsertChars(data->CursorPos, candidates[0].c_str(), candidates[0].c_str() + match_len);
			}

			// List matches
			AddLog("> Possible matches:\n");
			for (int i = 0; i < candidates.size(); i++)
			{
				std::stringstream ss; 
				ss << "- " << candidates[i];
				AddLog(ss.str());
			}
				
		}

		break;
	}
	case ImGuiInputTextFlags_CallbackHistory:
	{
		// Example of HISTORY
		const size_t prev_history_pos = HistoryPos;
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (HistoryPos == -1)
				HistoryPos = m_History.size() - 1;
			else if (HistoryPos > 0)
				HistoryPos--;
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			if (HistoryPos != -1)
				if (++HistoryPos >= m_History.size())
					HistoryPos = -1;
		}

		// A better implementation would preserve the data on the current input line along with cursor position.
		if (prev_history_pos != HistoryPos)
		{
			if ((int64_t)HistoryPos < 0)
			{
				HistoryPos = 0;
			}
			const char* history_str = (HistoryPos >= 0) ? m_History[HistoryPos].c_str() : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, history_str);
		}
	}
	}
	return 0;
}

void AppConsole::write_data()
{

}

