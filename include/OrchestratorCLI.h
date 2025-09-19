#ifndef ORCHESTRATORCLI_H
#define ORCHESTRATORCLI_H

#include <arpa/inet.h>
#include <fstream>
#include <ifaddrs.h>
#include <nlohmann/json.hpp>

#include "String.h"
#include "Version.h"

constexpr uint16_t DEF_PORT = 30030;
constexpr char DEF_CONFIGFILE[] = "./orchestrator.json";

enum OrchestratorAction {
    ACTION_NOACTION,
    ACTION_DISCOVER,
    ACTION_ADD,
    ACTION_RESTART,
    ACTION_REMOVE,
    ACTION_UPDATE,
    ACTION_REFRESH,
    ACTION_LIST,
    ACTION_PULL,
    ACTION_PUSH,
    ACTION_GETLOG
};

template <typename T>
T JSON(const nlohmann::json& jsonValue, const T& defaultValue = T()) {
    try {
        if (jsonValue.is_null()) return defaultValue;
        return jsonValue.get<T>();
    } catch (const nlohmann::json::type_error&) {
        return defaultValue;
    } catch (const nlohmann::json::out_of_range&) {
        return defaultValue;
    }
}

class OrchestratorCLI {
    private:
        in_addr mBindAddr{};
        std::string mConfigFile = DEF_CONFIGFILE;

        const nlohmann::json getDevice(const String& target);
        std::string queryIPAddress(const char* mac_address);
        std::string queryMACAddress(const char* ip_address);
        bool resolveInterfaceOrIp(const std::string& ifaceOrIp, in_addr& out);
        bool sendMessage(const std::string& message, uint16_t port, const char* dest_address);
        bool setBindInterface(const std::string& ifaceOrIp);
        bool readConfiguration();

    public:
        explicit OrchestratorCLI(const string &configfile);

        inline std::string ConfigFile() const { return mConfigFile; }
        inline void ConfigFile(const std::string& value) { mConfigFile = value; }

        bool Discovery(const String& target);
        bool GetLog(const String& target);
        void List(const String& target = String());
        bool Pull(const String& target);
        bool Push(const String& target);
        bool Refresh(const String& target);
        bool Restart(const String& target);
        bool SendToDevice(const std::string& destination, const nlohmann::json& payload);
        bool Update(const String& target);

        nlohmann::json Configuration;
};

#endif // ORCHESTRATORCLI_H
