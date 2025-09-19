#include "../include/OrchestratorCLI.h"

#include <cstring>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

OrchestratorCLI::OrchestratorCLI(const string &configfile) : mConfigFile(configfile) {
    if (!readConfiguration()) {
        throw std::runtime_error("Failed to read configuration file " + configfile);
    }

    if (!setBindInterface(JSON<std::string>(Configuration["Configuration"]["Bind"], ""))) {
        throw std::runtime_error("Failed to set bind interface");
    }
}

const nlohmann::json OrchestratorCLI::getDevice(const String& target) {
    const std::string t = target.c_str();

    enum class Mode { ByIP, ByMAC, ByHostname };
    Mode mode = (t.find('.') != std::string::npos) ? Mode::ByIP : (t.find(':') != std::string::npos) ? Mode::ByMAC : Mode::ByHostname;

    const std::vector<std::string> sections = {"Managed Devices", "Unmanaged Devices"};

    for (const auto& section : sections) {
        if (!Configuration.contains(section) || !Configuration[section].is_object()) continue;

        for (auto it = Configuration[section].begin(); it != Configuration[section].end(); ++it) {
            const std::string mac_key = it.key();
            const nlohmann::json& obj = it.value();

            bool match = false;
            switch (mode) {
                case Mode::ByIP: {
                    if (obj.contains("IP Address") && obj["IP Address"].is_string()) match = (obj["IP Address"].get<std::string>() == t);
                } break;
                case Mode::ByMAC: {
                    if (mac_key == t) {
                        match = true;
                    } else if (obj.contains("MAC Address") && obj["MAC Address"].is_string()) {
                        match = (obj["MAC Address"].get<std::string>() == t);
                    }
                } break;
                case Mode::ByHostname: {
                    if (obj.contains("Hostname") && obj["Hostname"].is_string()) match = (obj["Hostname"].get<std::string>() == t);
                } break;
            }

            if (match) {
                nlohmann::json device = obj;
                device["Where"] = section;
                if (!device.contains("MAC Address") || !device["MAC Address"].is_string()) device["MAC Address"] = mac_key;
                return nlohmann::json{{mac_key, device}};
            }
        }
    }

    return nlohmann::json::object();
}

bool OrchestratorCLI::resolveInterfaceOrIp(const std::string& ifaceOrIp, in_addr& out) {
    in_addr tmp{};
    if (inet_aton(ifaceOrIp.c_str(), &tmp)) { out = tmp; return true; }

    ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return false;

    bool ok = false;
    for (ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifaceOrIp != ifa->ifa_name) continue;
        out = ((sockaddr_in*)ifa->ifa_addr)->sin_addr;
        ok = true;
        break;
    }
    freeifaddrs(ifaddr);
    return ok;
}

bool OrchestratorCLI::sendMessage(const std::string& message, uint16_t port, const char* dest_address) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return false;

    if (mBindAddr.s_addr == 0) { close(sockfd); return false; }

    (void)setsockopt(
        sockfd,
        SOL_SOCKET,
        SO_BINDTODEVICE,
        Configuration["Configuration"]["Bind"].get<std::string>().c_str(),
        (socklen_t)Configuration["Configuration"]["Bind"].get<std::string>().size()
    );

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr = mBindAddr;
    (void)bind(sockfd, (sockaddr*)&local, sizeof(local));

    int broadcast = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        close(sockfd);
        return false;
    }

    sockaddr_in to{};
    std::memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = inet_addr(dest_address);

    const ssize_t sent = sendto(sockfd, message.c_str(), message.length(), 0, (struct sockaddr*)&to, sizeof(to));
    close(sockfd);
    return (sent >= 0);
}

bool OrchestratorCLI::setBindInterface(const std::string& ifaceOrIp) {
    if (ifaceOrIp.empty()) {
        mBindAddr = {};
        return true;
    }

    in_addr addr{};
    if (!resolveInterfaceOrIp(ifaceOrIp, addr)) return false;

    mBindAddr = addr;
    return true;
}

bool OrchestratorCLI::Discovery(const String& target) {
    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command",  "Discover"},
        {"Parameter","All"},
    };

    return PokeDevice(target, JsonCommand);
}

bool OrchestratorCLI::GetLog(const String& target) {
    // TODO: implementar quando o protocolo de log estiver definido
    return true;
}

