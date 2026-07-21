#include "irc_state.h"

#include "json.hpp"
#include "boost/dll/runtime_symbol_info.hpp"
#include "boost/log/trivial.hpp"
#include "boost/algorithm/string.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

using json = nlohmann::json;

static const char* authMethodToString(IrcAuthMethod m)
{
	switch (m)
	{
	case IrcAuthMethod::None: return "none";
	case IrcAuthMethod::NickServ: return "nickserv";
	case IrcAuthMethod::SaslPlain: return "sasl";
	default: return "inherit";
	}
}

static IrcAuthMethod authMethodFromString(const std::string& s)
{
	if (s == "none") return IrcAuthMethod::None;
	if (s == "nickserv") return IrcAuthMethod::NickServ;
	if (s == "sasl" || s == "sasl_plain") return IrcAuthMethod::SaslPlain;
	return IrcAuthMethod::Inherit;
}

void to_json(json& j, const RememberedChannel& c)
{
	j = json{
		{ "name", c.name },
		{ "key", c.key },
		{ "chanserv_password", c.chanserv_password },
		{ "chanserv_identify", c.chanserv_identify },
		{ "chanserv_op", c.chanserv_op },
		{ "auto_join", c.auto_join },
	};
}

void from_json(const json& j, RememberedChannel& c)
{
	if (j.is_string())
	{
		c = RememberedChannel{};
		c.name = j.get<std::string>();
		return;
	}
	c.name = j.value("name", "");
	c.key = j.value("key", "");
	c.chanserv_password = j.value("chanserv_password", "");
	c.chanserv_identify = j.value("chanserv_identify", !c.chanserv_password.empty());
	c.chanserv_op = j.value("chanserv_op", false);
	c.auto_join = j.value("auto_join", true);
}

void to_json(json& j, const ServerConfig& s)
{
	j = json{
		{ "id", s.id },
		{ "name", s.name },
		{ "host", s.host },
		{ "port", s.port },
		{ "use_tls", s.use_tls },
		{ "nick", s.nick },
		{ "alt_nick", s.alt_nick },
		{ "account", s.account },
		{ "username", s.username },
		{ "realname", s.realname },
		{ "password", s.password },
		{ "nickserv_password", s.nickserv_password },
		{ "auth_method", authMethodToString(s.auth_method) },
		{ "ghost_on_nick_taken", s.ghost_on_nick_taken },
		{ "auto_join", s.auto_join },
		{ "remembered_channels", s.remembered_channels },
		{ "auto_join_remembered", s.auto_join_remembered },
	};
}

void from_json(const json& j, ServerConfig& s)
{
	s.id = j.value("id", "");
	s.name = j.value("name", "");
	s.host = j.value("host", "irc.libera.chat");
	s.port = j.value("port", 6667);
	s.use_tls = j.value("use_tls", false);
	s.nick = j.value("nick", "");
	s.alt_nick = j.value("alt_nick", "");
	s.account = j.value("account", "");
	s.username = j.value("username", "");
	s.realname = j.value("realname", "");
	s.password = j.value("password", "");
	s.nickserv_password = j.value("nickserv_password", "");
	if (j.contains("auth_method"))
	{
		if (j["auth_method"].is_string())
			s.auth_method = authMethodFromString(j["auth_method"].get<std::string>());
		else
			s.auth_method = static_cast<IrcAuthMethod>(j.value("auth_method", 0));
	}
	else
		s.auth_method = IrcAuthMethod::Inherit;
	s.ghost_on_nick_taken = j.value("ghost_on_nick_taken", true);
	s.auto_join = j.value("auto_join", std::vector<std::string>{});
	s.remembered_channels.clear();
	if (j.contains("remembered_channels") && j["remembered_channels"].is_array())
	{
		for (const auto& item : j["remembered_channels"])
		{
			RememberedChannel ch;
			from_json(item, ch);
			if (!ch.name.empty())
				s.remembered_channels.push_back(std::move(ch));
		}
	}
	s.auto_join_remembered = j.value("auto_join_remembered", true);
	if (s.id.empty())
		s.id = IrcConfig::makeId();
	if (s.name.empty())
		s.name = s.host;
}

