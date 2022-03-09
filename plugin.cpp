// Copyright 2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "mumble/plugin/MumblePlugin.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <future>

#include "server_ws.hpp"
#include "nlohmann/json.hpp"

#define FLAG(expr, flag) ((expr) & (flag) ? true : false)

using namespace std::literals::string_literals;
using namespace nlohmann;

using WsServer = SimpleWeb::SocketServer<SimpleWeb::WS>;
using Endpoint = SimpleWeb::SocketServerBase<SimpleWeb::WS>::Endpoint;
using WsConnection = std::shared_ptr< WsServer::Connection >;

class WebsocketApiPlugin : public MumblePlugin {

public:
	static constexpr const char *PLUGIN_NAME   = "Websocket API Plugin";
	static constexpr const char *PLUGIN_AUTHOR = "Goujon";
	static constexpr const char *PLUGIN_DESCRIPTION =
		"Websocket API for receiving events and querying client state";

	static constexpr const unsigned short WS_PORT = 51966;
	static constexpr const char* WS_ENDPOINT_PATH = "mumble_events";

private:
	bool synchronized = false;

	Endpoint *wsEndpoint;
	WsServer wsServer;
	std::promise< unsigned short > wsServerPort;
	std::thread *wsServerThread;

public:
	WebsocketApiPlugin() : MumblePlugin(PLUGIN_NAME, PLUGIN_AUTHOR, PLUGIN_DESCRIPTION) {
	}

	~WebsocketApiPlugin() {
		wsDisconnect();
	}

	mumble_error_t init() noexcept {
		wsInit();
		return MUMBLE_STATUS_OK;
	}

	bool isAlive(mumble_connection_t connection) {
		return (connection >= 0 && synchronized);
	}

	virtual void onServerSynchronized(mumble_connection_t connection) noexcept override {
		try {
			synchronized = true;
			wsBroadcast(jsonMessage("event/connected"));
		}
		catch (MumbleAPIException e) {
			handleApiException("onServerSynchronized", e);
		}
	}

	virtual void onServerDisconnected(mumble_connection_t connection) noexcept override {
		try {
			synchronized = false;
			wsBroadcast(jsonMessage("event/disconnected"));
		}
		catch (MumbleAPIException e) {
			handleApiException("onServerDisconnected", e);
		}
	}

