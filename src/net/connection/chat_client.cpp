#include "net/connection/chat_client.h"

#include <algorithm>
#include <format>
#include <mutex>

#include "core/log.h"
#include "net/connection/ucs_endpoint.h"
#include "net/session/factory.h"
#include "platform/udp_socket.h"

namespace hailstone::net {

namespace {

std::vector<std::string> channel_names(std::string_view csv) {
    std::vector<std::string> names;
    while (!csv.empty()) {
        const auto delimiter = csv.find(',');
        auto name = csv.substr(0, delimiter);
        while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
        while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
        names.emplace_back(name);
        if (delimiter == std::string_view::npos) break;
        csv.remove_prefix(delimiter + 1);
    }
    return names;
}

std::size_t event_bytes(const ChatServerEvent& e) {
    std::size_t bytes = sizeof(e) + e.channel.size() + e.sender.size() + e.text.size();
    for (const auto& f : e.fields) bytes += sizeof(f) + f.size();
    return bytes;
}

} // namespace

struct ChatServer::Resolve {
    std::mutex mutex;
    bool done = false;
    std::vector<platform::Endpoint> endpoints;
    std::string error;
};

ChatServer::ChatServer() : ChatServer(Options{}) {}
ChatServer::ChatServer(Options options) : options_(std::move(options)) {}
ChatServer::~ChatServer() { disconnect(); }

ChatServer::Clock::time_point ChatServer::now() const {
    return options_.session.now();
}

void ChatServer::connect(const ChatServerInfo& info) {
    disconnect();
    info_ = info;
    error_.clear();
    const auto mailbox = "SOE.EQ." + info.server_short_name + "." + info.character;
    // The stock UCS copies the mailbox and key into char[64]. Refuse an identity it cannot safely read rather
    // than truncating a character's name into another identity. Embedded NULs would change the fields on wire.
    if (info.host.empty() || info.port == 0 || info.character.empty() || mailbox.size() > 63 ||
        info.mail_key.size() > 62 || info.host.find('\0') != std::string::npos ||
        mailbox.find('\0') != std::string::npos || info.mail_key.find('\0') != std::string::npos) {
        error_ = "invalid chat server endpoint or identity";
        return;
    }
    active_ = true;
    retries_ = 0;
    begin_attempt();
}

void ChatServer::begin_attempt() {
    retry_pending_ = false;
    channels_until_disconnect_polled_ = false;
    attempt_started_ = now();
    last_probe_ = attempt_started_;
    login_sent_ = false;
    logged_in_ = false;
    // CEverQuest::ChatServerConnect runs while the player is in the zone: the name (from OP_SetChatServer2) is looked
    // up on a thread, and update() opens the session on its address. A lookup given up on (disconnect, another
    // connect) finishes on its own and is dropped; one still running at exit is left to the process's end
    // (resolve_async).
    auto r = std::make_shared<Resolve>();
    resolving_ = r;
    platform::resolve_async(info_.host, info_.port, [r](std::vector<platform::Endpoint> endpoints, std::string error) {
        std::lock_guard lock(r->mutex);
        r->endpoints = std::move(endpoints);
        r->error = std::move(error);
        r->done = true;
    });
    log::info("chat server: connecting to {}:{} as {}.{}", info_.host, info_.port, info_.server_short_name, info_.character);
}

void ChatServer::start_session(const std::string& address, std::uint16_t port) {
    session::SessionRegistry registry;
    options_.session.protocol = options_.session_protocol;
    auto session = registry.create(options_.session_protocol, options_.session);
    if (!session) return fail("chat session protocol is unavailable: " + options_.session_protocol, false);
    endpoint_ = std::make_unique<UcsEndpoint>(std::move(session));
    endpoint_->connect(address, port); // a numeric address: no lookup on this thread
}

bool ChatServer::connected() const { return endpoint_ && logged_in_ && endpoint_->state() == SessionState::Connected; }

bool ChatServer::failed() const {
    if (resolving_) return false;
    return !endpoint_ || endpoint_->state() == SessionState::Disconnected;
}

void ChatServer::fail(std::string error, bool retry) {
    error_ = std::move(error);
    log::warn("chat server: {}", error_);
    resolving_.reset();
    endpoint_.reset();
    login_sent_ = logged_in_ = false;
    pending_.clear();
    pending_bytes_ = 0;
    // Only the unauthenticated rows are dropped. Events accepted after the login (tells, channel text received
    // earlier in this update) are still delivered, before the Disconnected that retires the session; the channel
    // list they are numbered against stays until that Disconnected is polled.
    before_login_.clear();
    before_login_bytes_ = 0;
    if (events_.empty()) channels_.clear();
    else channels_until_disconnect_polled_ = true;
    ChatServerEvent event;
    event.kind = ChatServerEvent::Kind::Disconnected;
    events_.push_back(std::move(event));
    retry_pending_ = active_ && retry && options_.reconnect_delay.count() > 0 && retries_ < options_.max_reconnect_attempts;
    if (retry_pending_) {
        // Hailstone's back-off: 5, 10, 20, 40 s ... capped, so a server that keeps failing is not hammered.
        auto delay = options_.reconnect_delay;
        for (std::size_t i = 0; i < retries_ && delay < options_.max_reconnect_delay; ++i) delay *= 2;
        retry_at_ = now() + std::min(delay, std::max(options_.max_reconnect_delay, options_.reconnect_delay));
        ++retries_;
    } else {
        active_ = false;
    }
}

void ChatServer::accept_event(ChatServerEvent event) {
    if (event.kind == ChatServerEvent::Kind::Channels) {
        channels_ = channel_names(event.text);
        channels_until_disconnect_polled_ = false;
    }
    events_.push_back(std::move(event));
}

void ChatServer::update() {
    if (retry_pending_) {
        if (now() < retry_at_) return;
        begin_attempt();
    }
    if (resolving_) {
        if (now() - attempt_started_ >= options_.connect_timeout)
            return fail("chat server lookup timed out", true);
        std::vector<platform::Endpoint> endpoints;
        std::string error;
        {
            std::lock_guard lock(resolving_->mutex);
            if (!resolving_->done) return;
            endpoints = std::move(resolving_->endpoints);
            error = std::move(resolving_->error);
        }
        resolving_.reset();
        if (endpoints.empty()) {
            return fail(error.empty() ? "could not resolve the chat server" : error, true);
        }
        start_session(endpoints.front().address_string(), endpoints.front().port);
    }
    if (!endpoint_) return;
    endpoint_->update();
    while (auto issue = endpoint_->receive_issue()) {
        if (issue->kind == messages::CodecErrorKind::UnsupportedOpcode) {
            log::debug("UCS: ignored unknown application message");
            continue;
        }
        if (issue->mail_login_reply && login_sent_ && !logged_in_)
            return fail("malformed UCS mailbox login reply", true);
        // A corrupt optional notice must not consume a pending login or a
        // queued command, and must not masquerade as a valid mailbox list.
        log::warn("UCS: rejected malformed application message: {}", issue->detail);
    }
    if (endpoint_->state() == SessionState::Disconnected) {
        // Stock UCS answers a key or address mismatch by closing the stream, never with a refusal packet
        // (EQEmu ucs/clientlist.cpp Clientlist::Process: KeyValid = false, then ClientStream->Close()). A close after
        // the login went out and before any reply is therefore a refusal: retrying would only repeat it.
        if (login_sent_ && !logged_in_) return fail("the chat server closed the link before answering the login", false);
        return fail(endpoint_->error().empty() ? "the chat server disconnected" : endpoint_->error(), true);
    }
    if (!login_sent_ && endpoint_->state() == SessionState::Connecting &&
        now() - attempt_started_ >= options_.connect_timeout)
        return fail("chat server connection timed out", true);
    if (!login_sent_ && endpoint_->state() == SessionState::Connected) {
        messages::UcsCommand login{};
        login.kind = messages::UcsCommandKind::Login;
        login.identity = info_;
        if (!endpoint_->send(login)) return fail("UCS login encoder rejected the credentials", false);
        login_sent_ = true;
        login_started_ = now();
    }
    while (auto e = endpoint_->receive()) {
        if (e->kind == ChatServerEvent::Kind::MailboxList) {
            if (!login_sent_) continue;
            if (!e->accepted) return fail("the chat server refused the login", false);
            if (logged_in_) continue; // a repeated application reply does not replay commands or reset mail
            logged_in_ = true;
            retries_ = 0;
            error_.clear();
            accept_event(std::move(*e));
            while (!before_login_.empty()) {
                accept_event(std::move(before_login_.front()));
                before_login_.pop_front();
            }
            before_login_bytes_ = 0;
            while (!pending_.empty()) {
                if (!endpoint_->send(pending_.front()))
                    return fail("the chat server rejected a pending command", true);
                pending_.pop_front();
            }
            pending_bytes_ = 0;
        } else if (!logged_in_) {
            const auto bytes = event_bytes(*e);
            if (before_login_.size() >= options_.max_before_login_events ||
                bytes > options_.max_before_login_bytes - before_login_bytes_)
                return fail("too many chat events before the login reply", true);
            before_login_bytes_ += bytes;
            before_login_.push_back(std::move(*e));
        } else {
            accept_event(std::move(*e));
        }
    }
    if (login_sent_ && !logged_in_ && now() - login_started_ >= options_.login_timeout)
        return fail("the chat server did not answer the login", true);
    // UniversalChatProxy checks the age of the oldest sent, unacknowledged packet, not time since any traffic.
    // Keepalives and unrelated inbound messages therefore cannot hide a stuck command.
    if (login_sent_ && now() - last_probe_ > std::chrono::milliseconds(5000)) {
        last_probe_ = now();
        if (endpoint_->stats().oldest_unacked_ms > 30000)
            return fail("the chat server did not acknowledge reliable data", true);
    }
}

bool ChatServer::queue_command(messages::UcsCommand command) {
    if (!active_ || retry_pending_ || (!endpoint_ && !resolving_)) return false;
    // The codec reports the exact on-wire body size including the application
    // packet object overhead, without exposing the encoded data to this layer.
    auto bytes = UcsEndpoint::wire_cost(command);
    if (!bytes || *bytes > options_.max_pending_bytes) return false;
    if (!logged_in_) {
        if (pending_.size() >= options_.max_pending_packets ||
            *bytes > options_.max_pending_bytes - pending_bytes_) return false;
        pending_bytes_ += *bytes;
        pending_.push_back(std::move(command));
        return true;
    }
    return endpoint_ && endpoint_->state() == SessionState::Connected && endpoint_->send(command);
}

bool ChatServer::send_command(const std::string& text) {
    if (text.size() > options_.max_pending_bytes || text.find('\0') != std::string::npos) return false;
    messages::UcsCommand command{};
    command.kind = messages::UcsCommandKind::ChatText;
    command.text = text;
    return queue_command(std::move(command));
}

bool ChatServer::send_mail_command(const std::string& text) {
    if (text.size() > options_.max_pending_bytes || text.find('\0') != std::string::npos) return false;
    messages::UcsCommand command{};
    command.kind = messages::UcsCommandKind::MailText;
    command.text = text;
    return queue_command(std::move(command));
}

bool ChatServer::send_mail_request(const MailRequest& request) {
    messages::UcsCommand command{};
    command.kind = messages::UcsCommandKind::MailRequest;
    command.mail = request;
    return queue_command(std::move(command));
}

std::optional<ChatServerEvent> ChatServer::poll() {
    if (events_.empty()) return std::nullopt;
    auto e = std::move(events_.front());
    events_.pop_front();
    if (e.kind == ChatServerEvent::Kind::Disconnected && channels_until_disconnect_polled_) {
        channels_.clear();
        channels_until_disconnect_polled_ = false;
    }
    return e;
}

void ChatServer::simulate_link_loss() {
    if (endpoint_) endpoint_->simulate_link_loss();
}

void ChatServer::simulate_socket_loss() {
    if (endpoint_) endpoint_->simulate_socket_loss();
}

void ChatServer::disconnect() {
    active_ = retry_pending_ = false;
    resolving_.reset();
    // Destroy the old socket and its reliable queue before accepting another endpoint or identity.
    endpoint_.reset();
    login_sent_ = logged_in_ = false;
    pending_.clear();
    pending_bytes_ = 0;
    before_login_.clear();
    before_login_bytes_ = 0;
    events_.clear();
    channels_.clear();
    channels_until_disconnect_polled_ = false;
}

} // namespace hailstone::net
