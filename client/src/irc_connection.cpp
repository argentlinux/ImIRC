#include "irc_connection.h"

#include "boost/algorithm/string.hpp"
#include "boost/log/trivial.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <sstream>

namespace
{
	std::string stripCr(std::string s)
	{
		if (!s.empty() && s.back() == '\r')
			s.pop_back();
		return s;
	}

	std::string base64Encode(const std::string& input)
	{
		if (input.empty())
			return {};
		const int out_len = 4 * ((int)((input.size() + 2) / 3));
		std::string out((size_t)out_len, '\0');
		const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]),
			reinterpret_cast<const unsigned char*>(input.data()), (int)input.size());
		if (n > 0)
			out.resize((size_t)n);
		else
			out.clear();
		return out;
	}
}

std::string IrcMessage::nick() const
{
	const auto bang = prefix.find('!');
	if (bang == std::string::npos)
		return prefix;
	return prefix.substr(0, bang);
}

std::string IrcMessage::trailing() const
{
	if (params.empty())
		return {};
	return params.back();
}

bool IrcMessage::parse(const std::string& line, IrcMessage& out)
{
	out = IrcMessage{};
	out.raw = line;
	if (line.empty())
		return false;

	std::string rest = line;
	if (rest[0] == ':')
	{
		const auto sp = rest.find(' ');
		if (sp == std::string::npos)
			return false;
		out.prefix = rest.substr(1, sp - 1);
		rest = rest.substr(sp + 1);
	}

	while (!rest.empty() && rest[0] == ' ')
		rest.erase(rest.begin());

	const auto sp = rest.find(' ');
	if (sp == std::string::npos)
	{
		out.command = rest;
		return !out.command.empty();
	}

	out.command = rest.substr(0, sp);
	rest = rest.substr(sp + 1);

	while (!rest.empty())
	{
		while (!rest.empty() && rest[0] == ' ')
			rest.erase(rest.begin());
		if (rest.empty())
			break;
		if (rest[0] == ':')
		{
			out.params.push_back(rest.substr(1));
			break;
		}
		const auto nsp = rest.find(' ');
		if (nsp == std::string::npos)
		{
			out.params.push_back(rest);
			break;
		}
		out.params.push_back(rest.substr(0, nsp));
		rest = rest.substr(nsp + 1);
	}

	boost::to_upper(out.command);
	return !out.command.empty();
}

void IrcEventQueue::push(IrcNetEvent ev)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_events.push_back(std::move(ev));
}

std::vector<IrcNetEvent> IrcEventQueue::drain()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<IrcNetEvent> out;
	out.swap(m_events);
	return out;
}

IrcConnection::IrcConnection(boost::asio::io_context& ioctx,
	ServerConfig cfg,
	std::shared_ptr<IrcEventQueue> events)
	: m_io(ioctx)
	, m_strand(boost::asio::make_strand(ioctx))
	, m_cfg(std::move(cfg))
	, m_events(std::move(events))
	, m_resolver(ioctx)
{
	m_desiredNick = m_cfg.nick;
	m_currentNick = m_cfg.nick;
	if (m_cfg.username.empty())
		m_cfg.username = m_cfg.nick;
	if (m_cfg.realname.empty())
		m_cfg.realname = m_cfg.nick;
}

IrcConnection::~IrcConnection()
{
	m_alive = false;
}

void IrcConnection::postStatus(const std::string& text)
{
	postEvent(IrcNetEvent{
		.type = IrcNetEventType::Status,
		.config_id = m_cfg.id,
		.text = text,
	});
}

void IrcConnection::postEvent(IrcNetEvent ev)
{
	ev.config_id = m_cfg.id;
	if (m_events)
		m_events->push(std::move(ev));
}

void IrcConnection::start()
{
	auto self = shared_from_this();
	boost::asio::post(m_strand, [self]()
		{
			self->m_alive = true;
			self->postStatus("Resolving " + self->m_cfg.host + "...");
			self->resolve();
		});
}

