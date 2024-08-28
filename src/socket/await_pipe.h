#include <sys/http.h>
#include <thread>
#include <nlohmann/json.hpp>
#include <filesystem>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#include <tchar.h>
#pragma comment(lib, "iphlpapi.lib")
#include <tlhelp32.h>
#include <psapi.h>
#elif __linux__
#include <fstream>
#include <sstream>
#elif __APPLE__
#include <cstdlib>
#include <cstdio>
#endif
#ifndef MILLENNIUM_CLI
// #include <boxer/boxer.h>
#endif
#include <procmon/cmd.h>

static bool bHasCheckedConnection = false;

class SocketHelpers
{
private:

    short debuggerPort;

    const short GetDebuggerPort()
    {
        #ifdef _WIN32
        {
            std::unique_ptr<StartupParameters> startupParams = std::make_unique<StartupParameters>();

            for (const auto& parameter : startupParams->GetArgumentList())
            {
                size_t pos = parameter.find('=');

                if (pos == std::string::npos)
                {
                    continue;
                }

                std::string key = parameter.substr(0, pos);
                std::string value = parameter.substr(pos + 1);

                if (key == "-devtools-port")
                {
                    try
                    {
                        return std::stoi(value);
                    }
                    catch (const std::invalid_argument& e) {
                        LOG_ERROR("failed to parse dev-tools port due to invalid argument. exception -> {}", e.what());
                    }
                    catch (const std::out_of_range& e) {
                        LOG_ERROR("failed to parse dev-tools port due to too large of an integer. exception -> {}", e.what());
                    }
                    catch (...) {
                        LOG_ERROR("failed to parse dev-tools port due to an unknown error. exception -> {}");
                    }
                }
            }
        }
        #endif
        return 8080;
    }

    const std::string GetDebuggerUrl()
    {
        return fmt::format("http://localhost:{}", debuggerPort);
    }

    struct SteamConnectionProps
    {
        bool hasConnection;
        std::string processName = {};
    };

#ifdef _WIN32
    std::string GetProcessName(DWORD processID) 
    {
        char Buffer[MAX_PATH];
        HANDLE Handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);

        if (Handle)
        {
            if (GetModuleFileNameExA(Handle, 0, Buffer, MAX_PATH))
            {
                return std::string(Buffer);
            }
            CloseHandle(Handle);
        }

        return {};
    }
#endif

    SteamConnectionProps GetSteamConnectionProps()
    {
#ifdef _WIN32
        // Windows-specific code
        DWORD size = 0;
        PMIB_TCPTABLE2 tcpTable;
        GetTcpTable2(nullptr, &size, TRUE);
        tcpTable = (PMIB_TCPTABLE2)malloc(size);

        if (GetTcpTable2(tcpTable, &size, TRUE) != NO_ERROR) 
        {
            free(tcpTable);
            return { false };
        }

        for (DWORD i = 0; i < tcpTable->dwNumEntries; i++)
        {
            if (ntohs((u_short)tcpTable->table[i].dwLocalPort) == debuggerPort)
            {
                const auto targetProcess = std::filesystem::path(GetProcessName((int)tcpTable->table[i].dwOwningPid));

                return { 
                    targetProcess.filename().string() == "steamwebhelper.exe",
                    targetProcess.string()
                };
            }
        }
#elif __linux__
        // Linux-specific code
        //std::ifstream tcpFile("/proc/net/tcp");
        //std::string line;

        //while (std::getline(tcpFile, line)) 
        //{
        //    std::istringstream iss(line);
        //    std::string localAddress;

        //    if (iss >> localAddress && localAddress.find(":" + std::to_string(debuggerPort)) != std::string::npos) 
        //    {
        //        // Extract and print process ID or details
        //        // This is a simplified example, more parsing required in real case

        //        return { true, {} };
        //    }
        //}
        return { true, {} };
#elif __APPLE__
        // macOS-specific code using lsof
        std::string command = "lsof -i :" + std::to_string(port) + " | grep LISTEN";
        FILE* fp = popen(command.c_str(), "r");
        if (fp) 
        {
            char buffer[128];
            while (fgets(buffer, sizeof(buffer), fp) != nullptr) 
            {
                result += buffer;
            }
            pclose(fp);
        }
#endif

        return { false };
    }