void OrchestratorCLI::List(const String& target) {
    const bool is_specific = !(target.Equals("all", true) || target.Equals("managed", true) || target.Equals("unmanaged", true));
    const bool include_managed = is_specific || target.Equals("all", true) || target.Equals("managed", true);
    const bool include_unmanaged = is_specific || target.Equals("all", true) || target.Equals("unmanaged", true);

    auto limit_c = [&](const char* s, size_t n) -> String { return String(s).LimitString(n, true); };
    
    auto print_header = [&] {
        std::fprintf(stdout, "%s | %s | %s | %s | %s | %s | %s\r\n",
            limit_c("MAC Address", 17).c_str(),
            limit_c("Hostname", 20).c_str(),
            limit_c("IP Address", 15).c_str(),
            limit_c("Hardware Model", 20).c_str(),
            limit_c("Version", 7).c_str(),
            limit_c("Last Update", 27).c_str(),
            limit_c("Where", 17).c_str()
        );
    };

    auto print_row = [&](const std::string& id, const nlohmann::json& info, const char* where) {
        const std::string hn = info.value("Hostname", std::string("Unknown"));
        const std::string ip = info.value("IP Address", std::string("Unknown"));
        const std::string hw = info.value("Hardware Model", std::string("Unknown"));
        const std::string ver = info.value("Version", std::string("Unknown"));
        const std::string lu = info.value("Last Update", std::string());

        const String sid = String(id.c_str()).LimitString(17, true);
        const String shn = String(hn.c_str()).LimitString(20, true);
        const String sip = String(ip.c_str()).LimitString(15, true);
        const String shw = String(hw.c_str()).LimitString(20, true);
        const String svr = String(ver.c_str()).LimitString(7, true);
        const String slu = String(lu.c_str()).LimitString(27, true);

        std::fprintf(stdout, "%s | %s | %s | %s | %s | %s | %s\r\n", sid.c_str(), shn.c_str(), sip.c_str(), shw.c_str(), svr.c_str(), slu.c_str(), where);
    };

    auto matches_target = [&](const std::string& id, const nlohmann::json& info) -> bool {
        if (!is_specific) return true;

        const std::string hn = info.value("Hostname", std::string("Unknown"));
        const std::string ip = info.value("IP Address", std::string("Unknown"));

        return target.Equals(id.c_str(), true) || target.Equals(hn.c_str(), true) || target.Equals(ip.c_str(), true);
    };

    auto process_section = [&](const char* section_key, const char* where_label, bool enabled, uint16_t& counter, bool is_specific_query) {
        if (!enabled) return;

        auto it = Configuration.find(section_key);
        if (it == Configuration.end() || !it->is_object() || it->empty()) {
            if (!is_specific_query) {
                if (std::strcmp(section_key, "Managed Devices") == 0) {
                    std::fprintf(stdout, "No devices managed by this server.\r\n\r\n");
                } else {
                    std::fprintf(stdout, "No unmanaged devices found by this server.\r\n\r\n");
                }
            }
            return;
        }

        for (const auto& [device_id, device_info] : it->items()) {
            if (matches_target(device_id, device_info)) {
                ++counter;
                print_row(device_id, device_info, where_label);
            }
        }
    };

    print_header();

    uint16_t m = 0, u = 0;
    process_section("Managed Devices", "Managed Devices", include_managed, m, !is_specific);
    process_section("Unmanaged Devices", "Unmanaged Devices", include_unmanaged, u, !is_specific);

    std::fprintf(stdout, "\r\n%d device%s.\r\n\r\n", m + u, ((m + u) > 1 ? "s" : ""));
}