void IrcConnection::stop(const std::string& quit_msg)
{
	auto self = shared_from_this();
	boost::asio::post(m_strand, [self, quit_msg]()
		{
			self->cancelNickReclaim();
			if (self->m_alive && self->m_registered)
				self->enqueueWrite("QUIT :" + quit_msg);
			self->m_alive = false;

			boost::system::error_code ec;
			if (self->m_ssl)
			{
				self->m_ssl->lowest_layer().cancel(ec);
				self->m_ssl->lowest_layer().close(ec);
			}
			if (self->m_socket)
			{
				self->m_socket->cancel(ec);
				self->m_socket->close(ec);
			}
			self->postEvent(IrcNetEvent{ .type = IrcNetEventType::Disconnected, .text = "Disconnected" });
		});
}

void IrcConnection::sendRaw(std::string line)
{
	auto self = shared_from_this();
	boost::asio::post(m_strand, [self, line = std::move(line)]() mutable
		{
			self->enqueueWrite(std::move(line));
		});
}

void IrcConnection::join(const std::string& channel, const std::string& key)
{
	sendRaw(joinLine(channel, key));
}

void IrcConnection::part(const std::string& channel, const std::string& reason)
{
	if (reason.empty())
		sendRaw("PART " + channel);
	else
		sendRaw("PART " + channel + " :" + reason);
}

void IrcConnection::privmsg(const std::string& target, const std::string& text)
{
	sendRaw("PRIVMSG " + target + " :" + text);
}

void IrcConnection::nick(const std::string& new_nick)
{
	m_desiredNick = new_nick;
	sendRaw("NICK " + new_nick);
}

void IrcConnection::quit(const std::string& reason)
{
	stop(reason);
}

void IrcConnection::listChannels(const std::string& pattern)
{
	if (pattern.empty())
		sendRaw("LIST");
	else
		sendRaw("LIST " + pattern);
}

void IrcConnection::who(const std::string& target)
{
	if (!target.empty())
		sendRaw("WHO " + target);
}

void IrcConnection::chanServIdentify(const std::string& channel, const std::string& password)
{
	if (channel.empty() || password.empty())
		return;
	sendRaw("PRIVMSG ChanServ :IDENTIFY " + channel + " " + password);
	postStatus("Sent ChanServ IDENTIFY for " + channel);
}

void IrcConnection::chanServOp(const std::string& channel)
{
	if (channel.empty())
		return;
	sendRaw("PRIVMSG ChanServ :OP " + channel);
	postStatus("Sent ChanServ OP for " + channel);
}

void IrcConnection::upsertRememberedChannel(RememberedChannel channel)
{
	auto self = shared_from_this();
	boost::asio::post(m_strand, [self, channel = std::move(channel)]() mutable
		{
			if (auto* rc = IrcConfig::upsertRememberedChannel(self->m_cfg, channel.name))
				*rc = std::move(channel);
		});
}

std::string IrcConnection::accountName() const
{
	if (!m_cfg.account.empty())
		return m_cfg.account;
	return m_desiredNick.empty() ? m_cfg.nick : m_desiredNick;
}

std::string IrcConnection::joinLine(const std::string& channel, const std::string& key) const
{
	if (key.empty())
		return "JOIN " + channel;
	return "JOIN " + channel + " " + key;
}

void IrcConnection::resolve()
{
	auto self = shared_from_this();
	m_resolver.async_resolve(m_cfg.host, std::to_string(m_cfg.port),
		boost::asio::bind_executor(m_strand,
			[self](const boost::system::error_code& ec, tcp::resolver::results_type results)
			{
				self->onResolved(ec, results);
			}));
}