void to_json(json& j, const IrcIdentity& id)
{
	j = json{
		{ "nick", id.nick },
		{ "alt_nick", id.alt_nick },
		{ "account", id.account },
		{ "username", id.username },
		{ "realname", id.realname },
		{ "nickserv_password", id.nickserv_password },
		{ "default_auth", authMethodToString(id.default_auth) },
		{ "ghost_on_nick_taken", id.ghost_on_nick_taken },
		{ "nick_history", id.nick_history },
	};
}

void from_json(const json& j, IrcIdentity& id)
{
	id.nick = j.value("nick", "imirc_user");
	id.alt_nick = j.value("alt_nick", "");
	id.account = j.value("account", "");
	id.username = j.value("username", "imirc");
	id.realname = j.value("realname", "ImIRC User");
	id.nickserv_password = j.value("nickserv_password", "");
	if (j.contains("default_auth"))
	{
		if (j["default_auth"].is_string())
			id.default_auth = authMethodFromString(j["default_auth"].get<std::string>());
		else
			id.default_auth = static_cast<IrcAuthMethod>(j.value("default_auth", 2));
	}
	if (id.default_auth == IrcAuthMethod::Inherit)
		id.default_auth = IrcAuthMethod::NickServ;
	id.ghost_on_nick_taken = j.value("ghost_on_nick_taken", true);
	id.nick_history = j.value("nick_history", std::vector<std::string>{});
}

std::string IrcConfig::configDir()
{
	namespace fs = std::filesystem;

	if (const char* override_dir = std::getenv("IMIRC_CONFIG_DIR"))
	{
		if (override_dir[0] != '\0')
		{
			fs::path dir = override_dir;
			std::error_code ec;
			fs::create_directories(dir, ec);
			return dir.string();
		}
	}

	auto tryDir = [](const fs::path& dir) -> bool
	{
		std::error_code ec;
		fs::create_directories(dir, ec);
		if (ec)
			return false;
		const fs::path probe = dir / ".imirc_write_test";
		{
			std::ofstream out(probe, std::ios::trunc);
			if (!out)
				return false;
			out << "ok";
			if (!out)
				return false;
		}
		fs::remove(probe, ec);
		return true;
	};

	// Portable layouts (extracted tar, local build): keep config next to the binary
	// when that location is writable. AppImage / system installs are read-only.
	const bool appimage = (std::getenv("APPIMAGE") != nullptr);
	if (!appimage)
	{
		const fs::path beside = fs::path(boost::dll::program_location().remove_filename().string()) / "config";
		if (tryDir(beside))
			return beside.string();
	}

	// System config home when the binary dir is not writable.
	fs::path cfg;
#if defined(_WIN32)
	if (const char* appdata = std::getenv("APPDATA"); appdata && appdata[0] != '\0')
		cfg = fs::path(appdata) / "imirc";
	else if (const char* profile = std::getenv("USERPROFILE"); profile && profile[0] != '\0')
		cfg = fs::path(profile) / "AppData" / "Roaming" / "imirc";
	else
		cfg = fs::temp_directory_path() / "imirc-config";
#else
	if (const char* xdg_home = std::getenv("XDG_CONFIG_HOME"); xdg_home && xdg_home[0] != '\0')
		cfg = fs::path(xdg_home) / "imirc";
	else if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
		cfg = fs::path(home) / ".config" / "imirc";
	else
		cfg = fs::temp_directory_path() / "imirc-config";
#endif

	std::error_code ec;
	fs::create_directories(cfg, ec);
	return cfg.string();
}

std::string IrcConfig::serversPath()
{
	return (std::filesystem::path(configDir()) / "servers.json").string();
}

std::string IrcConfig::identityPath()
{
	return (std::filesystem::path(configDir()) / "identity.json").string();
}

std::string IrcConfig::channelHistoryPath()
{
	return (std::filesystem::path(configDir()) / "channel_history.json").string();
}

std::string IrcConfig::configPath()
{
	return serversPath();
}

std::string IrcConfig::makeId()
{
	static std::mt19937 rng{ std::random_device{}() };
	std::uniform_int_distribution<uint32_t> dist;
	std::stringstream ss;
	ss << std::hex << dist(rng) << dist(rng);
	return ss.str();
}

