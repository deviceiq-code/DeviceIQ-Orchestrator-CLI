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
    ACTION_RESTORE,
    ACTION_LIST,
    ACTION_PULL,
    ACTION_PUSH,
    ACTION_GETLOG,
    ACTION_CLEARLOG
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
        bool resolveInterfaceOrIp(const std::string& ifaceOrIp, in_addr& out);
        bool sendMessage(const std::string &message, uint16_t port, const char* dest_address);
        bool setBindInterface(const std::string& ifaceOrIp);
        bool readConfiguration();

        bool sendCommand(const String &target, const String &command);
        bool pokeDevice(const std::string &device, const nlohmann::json &payload);

    public:
        explicit OrchestratorCLI(const string &configfile);
        
        bool Discovery(const String &target);
        bool GetLog(const String &target) { return sendCommand(target, "GetLog"); }
        bool ClearLog(const String &target) { return sendCommand(target, "ClearLog"); }
        void List(const String &target = String());
        bool Pull(const String &target) { return sendCommand(target, "Pull"); }
        bool Push(const String &target) { return sendCommand(target, "Push"); }
        bool Refresh(const String &target) { return sendCommand(target, "Refresh"); }
        bool Restart(const String &target) { return sendCommand(target, "Restart"); }
        bool Restore(const String &target) { return sendCommand(target, "Restore"); }
        bool Update(const String &target) { return sendCommand(target, "Update"); }
        bool Add(const String &target) { return sendCommand(target, "Add"); }
        bool Remove(const String &target) { return sendCommand(target, "Remove"); }

        nlohmann::json Configuration;
};

#endif // ORCHESTRATORCLI_H
