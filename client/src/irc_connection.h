#ifndef irc_connection_h__
#define irc_connection_h__

#include "irc_state.h"

#include "boost/asio.hpp"
#include <boost/asio/ssl.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>

struct IrcMessage
{
	std::string raw;
	std::string prefix;
	std::string command;
	std::vector<std::string> params;

	std::string nick() const;
	std::string trailing() const;
	static bool parse(const std::string& line, IrcMessage& out);
};

enum class IrcNetEventType
{
	Status,
	Connected,
	Disconnected,
	Registered,
	Privmsg,
	Notice,
	Join,
	Part,
	Quit,
	NickChange,
	Topic,
	Names,
	NamesEnd,
	WhoEntry,
	ListStart,
	ListEntry,
	ListEnd,
	Numeric,
	Error
};

struct IrcNetEvent
{
	IrcNetEventType type = IrcNetEventType::Status;
	std::string config_id;
	std::string channel;   // target channel / query nick where relevant
	std::string nick;
	std::string text;
	std::vector<std::string> nicks;
	int numeric = 0;
	int user_count = 0;
};

class IrcEventQueue
{
public:
	void push(IrcNetEvent ev);
	std::vector<IrcNetEvent> drain();

private:
	std::mutex m_mutex;
	std::vector<IrcNetEvent> m_events;
};

// Boost.Asio IRC client (plain TCP or TLS).
class IrcConnection : public std::enable_shared_from_this<IrcConnection>
{
public:
	IrcConnection(boost::asio::io_context& ioctx,
		ServerConfig cfg,
		std::shared_ptr<IrcEventQueue> events);
	~IrcConnection();

	void start();
	void stop(const std::string& quit_msg = "Quit");

	void sendRaw(std::string line);
	void join(const std::string& channel, const std::string& key = "");
	void part(const std::string& channel, const std::string& reason = "");
	void privmsg(const std::string& target, const std::string& text);
	void nick(const std::string& new_nick);
	void quit(const std::string& reason = "Quit");
	void listChannels(const std::string& pattern = "");
	void who(const std::string& target);
	void chanServIdentify(const std::string& channel, const std::string& password);
	void chanServOp(const std::string& channel);
	void upsertRememberedChannel(RememberedChannel channel);

	const ServerConfig& config() const { return m_cfg; }
	bool isAlive() const { return m_alive; }

private:
	using tcp = boost::asio::ip::tcp;

	void postStatus(const std::string& text);
	void postEvent(IrcNetEvent ev);
	void resolve();
	void onResolved(const boost::system::error_code& ec, tcp::resolver::results_type results);
	void onConnected(const boost::system::error_code& ec);
	void handshakeTls();
	void onTlsHandshake(const boost::system::error_code& ec);
	void registerUser();
	void finishCapabilities();
	void startSaslPlain();
	void sendNickServIdentify();
	void sendNickServGhost();
	void beginNickReclaim();
	void scheduleNickReclaim(std::chrono::milliseconds delay);
	void cancelNickReclaim();
	void tryTempNick(const std::string& attempted);
	void applyChanServ(const std::string& channel);
	void handleNickInUse(const std::string& attempted);
	void handleNickServNotice(const std::string& text);
	std::string accountName() const;
	std::string joinLine(const std::string& channel, const std::string& key) const;
	void doAutoJoins();
	void doRead();
	void onRead(const boost::system::error_code& ec, std::size_t n);
	void processLine(std::string line);
	void handleMessage(const IrcMessage& msg);
	void enqueueWrite(std::string line);
	void doWrite();

	boost::asio::io_context& m_io;
	boost::asio::strand<boost::asio::io_context::executor_type> m_strand;
	ServerConfig m_cfg;
	std::shared_ptr<IrcEventQueue> m_events;

	tcp::resolver m_resolver;
	std::unique_ptr<tcp::socket> m_socket;
	std::unique_ptr<boost::asio::ssl::context> m_sslCtx;
	std::unique_ptr<boost::asio::ssl::stream<tcp::socket>> m_ssl;
	std::unique_ptr<boost::asio::steady_timer> m_reclaimTimer;

	boost::asio::streambuf m_inbuf;
	std::deque<std::string> m_outq;
	bool m_writing = false;
	bool m_alive = false;
	bool m_registered = false;
	bool m_wantSasl = false;
	bool m_saslComplete = false;
	bool m_capEnded = false;
	bool m_nickservSent = false;
	bool m_altNickTried = false;
	bool m_ghostSent = false;
	bool m_wantReclaim = false;
	bool m_ghostFailed = false;
	int m_reclaimAttempts = 0;
	int m_nickSuffix = 0;
	std::string m_desiredNick;
	std::string m_currentNick;
};

#endif // irc_connection_h__