namespace
{
	ServerConfig makeDefaultServer(std::string name, std::string host, int port, bool tls,
		std::vector<std::string> auto_join = {})
	{
		ServerConfig s;
		s.id = IrcConfig::makeId();
		s.name = std::move(name);
		s.host = std::move(host);
		s.port = port;
		s.use_tls = tls;
		// Leave nick/username/realname empty so global identity applies
		s.auto_join = std::move(auto_join);
		s.auth_method = IrcAuthMethod::Inherit;
		s.auto_join_remembered = true;
		return s;
	}

	std::vector<ServerConfig> defaultServers()
	{
		return {
			makeDefaultServer("Libera Chat", "irc.libera.chat", 6697, true),
			makeDefaultServer("Freenode", "irc.freenode.net", 6697, true),
			makeDefaultServer("OFTC", "irc.oftc.net", 6697, true),
			makeDefaultServer("Rizon", "irc.rizon.net", 6697, true),
			makeDefaultServer("DALnet", "irc.dal.net", 6697, true),
			makeDefaultServer("EFNet", "irc.efnet.org", 6697, true),
			makeDefaultServer("Undernet", "irc.undernet.org", 6697, true),
			makeDefaultServer("QuakeNet", "irc.quakenet.org", 6667, false),
			makeDefaultServer("IRCnet", "open.ircnet.net", 6667, false),
			makeDefaultServer("EsperNet", "irc.esper.net", 6697, true),
		};
	}

	void syncChannelHistoryFile(const std::vector<ServerConfig>& servers)
	{
		std::map<std::string, std::vector<std::string>> history;
		for (const auto& s : servers)
		{
			if (s.remembered_channels.empty())
				continue;
			std::vector<std::string> names;
			names.reserve(s.remembered_channels.size());
			for (const auto& ch : s.remembered_channels)
			{
				if (!ch.name.empty())
					names.push_back(ch.name);
			}
			if (!names.empty())
				history[s.id] = std::move(names);
		}
		IrcConfig::saveChannelHistory(history, nullptr);
	}

	// Older builds wrote stock defaults into every server profile, which blocked
	// Account → Identity from applying (resolve only fills empty fields).
	bool clearPlaceholderIdentityFields(ServerConfig& s)
	{
		bool changed = false;
		if (boost::iequals(s.nick, "imirc_user"))
		{
			s.nick.clear();
			changed = true;
		}
		if (boost::iequals(s.username, "imirc") || boost::iequals(s.username, "imirc_user"))
		{
			s.username.clear();
			changed = true;
		}
		if (boost::iequals(s.realname, "ImIRC User") || boost::iequals(s.realname, "imirc_user"))
		{
			s.realname.clear();
			changed = true;
		}
		return changed;
	}
}

bool IrcConfig::loadServers(std::vector<ServerConfig>& out, std::string* err)
{
	out.clear();
	const std::string path = serversPath();
	if (!std::filesystem::exists(path))
	{
		out = defaultServers();
		return saveServers(out, err);
	}

	try
	{
		std::ifstream in(path);
		if (!in)
		{
			if (err) *err = "failed to open " + path;
			return false;
		}
		json j;
		in >> j;
		if (j.is_array())
			out = j.get<std::vector<ServerConfig>>();
		else if (j.contains("servers") && j["servers"].is_array())
			out = j["servers"].get<std::vector<ServerConfig>>();
		else
		{
			if (err) *err = "invalid servers.json format";
			return false;
		}

		// Merge channel_history.json if present (additive)
		std::map<std::string, std::vector<std::string>> history;
		if (loadChannelHistory(history, nullptr))
		{
			for (auto& s : out)
			{
				auto it = history.find(s.id);
				if (it == history.end())
					continue;
				for (const auto& ch : it->second)
				{
					if (ch.empty())
						continue;
					bool found = false;
					for (const auto& existing : s.remembered_channels)
					{
						if (boost::iequals(existing.name, ch))
						{
							found = true;
							break;
						}
					}
					if (!found)
						s.remembered_channels.push_back(RememberedChannel{ .name = ch });
				}
			}
		}

		bool migrated = false;
		for (auto& s : out)
			migrated = clearPlaceholderIdentityFields(s) || migrated;
		if (migrated)
		{
			BOOST_LOG_TRIVIAL(info) << "Cleared placeholder server nick/username so Account → Identity applies";
			saveServers(out, nullptr);
		}
		return true;
	}
	catch (const std::exception& e)
	{
		if (err) *err = e.what();
		BOOST_LOG_TRIVIAL(error) << "loadServers: " << e.what();
		return false;
	}
}