	virtual void onChannelEntered(mumble_connection_t connection, mumble_userid_t userID,
								  mumble_channelid_t previousChannelID, mumble_channelid_t newChannelID) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg    = jsonMessage("event/channel_entered");
			msg["user"] = jsonGetUser(connection, userID);
			if (previousChannelID >= 0) {
				msg["fromChannel"] = jsonGetChannel(connection, previousChannelID);
			} else {
				msg["fromChannel"] = nullptr;
			}
			msg["toChannel"] = jsonGetChannel(connection, newChannelID);
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onChannelEntered", e);
		}
	}
	
	virtual void onChannelExited(mumble_connection_t connection, mumble_userid_t userID,
								 mumble_channelid_t channelID) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg       = jsonMessage("event/channel_exited");
			msg["user"]    = jsonGetUser(connection, userID);
			msg["channel"] = jsonGetChannel(connection, channelID);
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onChannelExited", e);
		}
	}

	virtual void onUserMuteDeafStateChanged(mumble_connection_t connection, mumble_userid_t userID,
											mumble_mutedeaf_state_t muteDeafState) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg    = jsonMessage("event/user_mute_deafen_state_changed");
			msg["user"] = jsonGetUser(connection, userID);
			fillMuteDeafState(msg, muteDeafState);
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onUserMuteDeafStateChanged", e);
		}
	}

	virtual void onUserTalkingStateChanged(mumble_connection_t connection, mumble_userid_t userID,
										   mumble_talking_state_t talkingState) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg            = jsonMessage("event/user_talking_state_changed");
			msg["user"]         = jsonGetUser(connection, userID);
			msg["talkingState"] = talkingState;
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onUserTalkingStateChanged", e);
		}
	}

	virtual void onUserAdded(mumble_connection_t connection, mumble_userid_t userID) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg    = jsonMessage("event/user_added");
			msg["user"] = jsonGetUser(connection, userID);
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onUserAdded", e);
		}
	}

	virtual void onUserRemoved(mumble_connection_t connection, mumble_userid_t userID) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg  = jsonMessage("event/user_removed");
			msg["id"] = userID;
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onUserRemoved", e);
		}
	}

	virtual void onChannelAdded(mumble_connection_t connection, mumble_channelid_t channelID) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg = jsonMessage("event/channel_added");
			msg["channel"] = jsonGetChannel(connection, channelID);
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onChannelAdded", e);
		}
	}

	virtual void onChannelRemoved(mumble_connection_t connection, mumble_channelid_t channelID) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg  = jsonMessage("event/channel_removed");
			msg["id"] = channelID;
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onChannelRemoved", e);
		}
	}

	virtual void onChannelRenamed(mumble_connection_t connection, mumble_channelid_t channelID) noexcept override {
		try {
			if (!isAlive(connection)) {
				return;
			}
			auto msg       = jsonMessage("event/channel_renamed");
			msg["channel"] = jsonGetChannel(connection, channelID);
			wsBroadcast(msg);
		}
		catch (MumbleAPIException e) {
			handleApiException("onChannelRenamed", e);
		}
	}

	virtual void releaseResource(const void *ptr) noexcept override {
		// We don't allocate any resources so we can have a no-op implementation
		// We'll terminate though in case it is called as that is definitely a bug
		std::terminate();
	}

	void handleMessage(WsConnection connection, json &msg) {
		auto type = msg["type"].get< std::string >();
		try {
			if (type.compare("request/local_user_id") == 0) {
				wsSendLocalUserId(connection);
			} else if (type.compare("request/local_user_state") == 0) {
				wsSendLocalUserState(connection);
			} else if (type.compare("request/channels") == 0) {
				wsSendChannels(connection);
			} else if (type.compare("request/change_channel") == 0) {
				handleChangeChannel(msg["channelId"].get< mumble_channelid_t >());
			} else if (type.compare("request/is_connected") == 0) {
				auto msg  = jsonMessage("response/is_connected");
				msg["connected"] = synchronized;
				wsSendJson(connection, msg);
			} else if (type.compare("request/set_muted") == 0) {
				m_api.requestLocalUserMute(msg["muted"]);
				auto msg = jsonMessage("response/set_muted");
				wsSendJson(connection, msg);
			} else if (type.compare("request/set_deafened") == 0) {
				m_api.requestLocalUserDeaf(msg["deafened"]);
				m_api.requestLocalUserMute(msg["deafened"]);
				auto msg = jsonMessage("response/set_deafened");
				wsSendJson(connection, msg);
			} else if (type.compare("request/user_mute_deafen_state") == 0) {
				wsSendUserMuteDeafState(connection, msg["id"].get< mumble_userid_t >());
			} else if (type.compare("request/set_local_mute") == 0) {
				handleSetLocalMute(msg["id"].get< mumble_userid_t >(), msg["enable"].get< bool >());
			}
		}
		catch (MumbleAPIException e) {
			handleApiException("handleMessage[" + type + "]", e);
		}
	}

	void handleChangeChannel(mumble_channelid_t channelID) {
		try {
			mumble_connection_t connection = m_api.getActiveServerConnection();
			if (!isAlive(connection)) {
				return;
			}
			mumble_userid_t userID = m_api.getLocalUserID(connection);
			m_api.requestUserMove(connection, userID, channelID);
		}
		catch (MumbleAPIException e) {
			handleApiException("handleChangeChannel", e);
		}
	}

	void handleSetLocalMute(mumble_userid_t userID, bool enable) {
		try {
			mumble_connection_t connection = m_api.getActiveServerConnection();
			if (!isAlive(connection)) {
				return;
			}
			m_api.requestLocalMute(connection, userID, enable);
		}
		catch (MumbleAPIException e) {
			handleApiException("handleSetLocalMute", e);
		}
	}