void IrcConnection::onResolved(const boost::system::error_code& ec, tcp::resolver::results_type results)
{
	if (ec || !m_alive)
	{
		postEvent(IrcNetEvent{ .type = IrcNetEventType::Error, .text = "Resolve failed: " + ec.message() });
		m_alive = false;
		return;
	}

	postStatus("Connecting to " + m_cfg.host + ":" + std::to_string(m_cfg.port)
		+ (m_cfg.use_tls ? " (TLS)" : "") + "...");

	auto self = shared_from_this();
	if (m_cfg.use_tls)
	{
		m_sslCtx = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);
		m_sslCtx->set_default_verify_paths();
		// Many IRC networks use certs that fail strict verify on some systems; keep usable defaults.
		m_sslCtx->set_verify_mode(boost::asio::ssl::verify_none);
		m_ssl = std::make_unique<boost::asio::ssl::stream<tcp::socket>>(m_io, *m_sslCtx);
		boost::asio::async_connect(m_ssl->lowest_layer(), results,
			boost::asio::bind_executor(m_strand,
				[self](const boost::system::error_code& err, const tcp::endpoint&)
				{
					self->onConnected(err);
				}));
	}
	else
	{
		m_socket = std::make_unique<tcp::socket>(m_io);
		boost::asio::async_connect(*m_socket, results,
			boost::asio::bind_executor(m_strand,
				[self](const boost::system::error_code& err, const tcp::endpoint&)
				{
					self->onConnected(err);
				}));
	}
}

void IrcConnection::onConnected(const boost::system::error_code& ec)
{
	if (ec || !m_alive)
	{
		postEvent(IrcNetEvent{ .type = IrcNetEventType::Error, .text = "Connect failed: " + ec.message() });
		m_alive = false;
		return;
	}

	if (m_cfg.use_tls)
		handshakeTls();
	else
	{
		postEvent(IrcNetEvent{ .type = IrcNetEventType::Connected, .text = "TCP connected" });
		registerUser();
		doRead();
	}
}

void IrcConnection::handshakeTls()
{
	auto self = shared_from_this();
	if (!SSL_set_tlsext_host_name(m_ssl->native_handle(), m_cfg.host.c_str()))
	{
		postEvent(IrcNetEvent{ .type = IrcNetEventType::Error, .text = "Failed to set TLS SNI hostname" });
		m_alive = false;
		return;
	}

	m_ssl->async_handshake(boost::asio::ssl::stream_base::client,
		boost::asio::bind_executor(m_strand,
			[self](const boost::system::error_code& ec)
			{
				self->onTlsHandshake(ec);
			}));
}

void IrcConnection::onTlsHandshake(const boost::system::error_code& ec)
{
	if (ec || !m_alive)
	{
		postEvent(IrcNetEvent{ .type = IrcNetEventType::Error, .text = "TLS handshake failed: " + ec.message() });
		m_alive = false;
		return;
	}

	postEvent(IrcNetEvent{ .type = IrcNetEventType::Connected, .text = "TLS connected" });
	registerUser();
	doRead();
}

void IrcConnection::registerUser()
{
	m_wantSasl = (m_cfg.auth_method == IrcAuthMethod::SaslPlain && !m_cfg.nickserv_password.empty());
	m_saslComplete = false;
	m_capEnded = false;
	m_nickservSent = false;
	m_altNickTried = false;
	m_ghostSent = false;
	m_wantReclaim = false;
	m_ghostFailed = false;
	m_reclaimAttempts = 0;
	m_nickSuffix = 0;
	m_desiredNick = m_cfg.nick;
	m_currentNick = m_cfg.nick;
	cancelNickReclaim();

	enqueueWrite("CAP LS 302");
	if (!m_cfg.password.empty())
		enqueueWrite("PASS " + m_cfg.password);
	enqueueWrite("NICK " + m_cfg.nick);
	enqueueWrite("USER " + m_cfg.username + " 0 * :" + m_cfg.realname);
}

void IrcConnection::finishCapabilities()
{
	if (m_capEnded)
		return;
	m_capEnded = true;
	enqueueWrite("CAP END");
}

void IrcConnection::startSaslPlain()
{
	enqueueWrite("AUTHENTICATE PLAIN");
}