bool IrcConfig::saveServers(const std::vector<ServerConfig>& servers, std::string* err)
{
	try
	{
		json j;
		j["servers"] = servers;
		const std::string path = serversPath();
		std::ofstream out(path);
		if (!out)
		{
			if (err) *err = "failed to write " + path;
			return false;
		}
		out << j.dump(2);
		syncChannelHistoryFile(servers);
		return true;
	}
	catch (const std::exception& e)
	{
		if (err) *err = e.what();
		BOOST_LOG_TRIVIAL(error) << "saveServers: " << e.what();
		return false;
	}
}

bool IrcConfig::loadIdentity(IrcIdentity& out, std::string* err)
{
	const std::string path = identityPath();
	if (!std::filesystem::exists(path))
	{
		out = IrcIdentity{};
		return saveIdentity(out, err);
	}
	try
	{
		std::ifstream in(path);
		if (!in)
		{
			if (err) *err = "failed to open " + path;
			return false;
		}
		json j;
		in >> j;
		out = j.get<IrcIdentity>();
		return true;
	}
	catch (const std::exception& e)
	{
		if (err) *err = e.what();
		BOOST_LOG_TRIVIAL(error) << "loadIdentity: " << e.what();
		return false;
	}
}

bool IrcConfig::saveIdentity(const IrcIdentity& identity, std::string* err)
{
	try
	{
		json j = identity;
		std::ofstream out(identityPath());
		if (!out)
		{
			if (err) *err = "failed to write " + identityPath();
			return false;
		}
		out << j.dump(2);
		return true;
	}
	catch (const std::exception& e)
	{
		if (err) *err = e.what();
		BOOST_LOG_TRIVIAL(error) << "saveIdentity: " << e.what();
		return false;
	}
}

bool IrcConfig::loadChannelHistory(std::map<std::string, std::vector<std::string>>& out, std::string* err)
{
	out.clear();
	const std::string path = channelHistoryPath();
	if (!std::filesystem::exists(path))
		return true;
	try
	{
		std::ifstream in(path);
		if (!in)
		{
			if (err) *err = "failed to open " + path;
			return false;
		}
		json j;
		in >> j;
		if (j.contains("servers") && j["servers"].is_object())
		{
			for (auto it = j["servers"].begin(); it != j["servers"].end(); ++it)
			{
				if (it.value().is_array())
					out[it.key()] = it.value().get<std::vector<std::string>>();
				else if (it.value().is_object() && it.value().contains("channels"))
					out[it.key()] = it.value()["channels"].get<std::vector<std::string>>();
			}
		}
		return true;
	}
	catch (const std::exception& e)
	{
		if (err) *err = e.what();
		BOOST_LOG_TRIVIAL(error) << "loadChannelHistory: " << e.what();
		return false;
	}
}

bool IrcConfig::saveChannelHistory(const std::map<std::string, std::vector<std::string>>& history, std::string* err)
{
	try
	{
		json j;
		j["servers"] = json::object();
		for (const auto& [id, channels] : history)
			j["servers"][id] = channels;
		std::ofstream out(channelHistoryPath());
		if (!out)
		{
			if (err) *err = "failed to write " + channelHistoryPath();
			return false;
		}
		out << j.dump(2);
		return true;
	}
	catch (const std::exception& e)
	{
		if (err) *err = e.what();
		BOOST_LOG_TRIVIAL(error) << "saveChannelHistory: " << e.what();
		return false;
	}
}

