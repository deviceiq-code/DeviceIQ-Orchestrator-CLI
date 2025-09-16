#ifndef OrchestratorCLI_h
#define OrchestratorCLI_h

#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <thread>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <random>
#include <chrono>
#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <nlohmann/json.hpp>

#include "String.h"
#include "Tools.h"

using namespace std;
using namespace Tools;

using json = nlohmann::json;

// constexpr size_t DEF_BUFFERSIZE = 1024;
// constexpr uint32_t DEF_LISTENTIMEOUT = 5;
constexpr uint16_t DEF_PORT = 30030;
constexpr char DEF_BROADCASTADDRESS[] = "255.255.255.255";
constexpr char DEF_CONFIGFILE[] = "./orchestrator.json";
constexpr char DEF_LOGFILE[] = "./orchestrator.log";
constexpr char DEF_SERVERNAME[] = "Orchestrator";
constexpr bool DEF_FORCE = false;
constexpr bool DEF_APPLY = false;

enum OrchestratorAction { ACTION_NOACTION, ACTION_DISCOVERY, ACTION_ADD, ACTION_RESTART, ACTION_RELOADCONFIG, ACTION_REMOVE, ACTION_UPDATE, ACTION_REFRESH, ACTION_LIST, ACTION_PULL, ACTION_PUSH, ACTION_MANAGE, ACTION_CHECKONLINE, ACTION_GETLOG };
enum DiscoveryMode { DISCOVERY_NONE, DISCOVERY_ALL, DISCOVERY_UNMANAGED, DISCOVERY_MANAGED };

enum OperationResult {
    NOTMANAGED,

    ADD_FAIL,
    ADD_SUCCESS,
    ADD_ALREADYMANAGED,

    REMOVE_FAIL,
    REMOVE_SUCCESS,

    REFRESH_FAIL,
    REFRESH_SUCCESS,
    REFRESH_PARTIAL,

    PULLING_FAIL,
    PULLING_SUCCESS,

    PUSHING_FAIL,
    PUSHING_SUCCESS
};

template <typename T>
T JSON(const nlohmann::json& jsonValue, const T& defaultValue = T()) {
    try {
        if (jsonValue.is_null()) { return defaultValue; }
        return jsonValue.get<T>();
    } catch (const nlohmann::json::type_error&) {
        return defaultValue;
    } catch (const nlohmann::json::out_of_range&) {
        return defaultValue;
    }
}

struct Client {
    uint16_t ID;
    sockaddr_in Info;
    std::string IncomingBuffer;
    std::string OutgoingBuffer;
    void Send(char* reply) { OutgoingBuffer = reply; send(ID, OutgoingBuffer.c_str(), OutgoingBuffer.length(), 0); }
};

class OrchestratorClient {
    private:
        int mID;
        sockaddr_in mInfo;
        string mIncomingBuffer;
        string mOutgoingBuffer;
        json mIncomingJSON;
        json mOutgoingJSON;
        
    public:
        OrchestratorClient(uint32_t id, const sockaddr_in &info) : mID(id), mInfo(info) {}

        bool Reply();

        void IncomingBuffer(const string &value);
        const string IncomingBuffer() { return mIncomingBuffer; }
        void OutgoingBuffer(const string &value);
        void OutgoingBuffer(const json &value);
        const string OutgoingBuffer() { return mOutgoingBuffer; }
        const string IPAddress() { return inet_ntoa(mInfo.sin_addr); }

        const json &IncomingJSON() const noexcept { return mIncomingJSON; }
        const json &OutgoingJSON() const noexcept { return mOutgoingJSON; }
};

struct Device {
    uint16_t ID;
};

class OrchestratorCLI {
    private:
        in_addr mBindAddr{};
        bool resolveInterfaceOrIp(const string& ifaceOrIp, in_addr& out);
        bool setBindInterface(const std::string& ifaceOrIp);

        string mConfigFile = DEF_CONFIGFILE;

        string queryIPAddress(const char* mac_address);
        string queryMACAddress(const char* ip_address);

        inline bool isManaged(const String &target) { return Configuration["Managed Devices"].contains(target); }
        const json getDevice(const String &target);

        void applyBindForUdpSocket(int sockfd);
        bool sendMessage(const std::string& message, const uint16_t port, const char* dest_address = DEF_BROADCASTADDRESS);
    public:
        explicit OrchestratorCLI() {}

        inline std::string ConfigFile() const { return mConfigFile; }
        inline void ConfigFile(const std::string& value) { mConfigFile = value; }

        void List();
        bool Discovery(const String &target);
        bool Restart(const String &target);
        bool Refresh(const String &target);
        bool Pull(const String &target);
        bool Push(const String &target);
        bool Update(const String &target);
        bool GetLog(const String &target);
        
        json Query(const std::string& orchestrator_url, uint16_t orchestrator_port, const json& payload);
        bool SendToDevice(const std::string& destination, const json& payload);
        
        bool Initialize();
        bool ReadConfiguration();

        json Configuration;
};

#endif