void IrcConnection::sendNickServIdentify()
{
	if (m_nickservSent || m_cfg.nickserv_password.empty())
		return;
	if (m_cfg.auth_method == IrcAuthMethod::None)
		return;
	if (m_cfg.auth_method == IrcAuthMethod::SaslPlain && m_saslComplete)
		return;

	m_nickservSent = true;
	const std::string acct = accountName();
	// Atheme accepts "IDENTIFY <password>" or "IDENTIFY <account> <password>"
	enqueueWrite("PRIVMSG NickServ :IDENTIFY " + acct + " " + m_cfg.nickserv_password);
	postStatus("Sent NickServ IDENTIFY for " + acct);
}

void IrcConnection::sendNickServGhost()
{
	if (m_ghostSent || m_cfg.nickserv_password.empty() || !m_cfg.ghost_on_nick_taken)
		return;
	m_ghostSent = true;
	const std::string target = m_desiredNick.empty() ? m_cfg.nick : m_desiredNick;
	enqueueWrite("PRIVMSG NickServ :GHOST " + target + " " + m_cfg.nickserv_password);
	postStatus("Sent NickServ GHOST for " + target + " — waiting before reclaiming nick");
}

void IrcConnection::cancelNickReclaim()
{
	if (m_reclaimTimer)
	{
		m_reclaimTimer->cancel();
		m_reclaimTimer.reset();
	}
}

void IrcConnection::scheduleNickReclaim(std::chrono::milliseconds delay)
{
	cancelNickReclaim();
	if (!m_wantReclaim || m_ghostFailed || m_desiredNick.empty())
		return;

	m_reclaimTimer = std::make_unique<boost::asio::steady_timer>(m_io);
	m_reclaimTimer->expires_after(delay);
	auto self = shared_from_this();
	m_reclaimTimer->async_wait(boost::asio::bind_executor(m_strand,
		[self](const boost::system::error_code& ec)
		{
			if (ec || !self->m_alive || !self->m_wantReclaim || self->m_ghostFailed)
				return;
			if (boost::iequals(self->m_currentNick, self->m_desiredNick))
			{
				self->m_wantReclaim = false;
				return;
			}
			++self->m_reclaimAttempts;
			self->postStatus("Reclaiming nick " + self->m_desiredNick
				+ " (attempt " + std::to_string(self->m_reclaimAttempts) + ")");
			self->enqueueWrite("NICK " + self->m_desiredNick);
		}));
}

void IrcConnection::beginNickReclaim()
{
	if (!m_cfg.ghost_on_nick_taken || m_cfg.nickserv_password.empty() || m_desiredNick.empty())
		return;
	m_wantReclaim = true;
	m_ghostFailed = false;
	sendNickServIdentify();
	sendNickServGhost();
	// Wait for NickServ; also schedule a first reclaim in case the notice is missed.
	scheduleNickReclaim(std::chrono::milliseconds(1500));
}

void IrcConnection::tryTempNick(const std::string& attempted)
{
	std::string temp;
	if (!m_altNickTried && !m_cfg.alt_nick.empty()
		&& !boost::iequals(m_cfg.alt_nick, attempted)
		&& !boost::iequals(m_cfg.alt_nick, m_desiredNick))
	{
		m_altNickTried = true;
		temp = m_cfg.alt_nick;
	}
	else
	{
		++m_nickSuffix;
		temp = m_desiredNick + "_" + std::to_string(m_nickSuffix);
	}
	m_currentNick = temp;
	enqueueWrite("NICK " + temp);
	postStatus("Using temporary nick " + temp + " to reclaim " + m_desiredNick);
}

void IrcConnection::handleNickServNotice(const std::string& text)
{
	const std::string lower = boost::algorithm::to_lower_copy(text);

	if (lower.find("has been ghosted") != std::string::npos
		|| lower.find("has been killed") != std::string::npos
		|| (lower.find("ghost") != std::string::npos && lower.find("success") != std::string::npos))
	{
		postStatus("NickServ GHOST succeeded — reclaiming " + m_desiredNick);
		scheduleNickReclaim(std::chrono::milliseconds(250));
		return;
	}

	if (lower.find("not a registered nickname") != std::string::npos
		|| lower.find("isn't registered") != std::string::npos
		|| lower.find("is not registered") != std::string::npos
		|| lower.find("invalid password") != std::string::npos
		|| lower.find("access denied") != std::string::npos
		|| lower.find("authentication failed") != std::string::npos)
	{
		m_ghostFailed = true;
		m_wantReclaim = false;
		cancelNickReclaim();
		postStatus("NickServ could not GHOST " + m_desiredNick + ": " + text);
		postStatus("Stay on " + m_currentNick
			+ " — register the nick, fix the NickServ password, or disconnect the other session.");
	}
}

