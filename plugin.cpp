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

	static constexpr const char *LOG_PATH = "R:\\mumble.log";

	static constexpr const short WS_PORT          = 8080;
	static constexpr const char *WS_ENDPOINT_PATH = "mumble_events";

private:
	std::ofstream logs;

	bool synchronized = false;

	Endpoint *wsEndpoint;
	WsServer wsServer;
	std::promise< unsigned short > wsServerPort;
	std::thread *wsServerThread;

	void (*wsErrorHandler)(WebsocketApiPlugin* plugin, const SimpleWeb::error_code &ec) =
		[](WebsocketApiPlugin* plugin, const SimpleWeb::error_code &ec) {
			if (ec) {
				plugin->getLogs() << "Server: Error sending message. " <<
					// See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for
					// error code meanings
					"Error: " << ec << ", error message: " << ec.message() << std::endl;
			}
		};
	/*
		USAGE:
	 
		connection->send(out_message, [this](const SimpleWeb::error_code &ec) {
			wsErrorHandler(this, ec);
		});	
	*/
public:
	WebsocketApiPlugin() : MumblePlugin(PLUGIN_NAME, PLUGIN_AUTHOR, PLUGIN_DESCRIPTION) {
		logs.rdbuf()->pubsetbuf(0, 0);
		logs.open(LOG_PATH, std::ofstream::out | std::ofstream::app);

		wsInit();
	}

	~WebsocketApiPlugin() {
		wsDisconnect();
		logs.close();
	}

	bool isAlive(mumble_connection_t connection) {
		return (connection >= 0 && synchronized);
	}

	std::ofstream& getLogs() {
		return logs;
	}
	
	virtual void onServerSynchronized(mumble_connection_t connection) noexcept override {
		synchronized = true;
		wsBroadcast(jsonMessage("event/connected"));
	}

	virtual void onServerDisconnected(mumble_connection_t connection) noexcept override {
		synchronized = false;
		wsBroadcast(jsonMessage("event/disconnected"));
	}

	virtual void onChannelEntered(mumble_connection_t connection, mumble_userid_t userID,
								  mumble_channelid_t previousChannelID, mumble_channelid_t newChannelID) noexcept override {
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
	
	virtual void onChannelExited(mumble_connection_t connection, mumble_userid_t userID,
								 mumble_channelid_t channelID) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg       = jsonMessage("event/channel_exited");
		msg["user"]    = jsonGetUser(connection, userID);
		msg["channel"] = jsonGetChannel(connection, channelID);
		wsBroadcast(msg);
	}

	virtual void onUserMuteDeafStateChanged(mumble_connection_t connection, mumble_userid_t userID,
											mumble_mutedeaf_state_t muteDeafState) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg    = jsonMessage("event/user_mute_deafen_state_changed");
		msg["user"] = jsonGetUser(connection, userID);
		fillMuteDeafState(msg, muteDeafState);
		wsBroadcast(msg);
	}

	virtual void onUserTalkingStateChanged(mumble_connection_t connection, mumble_userid_t userID,
										   mumble_talking_state_t talkingState) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg            = jsonMessage("event/user_talking_state_changed");
		msg["user"]         = jsonGetUser(connection, userID);
		msg["talkingState"] = talkingState;
		wsBroadcast(msg);
	}

	virtual void onUserAdded(mumble_connection_t connection, mumble_userid_t userID) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg    = jsonMessage("event/user_added");
		msg["user"] = jsonGetUser(connection, userID);
		wsBroadcast(msg);
	}

	virtual void onUserRemoved(mumble_connection_t connection, mumble_userid_t userID) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg  = jsonMessage("event/user_removed");
		msg["id"] = userID;
		wsBroadcast(msg);
	}

	virtual void onChannelAdded(mumble_connection_t connection, mumble_channelid_t channelID) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg = jsonMessage("event/channel_added");
		msg["channel"] = jsonGetChannel(connection, channelID);
		wsBroadcast(msg);
	}

	virtual void onChannelRemoved(mumble_connection_t connection, mumble_channelid_t channelID) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg  = jsonMessage("event/channel_removed");
		msg["id"] = channelID;
		wsBroadcast(msg);
	}

	virtual void onChannelRenamed(mumble_connection_t connection, mumble_channelid_t channelID) noexcept override {
		if (!isAlive(connection)) {
			return;
		}
		auto msg       = jsonMessage("event/channel_renamed");
		msg["channel"] = jsonGetChannel(connection, channelID);
		wsBroadcast(msg);
	}

	virtual void releaseResource(const void *ptr) noexcept override {
		// We don't allocate any resources so we can have a no-op implementation
		// We'll terminate though in case it is called as that is definitely a bug
		std::terminate();
	}

	void handleMessage(WsConnection connection, json &msg) {
		auto type = msg["type"].get< std::string >();
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

	void handleChangeChannel(mumble_channelid_t channelID) {
		mumble_connection_t connection = m_api.getActiveServerConnection();
		if (!isAlive(connection)) {
			return;
		}
		mumble_userid_t userID = m_api.getLocalUserID(connection);
		m_api.requestUserMove(connection, userID, channelID);
	}

	void handleSetLocalMute(mumble_userid_t userID, bool enable) {
		mumble_connection_t connection = m_api.getActiveServerConnection();
		if (!isAlive(connection)) {
			return;
		}
		m_api.requestLocalMute(connection, userID, enable);
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

	void wsBroadcast(std::string& message) {
		if (wsEndpoint == NULL) {
			return;
		}
		for (const auto conn : wsEndpoint->get_connections()) {
			conn.get()->send(message);
		}
	}

	void wsBroadcast(json &json) {
		wsBroadcast(json.dump());
	}

	void wsSendJson(WsConnection wsConnection, json &json) {
		wsConnection->send(json.dump());
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

	void wsInit() {
		wsServer.config.port = WS_PORT;
		wsEndpoint           = &wsServer.endpoint["^/"s + WS_ENDPOINT_PATH + "/?$"s];

		wsEndpoint->on_message = [this](WsConnection connection, std::shared_ptr< WsServer::InMessage > in_message) {
			if (!synchronized) {
				return;
			}

			try {
				auto input = json::parse(in_message->string());
				if (input.contains("type")) {
					handleMessage(connection, input);
				}
			} catch (...) {
				logs << "Could not process WS JSON message:" << std::endl << in_message;
			}

            auto out_message = in_message->string();
		};

		// Can modify handshake response headers here if needed
		wsEndpoint->on_handshake = [](WsConnection /*connection*/,
							   SimpleWeb::CaseInsensitiveMultimap & /*response_header*/) {
			return SimpleWeb::StatusCode::information_switching_protocols; // Upgrade to websocket
		};

		// See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code
		// meanings
		wsEndpoint->on_error = [this](WsConnection connection, const SimpleWeb::error_code &ec) {
			logs << "Server: Error in connection " << connection.get() << ". "
				 << "Error: " << ec << ", error message: " << ec.message() << std::endl;
		};

		wsServerThread = new std::thread([this]() {
			// Start server
			wsServer.start([this](unsigned short port) { wsServerPort.set_value(port); });
		});

		logs << "Server listening on port " << wsServerPort.get_future().get() << std::endl << std::endl;
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

	void wsDisconnect() {
		wsServer.stop();
	}

	//
	// MISC
	//

	virtual void dumpState(mumble_connection_t connection) noexcept {
		try {
			logs << "Server " << m_api.getServerHash(connection) << " finished synchronizing" << std::endl;
			std::map< mumble_channelid_t, std::vector< mumble_userid_t > > channelUsers;
			for (auto userId : m_api.getAllUsers(connection)) {
				const auto channelId = m_api.getChannelOfUser(connection, userId);
				channelUsers[channelId].push_back(userId);
			}
			for (auto channelId : m_api.getAllChannels(connection)) {
				logs << m_api.getChannelName(connection, channelId) << std::endl;
				for (auto userId : channelUsers[channelId]) {
					logs << "  - " << m_api.getUserName(connection, userId) << std::endl;
				}
			}
		} catch (const MumbleAPIException &e) {
			std::cerr << "dumpState: " << e.what() << " (ErrorCode " << e.errorCode() << ")" << std::endl;
		}
	}
};

MumblePlugin &MumblePlugin::getPlugin() noexcept {
	static WebsocketApiPlugin plugin;

	return plugin;
}