private:

	//
	// JSON
	//

	json jsonGetChannels(mumble_connection_t connection) {
		std::map< mumble_channelid_t, std::vector< mumble_userid_t > > channelUsers;
		for (auto userID : m_api.getAllUsers(connection)) {
			const auto channelID = m_api.getChannelOfUser(connection, userID);
			channelUsers[channelID].push_back(userID);
		}
		auto channels = json::array();
		for (auto channelId : m_api.getAllChannels(connection)) {
			auto channel = jsonGetChannel(connection, channelId);

			auto users = json::array();
			for (auto userId : channelUsers[channelId]) {
				users.push_back(jsonGetUser(connection, userId));
			}
			channel["users"] = users;

			channels.push_back(channel);
		}
		return channels;
	}

	json jsonGetChannel(mumble_connection_t connection, mumble_channelid_t channelID) {
		auto channel        = json::object();
		channel["id"]       = channelID;
		channel["name"]     = m_api.getChannelName(connection, channelID);
		channel["parentId"] = m_api.getParentChannelID(connection, channelID);
		return channel;
	}

	json jsonGetUser(mumble_connection_t connection, mumble_userid_t userID) {
		mumble_mutedeaf_state_t muteDeafState = m_api.getUserMuteDeafState(m_api.getActiveServerConnection(), userID);
		auto user            = json::object();
		user["id"]           = userID;
		user["name"]         = m_api.getUserName(connection, userID);
		fillMuteDeafState(user, muteDeafState);
		return user;
	}

	json jsonMessage(std::string type) {
		auto msg    = json::object();
		msg["type"] = type;
		return msg;
	}

	//
	// Web Sockets
	//

	void wsInit() {
		wsServer.config.port = WS_PORT;
		wsEndpoint = &wsServer.endpoint["^/"s + WS_ENDPOINT_PATH + "/?$"s];

		wsEndpoint->on_message = [this](WsConnection connection, std::shared_ptr< WsServer::InMessage > in_message) {
			if (!synchronized) {
				return;
			}

			try {
				auto input = json::parse(in_message->string());
				if (input.contains("type")) {
					handleMessage(connection, input);
				}
			}
			catch (...) {
				throw MumbleAPIException(MUMBLE_EC_GENERIC_ERROR, "Could not process WS JSON message:\n"s + in_message->string());
			}

			auto out_message = in_message->string();
		};

		// Can modify handshake response headers here if needed
		wsEndpoint->on_handshake = [](WsConnection /*connection*/,
			SimpleWeb::CaseInsensitiveMultimap& /*response_header*/) {
				return SimpleWeb::StatusCode::information_switching_protocols; // Upgrade to websocket
		};

		// See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code
		// meanings
		wsEndpoint->on_error = [this](WsConnection connection, const SimpleWeb::error_code& ec) {
			wsErrorHandler(this, ec);
		};

		wsServerThread = new std::thread([this]() {
			wsServer.start([this](unsigned short port) {
				wsServerPort.set_value(port);
			});
		});

		m_api.log(("Server listening on port "s + std::to_string(wsServerPort.get_future().get())).c_str());
	}

	void wsDisconnect() {
		wsServer.stop();
	}

	void wsBroadcast(std::string& message) {
		if (wsEndpoint == NULL) {
			return;
		}
		for (const auto conn : wsEndpoint->get_connections()) {
			conn.get()->send(message, [this](const SimpleWeb::error_code& ec) {
				wsErrorHandler(this, ec);
			});
		}
	}

	void wsBroadcast(json &json) {
		wsBroadcast(json.dump());
	}

	void wsSendJson(WsConnection wsConnection, json &json) {
		wsConnection->send(json.dump(), [this](const SimpleWeb::error_code& ec) {
			wsErrorHandler(this, ec);
		});
	}

	void wsSendLocalUserId(WsConnection wsConnection) {
		json msg  = jsonMessage("response/local_user_id");
		msg["id"] = m_api.getLocalUserID(m_api.getActiveServerConnection());
		wsSendJson(wsConnection, msg);
	}

	void wsSendLocalUserState(WsConnection wsConnection) {
		json msg        = jsonMessage("response/local_user_state");
		msg["muted"]    = m_api.isLocalUserMuted();
		msg["deafened"] = m_api.isLocalUserDeafened();
		wsSendJson(wsConnection, msg);
	}

	void wsSendUserMuteDeafState(WsConnection wsConnection, mumble_userid_t userID) {
		mumble_mutedeaf_state_t muteDeafState = m_api.getUserMuteDeafState(m_api.getActiveServerConnection(), userID);
		json msg  = jsonMessage("response/user_mute_deafen_state");
		msg["id"] = userID;
		fillMuteDeafState(msg, muteDeafState);
		wsSendJson(wsConnection, msg);
	}

	void wsSendChannels(WsConnection wsConnection) {
		json msg        = jsonMessage("response/channels");
		msg["channels"] = jsonGetChannels(m_api.getActiveServerConnection());
		wsSendJson(wsConnection, msg);
	}

	void fillMuteDeafState(json &msg, mumble_mutedeaf_state_t muteDeafState) {
		msg["muted"]           = FLAG(muteDeafState, MDS_MUTE);
		msg["deafened"]        = FLAG(muteDeafState, MDS_DEAF);
		msg["suppressed"]      = FLAG(muteDeafState, MDS_SUPPRESS);
		msg["selfMuted"]       = FLAG(muteDeafState, MDS_SELF_MUTE);
		msg["selfDeafened"]    = FLAG(muteDeafState, MDS_SELF_DEAF);
		msg["locallyMuted"]    = FLAG(muteDeafState, MDS_LOCAL_MUTE);
		msg["locallyIgnored"]  = FLAG(muteDeafState, MDS_LOCAL_IGNORE);
		msg["prioritySpeaker"] = FLAG(muteDeafState, MDS_PRIORITY_SPEAKER);
		msg["recording"]       = FLAG(muteDeafState, MDS_RECORDING);
	}

	//
	// MISC
	//

	void handleApiException(const std::string& where, const MumbleAPIException& e) const {
		m_api.log((where + ": ("s + std::to_string(e.errorCode()) + ") "s + e.what()).c_str());
	}

	void (*wsErrorHandler)(WebsocketApiPlugin* plugin, const SimpleWeb::error_code& ec) =
		[](WebsocketApiPlugin* plugin, const SimpleWeb::error_code& ec) {
		if (ec) {
			throw MumbleAPIException(MUMBLE_EC_GENERIC_ERROR,
				"WebSocket connection error ("s + std::to_string(ec.value()) + "): "s + ec.message());
		}
	};

	virtual void dumpState(mumble_connection_t connection) noexcept {
		std::ostringstream ss;
		try {
			ss << "Server " << m_api.getServerHash(connection) << " finished synchronizing" << std::endl;
			std::map< mumble_channelid_t, std::vector< mumble_userid_t > > channelUsers;
			for (auto userId : m_api.getAllUsers(connection)) {
				const auto channelId = m_api.getChannelOfUser(connection, userId);
				channelUsers[channelId].push_back(userId);
			}
			for (auto channelId : m_api.getAllChannels(connection)) {
				ss << m_api.getChannelName(connection, channelId) << std::endl;
				for (auto userId : channelUsers[channelId]) {
					ss << "  - " << m_api.getUserName(connection, userId) << std::endl;
				}
			}
			m_api.log(ss.str().c_str());
		} catch (const MumbleAPIException &e) {
			std::cerr << "dumpState: " << e.what() << " (ErrorCode " << e.errorCode() << ")" << std::endl;
		}
	}
};

MumblePlugin &MumblePlugin::getPlugin() noexcept {
	static WebsocketApiPlugin plugin;

	return plugin;
}