void IrcConnection::applyChanServ(const std::string& channel)
{
	const RememberedChannel* rc = IrcConfig::findRememberedChannel(m_cfg, channel);
	if (!rc)
		return;
	if (rc->chanserv_identify && !rc->chanserv_password.empty())
	{
		enqueueWrite("PRIVMSG ChanServ :IDENTIFY " + rc->name + " " + rc->chanserv_password);
		postStatus("Sent ChanServ IDENTIFY for " + rc->name);
	}
	if (rc->chanserv_op)
	{
		enqueueWrite("PRIVMSG ChanServ :OP " + rc->name);
		postStatus("Sent ChanServ OP for " + rc->name);
	}
}

void IrcConnection::handleNickInUse(const std::string& attempted)
{
	postStatus("Nick in use: " + attempted);

	const bool desired_taken = boost::iequals(attempted, m_desiredNick)
		|| boost::iequals(attempted, m_cfg.nick);

	// Still trying to reclaim after GHOST — retry a few times instead of yo5phz2.
	if (m_wantReclaim && !m_ghostFailed && desired_taken)
	{
		if (m_reclaimAttempts < 5)
		{
			const auto delay = std::chrono::milliseconds(1000 * (1 + m_reclaimAttempts));
			postStatus("Desired nick still held — retrying reclaim shortly");
			if (!m_ghostSent && m_registered)
				sendNickServGhost();
			scheduleNickReclaim(delay);
			return;
		}
		m_wantReclaim = false;
		cancelNickReclaim();
		postStatus("Gave up reclaiming " + m_desiredNick + " after repeated 433s");
	}

	// Temp nick collided while waiting to reclaim — pick another temp, keep reclaim intent.
	if (m_wantReclaim && !m_ghostFailed && !desired_taken)
	{
		tryTempNick(attempted);
		return;
	}

	// Start ghost/reclaim flow when our preferred nick is taken.
	if (desired_taken
		&& m_cfg.ghost_on_nick_taken
		&& !m_cfg.nickserv_password.empty()
		&& !m_ghostFailed)
	{
		m_wantReclaim = true;
		tryTempNick(attempted);
		if (m_registered)
			beginNickReclaim();
		return;
	}

	if (!m_altNickTried && !m_cfg.alt_nick.empty() && !boost::iequals(m_cfg.alt_nick, attempted))
	{
		m_altNickTried = true;
		m_currentNick = m_cfg.alt_nick;
		enqueueWrite("NICK " + m_cfg.alt_nick);
		postStatus("Trying alternate nick: " + m_cfg.alt_nick);
		return;
	}

	++m_nickSuffix;
	std::string fallback = m_desiredNick.empty() ? m_cfg.nick : m_desiredNick;
	fallback += std::to_string(m_nickSuffix);
	m_currentNick = fallback;
	enqueueWrite("NICK " + fallback);
	postStatus("Trying nick: " + fallback);
}

void IrcConnection::doAutoJoins()
{
	for (const auto& ch : m_cfg.auto_join)
	{
		if (ch.empty())
			continue;
		std::string key;
		if (const auto* rc = IrcConfig::findRememberedChannel(m_cfg, ch))
			key = rc->key;
		enqueueWrite(joinLine(ch, key));
	}
}

void IrcConnection::doRead()
{
	if (!m_alive)
		return;

	auto self = shared_from_this();
	auto handler = [self](const boost::system::error_code& ec, std::size_t n)
		{
			self->onRead(ec, n);
		};

	if (m_ssl)
	{
		boost::asio::async_read_until(*m_ssl, m_inbuf, '\n',
			boost::asio::bind_executor(m_strand, handler));
	}
	else if (m_socket)
	{
		boost::asio::async_read_until(*m_socket, m_inbuf, '\n',
			boost::asio::bind_executor(m_strand, handler));
	}
}

