#ifndef irc_state_h__
#define irc_state_h__

#include <string>
#include <vector>
#include <cstdint>
#include <map>

// 0 = inherit global identity default
enum class IrcAuthMethod
{
	Inherit = 0,
	None = 1,
	NickServ = 2,
	SaslPlain = 3
};

struct IrcIdentity
{
	std::string nick = "imirc_user";
	std::string alt_nick;              // used if primary nick is taken (433)
	std::string account;               // SASL/NickServ account name (blank = nick)
	std::string username = "imirc";
	std::string realname = "ImIRC User";
	std::string nickserv_password;
	IrcAuthMethod default_auth = IrcAuthMethod::NickServ;
	bool ghost_on_nick_taken = true;   // NickServ GHOST then reclaim nick
	std::vector<std::string> nick_history;
};

// Per-channel settings (keys, ChanServ, auto-join)
struct RememberedChannel
{
	std::string name;
	std::string key;                 // JOIN key (+k)
	std::string chanserv_password;   // ChanServ IDENTIFY #chan <pass>
	bool chanserv_identify = false;
	bool chanserv_op = false;        // ChanServ OP after join/identify
	bool auto_join = true;
};

struct ServerConfig
{
	std::string id;
	std::string name;
	std::string host = "irc.libera.chat";
	int port = 6667;
	bool use_tls = false;
	// Empty nick/username/realname/nickserv_password => use global identity
	std::string nick;
	std::string alt_nick;
	std::string account;
	std::string username;
	std::string realname;
	std::string password;          // server PASS
	std::string nickserv_password; // account password (NickServ / SASL)
	IrcAuthMethod auth_method = IrcAuthMethod::Inherit;
	bool ghost_on_nick_taken = true;
	std::vector<std::string> auto_join;
	std::vector<RememberedChannel> remembered_channels;
	bool auto_join_remembered = true;
};

enum class ChatMessageType
{
	Normal,
	Action,
	Join,
	Part,
	Quit,
	Nick,
	Notice,
	System
};

struct ChatMessage
{
	std::string timestamp;
	std::string nick;
	std::string text;
	ChatMessageType type = ChatMessageType::Normal;
};

struct ChannelUser
{
	std::string nick;
	std::string prefixes; // e.g. "@", "+", "~@"
};

struct ChannelState
{
	std::string name;
	std::vector<ChannelUser> users;
	std::vector<ChatMessage> messages;
	bool unread = false;
	bool is_query = false;
};

struct ChannelListEntry
{
	std::string name;
	int users = 0;
	std::string topic;
};

struct ServerSession
{
	std::string config_id;
	std::string display_name;
	std::string current_nick;
	bool connected = false;
	bool connecting = false;
	ChannelState status{ "(status)" };
	std::vector<ChannelState> channels;
	// Accumulates 353 replies until 366 (key = lowercase channel name)
	std::map<std::string, std::vector<ChannelUser>> pending_names;
	// Channel LIST browser state
	std::vector<ChannelListEntry> channel_list;
	bool channel_list_loading = false;
};

struct OpenTab
{
	int session_idx = -1;
	int channel_idx = -1; // -1 = status buffer
};

struct IrcClientState
{
	IrcIdentity identity;
	std::vector<ServerConfig> servers;
	std::vector<ServerSession> sessions;
	std::vector<OpenTab> open_tabs;
	int active_tab = 0;
	int selected_tree_session = -1;
	int selected_tree_channel = -1;
};

namespace IrcConfig
{
	std::string configDir();
	std::string serversPath();
	std::string identityPath();
	std::string channelHistoryPath();
	std::string makeId();

	bool loadServers(std::vector<ServerConfig>& out, std::string* err = nullptr);
	bool saveServers(const std::vector<ServerConfig>& servers, std::string* err = nullptr);
	bool loadIdentity(IrcIdentity& out, std::string* err = nullptr);
	bool saveIdentity(const IrcIdentity& identity, std::string* err = nullptr);
	bool loadChannelHistory(std::map<std::string, std::vector<std::string>>& out, std::string* err = nullptr);
	bool saveChannelHistory(const std::map<std::string, std::vector<std::string>>& history, std::string* err = nullptr);
	std::string configPath(); // alias for serversPath (compat)

	ServerConfig resolveForConnect(const ServerConfig& server, const IrcIdentity& identity);
	bool rememberChannel(std::vector<ServerConfig>& servers, const std::string& server_id, const std::string& channel);
	bool ensureChannelAutoJoin(std::vector<ServerConfig>& servers, const std::string& server_id, const std::string& channel);
	bool forgetChannelAutoJoin(std::vector<ServerConfig>& servers, const std::string& server_id, const std::string& channel);
	bool rememberNick(IrcIdentity& identity, const std::string& nick);
	RememberedChannel* findRememberedChannel(ServerConfig& server, const std::string& channel);
	const RememberedChannel* findRememberedChannel(const ServerConfig& server, const std::string& channel);
	RememberedChannel* upsertRememberedChannel(ServerConfig& server, const std::string& channel);
}

namespace IrcState
{
	std::string nowTimestamp();
	ChannelState* getChannel(ServerSession& session, int channel_idx);
	const ChannelState* getChannel(const ServerSession& session, int channel_idx);
	void appendSystem(ChannelState& channel, const std::string& text);
	void ensureTab(IrcClientState& state, int session_idx, int channel_idx);
	void selectTab(IrcClientState& state, int session_idx, int channel_idx);
	int findTab(const IrcClientState& state, int session_idx, int channel_idx);
	ServerSession* createSession(IrcClientState& state, const ServerConfig& cfg);
	int findSessionByConfig(const IrcClientState& state, const std::string& config_id);
	ChannelState* findOrCreateChannel(ServerSession& session, const std::string& name, bool is_query = false);
	int channelIndex(const ServerSession& session, const std::string& name);
	void disconnectSession(IrcClientState& state, int session_idx);
	void appendMessage(ChannelState& channel, ChatMessageType type, const std::string& nick, const std::string& text, bool mark_unread);

	std::string channelKey(const std::string& name);
	ChannelUser parseNamespacedNick(const std::string& token);
	void addOrUpdateUser(ChannelState& channel, const ChannelUser& user);
	void removeUser(ChannelState& channel, const std::string& nick);
	void renameUser(ChannelState& channel, const std::string& old_nick, const std::string& new_nick);
	void sortUsers(ChannelState& channel);
	int prefixRank(const std::string& prefixes);
}

#endif // irc_state_h__