ServerConfig IrcConfig::resolveForConnect(const ServerConfig& server, const IrcIdentity& identity)
{
	ServerConfig e = server;
	if (e.nick.empty())
		e.nick = identity.nick;
	if (e.alt_nick.empty())
		e.alt_nick = identity.alt_nick;
	if (e.account.empty())
		e.account = identity.account;
	if (e.username.empty())
		e.username = identity.username.empty() ? e.nick : identity.username;
	if (e.realname.empty())
		e.realname = identity.realname.empty() ? e.nick : identity.realname;
	if (e.nickserv_password.empty())
		e.nickserv_password = identity.nickserv_password;

	// Server ghost flag defaults to true; only inherit identity when server nick is blank
	// (i.e. using global identity). If server overrides nick, keep its own ghost setting.
	if (server.nick.empty())
		e.ghost_on_nick_taken = identity.ghost_on_nick_taken;

	if (e.auth_method == IrcAuthMethod::Inherit)
		e.auth_method = identity.default_auth;
	if (e.auth_method == IrcAuthMethod::Inherit)
		e.auth_method = IrcAuthMethod::NickServ;

	if (e.auto_join_remembered)
	{
		for (const auto& ch : e.remembered_channels)
		{
			if (ch.name.empty() || !ch.auto_join)
				continue;
			bool found = false;
			for (const auto& existing : e.auto_join)
			{
				if (boost::iequals(existing, ch.name))
				{
					found = true;
					break;
				}
			}
			if (!found)
				e.auto_join.push_back(ch.name);
		}
	}

	if (e.username.empty())
		e.username = e.nick;
	if (e.realname.empty())
		e.realname = e.nick;
	return e;
}

RememberedChannel* IrcConfig::findRememberedChannel(ServerConfig& server, const std::string& channel)
{
	for (auto& ch : server.remembered_channels)
	{
		if (boost::iequals(ch.name, channel))
			return &ch;
	}
	return nullptr;
}

const RememberedChannel* IrcConfig::findRememberedChannel(const ServerConfig& server, const std::string& channel)
{
	for (const auto& ch : server.remembered_channels)
	{
		if (boost::iequals(ch.name, channel))
			return &ch;
	}
	return nullptr;
}

RememberedChannel* IrcConfig::upsertRememberedChannel(ServerConfig& server, const std::string& channel)
{
	if (channel.empty())
		return nullptr;
	if (auto* existing = findRememberedChannel(server, channel))
		return existing;
	server.remembered_channels.push_back(RememberedChannel{ .name = channel });
	return &server.remembered_channels.back();
}

bool IrcConfig::rememberChannel(std::vector<ServerConfig>& servers, const std::string& server_id, const std::string& channel)
{
	if (channel.empty() || server_id.empty())
		return false;
	for (auto& s : servers)
	{
		if (s.id != server_id)
			continue;
		const bool existed = findRememberedChannel(s, channel) != nullptr;
		auto* rc = upsertRememberedChannel(s, channel);
		if (!rc)
			return false;
		if (!existed)
		{
			rc->auto_join = true;
			saveServers(servers, nullptr);
			return true;
		}
		return false;
	}
	return false;
}

bool IrcConfig::ensureChannelAutoJoin(std::vector<ServerConfig>& servers, const std::string& server_id, const std::string& channel)
{
	if (channel.empty() || server_id.empty())
		return false;
	for (auto& s : servers)
	{
		if (s.id != server_id)
			continue;
		auto* rc = upsertRememberedChannel(s, channel);
		if (!rc)
			return false;
		if (rc->auto_join)
			return false;
		rc->auto_join = true;
		saveServers(servers, nullptr);
		return true;
	}
	return false;
}

bool IrcConfig::forgetChannelAutoJoin(std::vector<ServerConfig>& servers, const std::string& server_id, const std::string& channel)
{
	if (channel.empty() || server_id.empty())
		return false;
	for (auto& s : servers)
	{
		if (s.id != server_id)
			continue;
		auto* rc = findRememberedChannel(s, channel);
		if (!rc || !rc->auto_join)
			return false;
		rc->auto_join = false;
		saveServers(servers, nullptr);
		return true;
	}
	return false;
}

bool IrcConfig::rememberNick(IrcIdentity& identity, const std::string& nick)
{
	if (nick.empty())
		return false;
	for (const auto& existing : identity.nick_history)
	{
		if (boost::iequals(existing, nick))
			return false;
	}
	identity.nick_history.insert(identity.nick_history.begin(), nick);
	constexpr size_t kMaxHistory = 32;
	if (identity.nick_history.size() > kMaxHistory)
		identity.nick_history.resize(kMaxHistory);
	saveIdentity(identity, nullptr);
	return true;
}