void IrcConnection::onRead(const boost::system::error_code& ec, std::size_t /*n*/)
{
	if (ec)
	{
		if (m_alive)
		{
			postEvent(IrcNetEvent{
				.type = IrcNetEventType::Disconnected,
				.text = "Connection closed: " + ec.message(),
			});
		}
		m_alive = false;
		return;
	}

	std::istream is(&m_inbuf);
	std::string line;
	while (std::getline(is, line))
	{
		processLine(stripCr(std::move(line)));
	}

	doRead();
}

void IrcConnection::processLine(std::string line)
{
	if (line.empty())
		return;

	BOOST_LOG_TRIVIAL(debug) << "[IRC <<] " << line;

	IrcMessage msg;
	if (!IrcMessage::parse(line, msg))
		return;

	handleMessage(msg);
}

void IrcConnection::handleMessage(const IrcMessage& msg)
{
	if (msg.command == "PING")
	{
		const std::string token = msg.params.empty() ? "" : msg.params.back();
		enqueueWrite(token.empty() ? "PONG" : ("PONG :" + token));
		return;
	}

	if (msg.command == "CAP")
	{
		std::string sub = msg.params.size() >= 2 ? msg.params[1] : "";
		boost::to_upper(sub);
		const std::string caps = msg.trailing();
		if (sub == "LS" || sub == "LS302")
		{
			const bool has_sasl = boost::algorithm::icontains(caps, "sasl");
			if (m_wantSasl && has_sasl)
			{
				postStatus("Requesting SASL capability");
				enqueueWrite("CAP REQ :sasl");
			}
			else
			{
				if (m_wantSasl && !has_sasl)
					postStatus("Server has no SASL; will use NickServ if configured");
				finishCapabilities();
			}
		}
		else if (sub == "ACK")
		{
			if (boost::algorithm::icontains(caps, "sasl"))
				startSaslPlain();
			else
				finishCapabilities();
		}
		else if (sub == "NAK")
		{
			postStatus("SASL capability rejected");
			m_wantSasl = false;
			finishCapabilities();
		}
		return;
	}

	if (msg.command == "AUTHENTICATE")
	{
		const std::string arg = msg.params.empty() ? msg.trailing() : msg.params[0];
		if (arg == "+")
		{
			std::string payload;
			payload.push_back('\0');
			payload += accountName();
			payload.push_back('\0');
			payload += m_cfg.nickserv_password;
			enqueueWrite("AUTHENTICATE " + base64Encode(payload));
		}
		return;
	}

	if (msg.command == "903")
	{
		m_saslComplete = true;
		postStatus("SASL authentication successful");
		finishCapabilities();
		return;
	}
	if (msg.command == "902" || msg.command == "904" || msg.command == "905"
		|| msg.command == "906" || msg.command == "907")
	{
		postStatus("SASL failed (" + msg.command + "): " + msg.trailing());
		m_wantSasl = false;
		finishCapabilities();
		return;
	}

	if (msg.command == "001")
	{
		m_registered = true;
		if (msg.params.size() >= 1)
			m_currentNick = msg.params[0];
		postEvent(IrcNetEvent{ .type = IrcNetEventType::Registered, .nick = m_currentNick, .text = msg.trailing() });

		if (!m_saslComplete
			&& !m_cfg.nickserv_password.empty()
			&& m_cfg.auth_method != IrcAuthMethod::None)
		{
			sendNickServIdentify();
		}

		if (m_wantReclaim && !m_ghostFailed)
			beginNickReclaim();

		doAutoJoins();
		return;
	}

	// ERR_NICKNAMEINUSE
	if (msg.command == "433")
	{
		const std::string attempted = msg.params.size() >= 2 ? msg.params[1] : m_currentNick;
		handleNickInUse(attempted);
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::Numeric,
			.text = msg.trailing().empty() ? ("Nick already in use: " + attempted) : msg.trailing(),
			.numeric = 433,
		});
		return;
	}

	if (msg.command == "PRIVMSG" || msg.command == "NOTICE")
	{
		if (msg.params.size() < 2)
			return;
		const std::string& target = msg.params[0];
		std::string text = msg.params.back();
		IrcNetEventType type = (msg.command == "PRIVMSG") ? IrcNetEventType::Privmsg : IrcNetEventType::Notice;

		if (type == IrcNetEventType::Notice
			&& boost::iequals(msg.nick(), "NickServ"))
		{
			handleNickServNotice(text);
		}

		// CTCP ACTION
		if (type == IrcNetEventType::Privmsg
			&& text.size() >= 9
			&& text.front() == '\001'
			&& text.back() == '\001'
			&& text.rfind("\001ACTION ", 0) == 0)
		{
			text = text.substr(8, text.size() - 9);
			postEvent(IrcNetEvent{
				.type = IrcNetEventType::Privmsg,
				.channel = target,
				.nick = msg.nick(),
				.text = text,
				.numeric = 1, // mark as action
			});
			return;
		}

		postEvent(IrcNetEvent{
			.type = type,
			.channel = target,
			.nick = msg.nick(),
			.text = text,
		});
		return;
	}

	if (msg.command == "JOIN")
	{
		const std::string channel = msg.params.empty() ? msg.trailing() : msg.params[0];
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::Join,
			.channel = channel,
			.nick = msg.nick(),
		});
		if (boost::iequals(msg.nick(), m_currentNick))
			applyChanServ(channel);
		return;
	}

	if (msg.command == "PART")
	{
		const std::string channel = msg.params.empty() ? "" : msg.params[0];
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::Part,
			.channel = channel,
			.nick = msg.nick(),
			.text = msg.trailing(),
		});
		return;
	}

	if (msg.command == "QUIT")
	{
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::Quit,
			.nick = msg.nick(),
			.text = msg.trailing(),
		});
		return;
	}

	if (msg.command == "NICK")
	{
		const std::string new_nick = msg.params.empty() ? msg.trailing() : msg.params[0];
		if (boost::iequals(msg.nick(), m_currentNick))
			m_currentNick = new_nick;
		if (boost::iequals(new_nick, m_desiredNick))
		{
			m_wantReclaim = false;
			cancelNickReclaim();
			postStatus("Nick reclaimed: " + new_nick);
		}
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::NickChange,
			.nick = msg.nick(),
			.text = new_nick,
		});
		return;
	}

	if (msg.command == "TOPIC")
	{
		const std::string channel = msg.params.empty() ? "" : msg.params[0];
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::Topic,
			.channel = channel,
			.nick = msg.nick(),
			.text = msg.trailing(),
		});
		return;
	}

	// RPL_NAMREPLY
	if (msg.command == "353")
	{
		// :server 353 nick [=*@] #chan :[prefixes]nick1 ...
		// Be tolerant of servers that omit the channel-type symbol.
		if (msg.params.size() < 3)
			return;

		std::string channel;
		for (size_t i = 1; i + 1 < msg.params.size(); ++i)
		{
			const std::string& p = msg.params[i];
			if (!p.empty() && (p[0] == '#' || p[0] == '&' || p[0] == '+' || p[0] == '!'))
			{
				channel = p;
				break;
			}
		}
		if (channel.empty())
			return;

		std::vector<std::string> tokens;
		boost::split(tokens, msg.params.back(), boost::is_any_of(" "), boost::token_compress_on);

		std::vector<std::string> nicks;
		nicks.reserve(tokens.size());
		for (const auto& tok : tokens)
		{
			if (tok.empty())
				continue;
			// Keep raw token (prefixes + optional userhost); UI layer parses it.
			nicks.push_back(tok);
		}
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::Names,
			.channel = channel,
			.nicks = std::move(nicks),
		});
		return;
	}

	// RPL_ENDOFNAMES
	if (msg.command == "366")
	{
		const std::string channel = msg.params.size() >= 2 ? msg.params[1] : "";
		postEvent(IrcNetEvent{ .type = IrcNetEventType::NamesEnd, .channel = channel });
		// Ask for WHO to catch anyone NAMES missed (services, etc.)
		if (!channel.empty())
			enqueueWrite("WHO " + channel);
		return;
	}

	// RPL_WHOREPLY :server 352 me channel user host server nick flags :hops realname
	if (msg.command == "352")
	{
		if (msg.params.size() < 6)
			return;
		const std::string& channel = msg.params[1];
		const std::string& nick = msg.params[5];
		std::string prefixes;
		if (msg.params.size() >= 7)
		{
			const std::string& flags = msg.params[6];
			for (char c : flags)
			{
				if (c == '~' || c == '&' || c == '@' || c == '%' || c == '+' || c == '!')
					prefixes.push_back(c);
			}
		}
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::WhoEntry,
			.channel = channel,
			.nick = nick,
			.text = prefixes,
		});
		return;
	}

	// RPL_ENDOFWHO — nothing special; WhoEntry already merged
	if (msg.command == "315")
		return;

	// RPL_LISTSTART
	if (msg.command == "321")
	{
		postEvent(IrcNetEvent{ .type = IrcNetEventType::ListStart });
		return;
	}

	// RPL_LIST :server 322 nick #channel usercount :topic
	if (msg.command == "322")
	{
		if (msg.params.size() < 3)
			return;
		int users = 0;
		try { users = std::stoi(msg.params[2]); } catch (...) { users = 0; }
		postEvent(IrcNetEvent{
			.type = IrcNetEventType::ListEntry,
			.channel = msg.params[1],
			.text = msg.trailing(),
			.user_count = users,
		});
		return;
	}

	// RPL_LISTEND
	if (msg.command == "323")
	{
		postEvent(IrcNetEvent{ .type = IrcNetEventType::ListEnd, .text = msg.trailing() });
		return;
	}

	// Numerics / everything else -> status / numeric
	int num = 0;
	try { num = std::stoi(msg.command); } catch (...) { num = 0; }

	std::string text = msg.raw;
	if (!msg.params.empty())
	{
		std::ostringstream oss;
		for (size_t i = 1; i < msg.params.size(); ++i)
		{
			if (i > 1) oss << ' ';
			oss << msg.params[i];
		}
		if (!oss.str().empty())
			text = oss.str();
	}

	postEvent(IrcNetEvent{
		.type = num > 0 ? IrcNetEventType::Numeric : IrcNetEventType::Status,
		.text = text,
		.numeric = num,
	});
}

