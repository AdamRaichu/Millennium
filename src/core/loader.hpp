#pragma once
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <string>
#include <nlohmann/json.hpp>
#include <winsock2.h>
#include <windows.h>
#include <generic/stream_parser.h>

void localize_python_runtime(stream_buffer::plugin_mgr::plugin_t plugin_name);
std::string get_plugin_frontend(std::string plugin_name);
static std::string sessionId;

struct shared {
	websocketpp::client
		<websocketpp::config::asio_client>* client;
	websocketpp::connection_hdl handle;
};

class plugin {
public:

	static plugin get() {
		static plugin _p;
		return _p;
	}

	void bootstrap();
};

namespace tunnel {
	bool post_shared(nlohmann::json data);
	bool post_global(nlohmann::json data);
}