public:
    struct ConnectSocketProps
    {
        std::string commonName;
        std::function<std::string()> fetchSocketUrl;

        std::function<void(
            websocketpp::client<websocketpp::config::asio_client>*,
            websocketpp::connection_hdl)> onConnect;

        std::function<void(
            websocketpp::client<websocketpp::config::asio_client>*,
            websocketpp::connection_hdl,
            std::shared_ptr<websocketpp::config::core_client::message_type>)> onMessage;

        bool bAutoReconnect = true;
    };

    const void VerifySteamConnection()
    {
        #ifndef MILLENNIUM_CLI
        if (bHasCheckedConnection)
        {
            return;
        }

        auto [canConnect, processName] = this->GetSteamConnectionProps();
        if (!canConnect)
        {
            const std::string message = fmt::format(
                "Millennium can't connect to Steam because the target port '{}' is currently being used by '{}'.\n"
                "To address this you must uninstall/close the conflicting app, change the port it uses (assuming its possible), or uninstall Millennium.\n\n"
                "Millennium & Steam will now close until further action is taken.", debuggerPort, processName
            );

            #ifdef _WIN32
            MessageBoxA(nullptr, message.c_str(), "Fatal Error", MB_ICONERROR);
            #endif
            Logger.Warn(message);
            std::exit(1);
        }

        bHasCheckedConnection = true;
        #endif
    }

    SocketHelpers() : debuggerPort(GetDebuggerPort())
    {
        Logger.Log("Opting to use '{}' for SteamDBG port", debuggerPort);
        this->VerifySteamConnection();
    }

    const std::string GetSteamBrowserContext()
    {
        #ifndef MILLENNIUM_CLI
        try
        {
            std::string browserUrl = fmt::format("{}/json/version", this->GetDebuggerUrl());
            nlohmann::basic_json<> instance = nlohmann::json::parse(Http::Get(browserUrl.c_str()));

            return instance["webSocketDebuggerUrl"];
        }
        catch (nlohmann::detail::exception& exception)
        {
            LOG_ERROR(exception.what());

            const std::string message = fmt::format("A fatal error occurred trying to get SteamBrowserContext -> {}", exception.what());
            //boxer::show(message.c_str(), "Fatal Error", //boxer::Style::Error);
            std::exit(1);
        }
        #else
        return {};
        #endif
    }

    void ConnectSocket(ConnectSocketProps socketProps)
    {
        const auto [commonName, fetchSocketUrl, onConnect, onMessage, bAutoReconnect] = socketProps;

        while (true)
        {
            const std::string socketUrl = fetchSocketUrl();

            try
            {
                websocketpp::client<websocketpp::config::asio_client> socketClient;

                socketClient.set_access_channels(websocketpp::log::alevel::none);
                socketClient.clear_error_channels(websocketpp::log::elevel::none);
                socketClient.set_error_channels(websocketpp::log::elevel::none);

                socketClient.init_asio();
                socketClient.set_open_handler(bind(onConnect, &socketClient, std::placeholders::_1));
                socketClient.set_message_handler(bind(onMessage, &socketClient, std::placeholders::_1, std::placeholders::_2));

                websocketpp::lib::error_code errorCode;
                websocketpp::client<websocketpp::config::asio_client>::connection_ptr con = socketClient.get_connection(socketUrl, errorCode);

                if (errorCode)
                {
                    throw websocketpp::exception(errorCode.message());
                }

                socketClient.connect(con);
                socketClient.run();
            }
            catch (websocketpp::exception& exception)
            {
                LOG_ERROR("webSocket exception thrown -> {}", exception.what());
            }

            Logger.Log("Disconnected from [{}] module...", commonName);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            if (!bAutoReconnect)
            {
                break;
            }
        }
    }
};