void IrcConnection::enqueueWrite(std::string line)
{
	if (!m_alive && line.rfind("QUIT", 0) != 0)
		return;

	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	line.append("\r\n");

	BOOST_LOG_TRIVIAL(debug) << "[IRC >>] " << stripCr(line.substr(0, line.size() - 1));

	m_outq.push_back(std::move(line));
	if (!m_writing)
		doWrite();
}

void IrcConnection::doWrite()
{
	if (m_outq.empty() || (!m_socket && !m_ssl))
	{
		m_writing = false;
		return;
	}

	m_writing = true;
	auto self = shared_from_this();
	const std::string& data = m_outq.front();

	auto handler = [self](const boost::system::error_code& ec, std::size_t)
		{
			if (ec)
			{
				self->m_writing = false;
				if (self->m_alive)
				{
					self->postEvent(IrcNetEvent{
						.type = IrcNetEventType::Error,
						.text = "Write failed: " + ec.message(),
					});
					self->m_alive = false;
				}
				return;
			}
			self->m_outq.pop_front();
			if (!self->m_outq.empty())
				self->doWrite();
			else
				self->m_writing = false;
		};

	if (m_ssl)
	{
		boost::asio::async_write(*m_ssl, boost::asio::buffer(data),
			boost::asio::bind_executor(m_strand, handler));
	}
	else
	{
		boost::asio::async_write(*m_socket, boost::asio::buffer(data),
			boost::asio::bind_executor(m_strand, handler));
	}
}