std::string IrcState::nowTimestamp()
{
	using clock = std::chrono::system_clock;
	const auto t = clock::to_time_t(clock::now());
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	std::stringstream ss;
	ss << std::put_time(&tm, "%H:%M:%S");
	return ss.str();
}

ChannelState* IrcState::getChannel(ServerSession& session, int channel_idx)
{
	if (channel_idx < 0)
		return &session.status;
	if (channel_idx >= 0 && channel_idx < (int)session.channels.size())
		return &session.channels[(size_t)channel_idx];
	return nullptr;
}

const ChannelState* IrcState::getChannel(const ServerSession& session, int channel_idx)
{
	if (channel_idx < 0)
		return &session.status;
	if (channel_idx >= 0 && channel_idx < (int)session.channels.size())
		return &session.channels[(size_t)channel_idx];
	return nullptr;
}

void IrcState::appendSystem(ChannelState& channel, const std::string& text)
{
	channel.messages.push_back(ChatMessage{
		.timestamp = nowTimestamp(),
		.nick = "",
		.text = text,
		.type = ChatMessageType::System,
	});
}

int IrcState::findTab(const IrcClientState& state, int session_idx, int channel_idx)
{
	for (int i = 0; i < (int)state.open_tabs.size(); ++i)
	{
		if (state.open_tabs[(size_t)i].session_idx == session_idx
			&& state.open_tabs[(size_t)i].channel_idx == channel_idx)
			return i;
	}
	return -1;
}

void IrcState::ensureTab(IrcClientState& state, int session_idx, int channel_idx)
{
	int idx = findTab(state, session_idx, channel_idx);
	if (idx < 0)
	{
		state.open_tabs.push_back({ session_idx, channel_idx });
		idx = (int)state.open_tabs.size() - 1;
	}
	state.active_tab = idx;
	state.selected_tree_session = session_idx;
	state.selected_tree_channel = channel_idx;
}

void IrcState::selectTab(IrcClientState& state, int session_idx, int channel_idx)
{
	ensureTab(state, session_idx, channel_idx);
	if (ChannelState* ch = getChannel(state.sessions[(size_t)session_idx], channel_idx))
		ch->unread = false;
}

ServerSession* IrcState::createSession(IrcClientState& state, const ServerConfig& cfg)
{
	for (auto& sess : state.sessions)
	{
		if (sess.config_id == cfg.id)
		{
			sess.display_name = cfg.name.empty() ? cfg.host : cfg.name;
			sess.current_nick = cfg.nick;
			sess.connecting = true;
			sess.connected = false;
			appendSystem(sess.status, "Connecting to " + cfg.host + ":" + std::to_string(cfg.port)
				+ (cfg.use_tls ? " (TLS)" : "") + " as " + cfg.nick);
			return &sess;
		}
	}

	ServerSession sess;
	sess.config_id = cfg.id;
	sess.display_name = cfg.name.empty() ? cfg.host : cfg.name;
	sess.current_nick = cfg.nick;
	sess.connecting = true;
	sess.connected = false;
	appendSystem(sess.status, "Connecting to " + cfg.host + ":" + std::to_string(cfg.port)
		+ (cfg.use_tls ? " (TLS)" : "") + " as " + cfg.nick);

	state.sessions.push_back(std::move(sess));
	const int session_idx = (int)state.sessions.size() - 1;
	ensureTab(state, session_idx, -1);
	return &state.sessions[(size_t)session_idx];
}

int IrcState::findSessionByConfig(const IrcClientState& state, const std::string& config_id)
{
	for (int i = 0; i < (int)state.sessions.size(); ++i)
	{
		if (state.sessions[(size_t)i].config_id == config_id)
			return i;
	}
	return -1;
}

int IrcState::channelIndex(const ServerSession& session, const std::string& name)
{
	for (int i = 0; i < (int)session.channels.size(); ++i)
	{
		if (boost::iequals(session.channels[(size_t)i].name, name))
			return i;
	}
	return -1;
}