bool OrchestratorCLI::Pull(const String& target) {
    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command",  "Pull"},
        {"Parameter",""}
    };

    if (target.Equals("all", true) || target.Equals("managed", true)) {
        if (!Configuration.contains("Managed Devices") || Configuration["Managed Devices"].empty()) {
            std::fprintf(stdout, "[Pull] No devices managed\r\n");
            return false;
        }

        size_t ok = 0, fail = 0, skipped = 0;

        for (auto& kv : Configuration["Managed Devices"].items()) {
            const std::string mac = kv.key();
            const nlohmann::json& dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                std::fprintf(stdout, "[Pull] %s no IP Address found\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            std::fprintf(stdout, "[Pull] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = PokeDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }

        std::fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        std::fprintf(stdout, "[Pull] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<std::string>();
    std::fprintf(stdout, "[Pull] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return PokeDevice(ip, JsonCommand);
}

bool OrchestratorCLI::Push(const String& target) {
    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command",  "Push"},
        {"Parameter",""}
    };

    if (target.Equals("all", true) || target.Equals("managed", true)) {
        if (!Configuration.contains("Managed Devices") || Configuration["Managed Devices"].empty()) {
            std::fprintf(stdout, "[Push] No devices managed\r\n");
            return false;
        }

        size_t ok = 0, fail = 0, skipped = 0;

        for (auto& kv : Configuration["Managed Devices"].items()) {
            const std::string mac = kv.key();
            const nlohmann::json& dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                std::fprintf(stdout, "[Push] %s no IP Address found\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            std::fprintf(stdout, "[Push] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = PokeDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }

        std::fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        std::fprintf(stdout, "[Push] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<std::string>();
    std::fprintf(stdout, "[Push] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return PokeDevice(ip, JsonCommand);
}

bool OrchestratorCLI::readConfiguration() {
    if (mConfigFile.empty()) {
        char exePath[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (count != -1) {
            exePath[count] = '\0';
            mConfigFile = "./" + mConfigFile + ".json";
        } else {
            mConfigFile = DEF_CONFIGFILE;
        }
    }

    std::ifstream configFile(mConfigFile);
    if (!configFile.is_open()) return false;

    try {
        configFile >> Configuration;
        return true;
    } catch (nlohmann::json::parse_error&) {
        return false;
    }
}

bool OrchestratorCLI::Refresh(const String& target) {
    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command",  "Refresh"},
        {"Parameter",""}
    };

    if (target.Equals("all", true) || target.Equals("managed", true)) {
        if (!Configuration.contains("Managed Devices") || Configuration["Managed Devices"].empty()) {
            std::fprintf(stdout, "[Refresh] No devices managed\r\n");
            return false;
        }

        size_t ok = 0, fail = 0, skipped = 0;

        for (auto& kv : Configuration["Managed Devices"].items()) {
            const std::string mac = kv.key();
            const nlohmann::json& dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                std::fprintf(stdout, "[Refresh] %s no IP Address found\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            std::fprintf(stdout, "[Refresh] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = PokeDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }

        std::fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        std::fprintf(stdout, "[Refresh] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<std::string>();
    std::fprintf(stdout, "[Refresh] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return PokeDevice(ip, JsonCommand);
}

bool OrchestratorCLI::Restart(const String& target) {
    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command",  "Restart"},
        {"Parameter",""}
    };

    auto send_to_group = [&](const char* section, size_t& ok, size_t& fail, size_t& skipped) -> bool {
        if (!Configuration.contains(section) || Configuration[section].empty()) {
            return false;
        }

        for (auto& kv : Configuration[section].items()) {
            const std::string mac = kv.key();
            const nlohmann::json& dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            const bool sent = PokeDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }
        return true;
    };

    if (target.Equals("all", true) || target.Equals("managed", true) || target.Equals("unmanaged", true)) {
        size_t ok = 0, fail = 0, skipped = 0;
        bool any_section = false;

        if (target.Equals("all", true) || target.Equals("managed", true)) {
            any_section |= send_to_group("Managed Devices", ok, fail, skipped);
        }
        if (target.Equals("all", true) || target.Equals("unmanaged", true)) {
            any_section |= send_to_group("Unmanaged Devices", ok, fail, skipped);
        }

        if (!any_section) {
            return false;
        }

        std::fprintf(stdout, "Restart — Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());
        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        std::fprintf(stdout, "Restart — Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<std::string>();
    return PokeDevice(ip, JsonCommand);
}

bool OrchestratorCLI::PokeDevice(const std::string& destination, const nlohmann::json& payload) {
    if (payload.empty()) return false;
    const std::string dumped = payload.dump(-1);

    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) return false;

    if (mBindAddr.s_addr == 0) { close(socket_fd); return false; }

    (void)setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_BINDTODEVICE,
        Configuration["Configuration"]["Bind"].get<std::string>().c_str(),
        (socklen_t)Configuration["Configuration"]["Bind"].get<std::string>().size()
    );

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr = mBindAddr;
    (void)bind(socket_fd, (sockaddr*)&local, sizeof(local));

    int broadcast = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        close(socket_fd);
        return false;
    }

    sockaddr_in to{};
    std::memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(Configuration["Configuration"]["Port"].get<uint16_t>());
    to.sin_addr.s_addr = inet_addr(destination.c_str());

    const ssize_t sent = sendto(socket_fd, dumped.c_str(), dumped.length(), 0, (struct sockaddr*)&to, sizeof(to));
    close(socket_fd);
    return (sent >= 0);
}

bool OrchestratorCLI::Update(const String& target) {
    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command",  "Update"},
        {"Parameter",""}
    };

    auto send_to_group = [&](const char* section, size_t& ok, size_t& fail, size_t& skipped) -> bool {
        if (!Configuration.contains(section) || Configuration[section].empty()) {
            std::fprintf(stdout, "[Update] No devices under section: %s\r\n", section);
            return false;
        }

        for (auto& kv : Configuration[section].items()) {
            const std::string mac = kv.key();
            const nlohmann::json& dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                std::fprintf(stdout, "[Update] %s has no IP Address — ignored\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            std::fprintf(stdout, "[Update] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = PokeDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }
        return true;
    };

    if (target.Equals("all", true) || target.Equals("managed", true) || target.Equals("unmanaged", true)) {
        size_t ok = 0, fail = 0, skipped = 0;
        bool any_section = false;

        if (target.Equals("all", true) || target.Equals("managed", true)) {
            any_section |= send_to_group("Managed Devices", ok, fail, skipped);
        }
        if (target.Equals("all", true) || target.Equals("unmanaged", true)) {
            any_section |= send_to_group("Unmanaged Devices", ok, fail, skipped);
        }

        if (!any_section) {
            std::fprintf(stdout, "[Update] No devices found for requested group(s).\r\n");
            return false;
        }

        std::fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        std::fprintf(stdout, "[Update] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<std::string>();
    std::fprintf(stdout, "[Update] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return PokeDevice(ip, JsonCommand);
}