ChannelState* IrcState::findOrCreateChannel(ServerSession& session, const std::string& name, bool is_query)
{
	int idx = channelIndex(session, name);
	if (idx >= 0)
		return &session.channels[(size_t)idx];

	ChannelState ch;
	ch.name = name;
	ch.is_query = is_query;
	session.channels.push_back(std::move(ch));
	return &session.channels.back();
}

void IrcState::disconnectSession(IrcClientState& state, int session_idx)
{
	if (session_idx < 0 || session_idx >= (int)state.sessions.size())
		return;

	state.sessions[(size_t)session_idx].connected = false;
	state.sessions[(size_t)session_idx].connecting = false;
	appendSystem(state.sessions[(size_t)session_idx].status, "Disconnected");
}

void IrcState::appendMessage(ChannelState& channel, ChatMessageType type, const std::string& nick, const std::string& text, bool mark_unread)
{
	channel.messages.push_back(ChatMessage{
		.timestamp = nowTimestamp(),
		.nick = nick,
		.text = text,
		.type = type,
	});
	if (mark_unread)
		channel.unread = true;
}

std::string IrcState::channelKey(const std::string& name)
{
	return boost::algorithm::to_lower_copy(name);
}

ChannelUser IrcState::parseNamespacedNick(const std::string& token)
{
	ChannelUser user;
	std::string n = token;

	auto is_mode_prefix = [](char c) {
		return c == '~' || c == '&' || c == '@' || c == '%' || c == '+';
	};

	while (!n.empty() && is_mode_prefix(n[0]))
	{
		user.prefixes.push_back(n[0]);
		n.erase(n.begin());
	}
	// Owner '!' used on some nets; avoid eating UHNAMES nick!user@host
	if (!n.empty() && n[0] == '!')
	{
		const bool multi = n.size() > 1 && (is_mode_prefix(n[1]) || n[1] == '!');
		const bool owner_only = n.find('@') == std::string::npos;
		if (multi || owner_only)
		{
			user.prefixes.push_back('!');
			n.erase(n.begin());
			while (!n.empty() && is_mode_prefix(n[0]))
			{
				user.prefixes.push_back(n[0]);
				n.erase(n.begin());
			}
		}
	}
	// UHNAMES / userhost-in-names: nick!user@host
	const auto bang = n.find('!');
	if (bang != std::string::npos)
		n = n.substr(0, bang);
	user.nick = n;
	return user;
}

int IrcState::prefixRank(const std::string& prefixes)
{
	// Lower is higher rank for sorting
	int best = 100;
	for (char c : prefixes)
	{
		int r = 100;
		switch (c)
		{
		case '~': r = 0; break; // owner
		case '&': r = 1; break; // admin
		case '!': r = 1; break; // some nets
		case '@': r = 2; break; // op
		case '%': r = 3; break; // halfop
		case '+': r = 4; break; // voice
		default: break;
		}
		best = std::min(best, r);
	}
	return best;
}

void IrcState::sortUsers(ChannelState& channel)
{
	std::sort(channel.users.begin(), channel.users.end(),
		[](const ChannelUser& a, const ChannelUser& b)
		{
			const int ra = prefixRank(a.prefixes);
			const int rb = prefixRank(b.prefixes);
			if (ra != rb)
				return ra < rb;
			return boost::ilexicographical_compare(a.nick, b.nick);
		});
}

void IrcState::addOrUpdateUser(ChannelState& channel, const ChannelUser& user)
{
	if (user.nick.empty())
		return;
	for (auto& existing : channel.users)
	{
		if (boost::iequals(existing.nick, user.nick))
		{
			existing.nick = user.nick;
			if (!user.prefixes.empty())
				existing.prefixes = user.prefixes;
			return;
		}
	}
	channel.users.push_back(user);
}

void IrcState::removeUser(ChannelState& channel, const std::string& nick)
{
	channel.users.erase(std::remove_if(channel.users.begin(), channel.users.end(),
		[&](const ChannelUser& u) { return boost::iequals(u.nick, nick); }), channel.users.end());
}

void IrcState::renameUser(ChannelState& channel, const std::string& old_nick, const std::string& new_nick)
{
	for (auto& u : channel.users)
	{
		if (boost::iequals(u.nick, old_nick))
		{
			u.nick = new_nick;
			return;
		}
	}
}
