#include "../include/OrchestratorCLI.h"

bool OrchestratorClient::Reply() {
    if (mID <= 0) return false;
    if (mOutgoingJSON.empty()) return true;

    const std::string replymessage = mOutgoingJSON.dump(-1);
    const char* data = replymessage.data();
    size_t total_sent = 0;
    size_t remaining  = replymessage.size();

    constexpr int SEND_TIMEOUT_MS = 3000;

    while (remaining > 0) {
        ssize_t n = ::send(mID, data + total_sent, remaining, MSG_NOSIGNAL);
        if (n > 0) {
            total_sent += static_cast<size_t>(n);
            remaining  -= static_cast<size_t>(n);
            continue;
        }
        if (n == -1) {
            if (errno == EINTR) continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd{ mID, POLLOUT, 0 };
                int pr = ::poll(&pfd, 1, SEND_TIMEOUT_MS);
                if (pr > 0) continue;
                // timeout esperando liberar para enviar
                return false;
            }
            // erro definitivo (EPIPE, etc.)
            return false;
        }
    }

    // **Sinaliza EOF na direção de escrita** para o cliente detectar fim da resposta.
    if (::shutdown(mID, SHUT_WR) == -1) {
        // opcional: log errno (E.g., perror("shutdown"));
        return false;
    }

    // Se você NÃO precisa manter a conexão para mais nada, feche tudo:
    // ::close(mID);
    // mID = -1;

    return true;
}

void OrchestratorClient::IncomingBuffer(const string &value) {
    mIncomingBuffer = value;
    mIncomingJSON.clear();

    try {
        mIncomingJSON = json::parse(mIncomingBuffer);
    } catch (...) {
        
    }
}

void OrchestratorClient::OutgoingBuffer(const json &value) {
    OutgoingBuffer(value.dump());
}

void OrchestratorClient::OutgoingBuffer(const string &value) {
    mOutgoingBuffer = value;
    mOutgoingJSON.clear();

    try {
        mOutgoingJSON = json::parse(mOutgoingBuffer);
    } catch (...) {

    }
}

std::string OrchestratorCLI::queryIPAddress(const char* mac_address) {
    if (Configuration.contains("Managed Devices") || Configuration["Managed Devices"].is_object()) {
        for (const auto& [device_id, device_info] : Configuration["Managed Devices"].items()) {
            if (device_info.value("MAC Address", "") == mac_address) {
                return device_info.value("IP Address", "IP Not Found");
            }
        }
    }
    
    return "IP Not Found";
}

std::string OrchestratorCLI::queryMACAddress(const char* ip_address) {
    if (Configuration.contains("Managed Devices") || Configuration["Managed Devices"].is_object()) {
        for (const auto& [device_id, device_info] : Configuration["Managed Devices"].items()) {
            if (device_info.value("IP Address", "") == ip_address) {
                return device_info.value("MAC Address", "MAC Not Found");
            }
        }
    }
    
    return "MAC Not Found";
}

void OrchestratorCLI::applyBindForUdpSocket(int sockfd) {
    if (mBindAddr.s_addr == 0) return;
    (void)setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, Configuration["Configuration"]["Bind"].get<string>().c_str(), (socklen_t)Configuration["Configuration"]["Bind"].get<string>().size());

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr = mBindAddr;
    (void)bind(sockfd, (sockaddr*)&local, sizeof(local));
}

bool OrchestratorCLI::sendMessage(const std::string& message, const uint16_t port, const char* dest_address) {
    int sockfd, broadcast = 1;
    struct sockaddr_in broadcast_addr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return false;

    applyBindForUdpSocket(sockfd);

    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) { close(sockfd); return false; }

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = inet_addr(dest_address);
    broadcast_addr.sin_port = htons(port);

    if (sendto(sockfd, message.c_str(), message.length(), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) { close(sockfd); return false; }

    close(sockfd);
    return true;
}

void OrchestratorCLI::List() {
    fprintf(stdout, "Devices managed by this server:\r\n\r\n");
    
    if (Configuration["Managed Devices"].size() == 0) {
        fprintf(stdout, "No devices managed by this server.\r\n\r\n");
        return;
    } else {
        fprintf(stdout, "%s | %s | %s | %s | %s | %s\r\n",
            String("MAC Address").LimitString(17, true).c_str(),
            String("Hostname").LimitString(20, true).c_str(),
            String("IP Address").LimitString(15, true).c_str(),
            String("Hardware Model").LimitString(20, true) .c_str(),
            String("Version").LimitString(7, true).c_str(),
            String("Last Update").LimitString(27, true).c_str()
        );

        if (Configuration.contains("Managed Devices") && Configuration["Managed Devices"].is_object()) {
            uint16_t c = 0;

            for (const auto& [device_id, device_info] : Configuration["Managed Devices"].items()) {
                c++;

                fprintf(stdout, "%s | %s | %s | %s | %s | %s\r\n",
                    String(device_id).LimitString(17, true).c_str(),
                    String(device_info.value("Hostname", "Unknown")).LimitString(20, true).c_str(),
                    String(device_info.value("IP Address", "Unknown")).LimitString(15, true).c_str(),
                    String(device_info.value("Hardware Model", "Unknown")).LimitString(20, true).c_str(),
                    String(device_info.value("Version", "Unknown")).LimitString(7, true).c_str(),
                    String(device_info.value("Last Update", "")).LimitString(27, true).c_str()
                );
            }

            fprintf(stdout, "\r\n%d device%s managed.\r\n\r\n", c, (c > 1 ? "s" : ""));
        } else {
            fprintf(stdout, "Managed devices is missing or not an object.\r\n\r\n");
            exit(1);
        }
    }
}

bool OrchestratorCLI::Update(const String &target) {
        const uint16_t port = Configuration["Configuration"]["Port"].get<uint16_t>();

    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command", "Update"},
        {"Parameter", ""}
    };

    auto send_to_group = [&](const char* section, size_t& ok, size_t& fail, size_t& skipped) -> bool {
        if (!Configuration.contains(section) || Configuration[section].empty()) {
            fprintf(stdout, "[Update] No devices under section: %s\r\n", section);
            return false;
        }

        for (auto &kv : Configuration[section].items()) {
            const std::string mac = kv.key();
            const nlohmann::json &dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                fprintf(stdout, "[Update] %s has no IP Address — ignored\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            fprintf(stdout, "[Update] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = SendToDevice(ip, JsonCommand);
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
            fprintf(stdout, "[Update] No devices found for requested group(s).\r\n");
            return false;
        }

        fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    json device = getDevice(target);
    if (device.empty()) {
        fprintf(stdout, "[Update] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const string ip = it.value().at("IP Address").get<string>();
    fprintf(stdout, "[Update] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return SendToDevice(ip, JsonCommand);
}

bool OrchestratorCLI::Initialize() {
    bool ret = false;

    if (Configuration.empty()) {
        if (ReadConfiguration()) {
            
        }
    }

    // Bind
    if (setBindInterface(JSON<std::string>(Configuration["Configuration"]["Bind"], ""))) {
        ret = true;
    } else {
        ret = false;
    }

    return ret;
}

bool OrchestratorCLI::ReadConfiguration() {
    bool ret = false;

    if (mConfigFile.empty()) {
        char exePath[PATH_MAX];

        ssize_t count = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (count != -1) {
            exePath[count] = '\0';
            mConfigFile = "./" + Version.ProductName + ".json";
        } else {
            mConfigFile = DEF_CONFIGFILE;
        }
    }

    std::ifstream configFile(mConfigFile);

    if (configFile.is_open() == true) {
        try {
            configFile >> Configuration;
            ret = true;
        } catch (nlohmann::json::parse_error& ex) {
            ret = false;
        }
    }

    return ret;
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

json OrchestratorCLI::Query(const std::string& orchestrator_url, uint16_t orchestrator_port, const json& payload) {
    json reply;
    
    if (payload.empty()) return false;
    string dumped = payload.dump(-1);

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%u", orchestrator_port);

    struct addrinfo* res = nullptr;
    int gai = getaddrinfo(orchestrator_url.c_str(), portstr, &hints, &res);
    if (gai != 0 || !res) return false;

    const int CONNECT_TIMEOUT_MS = 700; // connect
    const int READ_TIMEOUT_SEC = 1; // recv

    for (auto* rp = res; rp != nullptr; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc < 0 && errno != EINPROGRESS && errno != EALREADY) { close(fd); continue; }

        struct pollfd pfd{ fd, POLLOUT, 0 };
        int pr = poll(&pfd, 1, CONNECT_TIMEOUT_MS);
        if (pr <= 0) { close(fd); continue; }

        int soerr = 0; socklen_t len = sizeof(soerr);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0 || soerr != 0) { close(fd); continue; }

        fcntl(fd, F_SETFL, flags);

        timeval tv{ READ_TIMEOUT_SEC, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        const char* p = dumped.c_str();
        size_t to_send = dumped.size();

        while (to_send > 0) {
            ssize_t n = send(fd, p, to_send, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                close(fd);
                fd = -1;
                break;
            }
            p += n;
            to_send -= static_cast<size_t>(n);
        }
        if (fd < 0) continue;

        shutdown(fd, SHUT_WR);

        std::string incoming;
        char buf[4096];
        while (true) {
            ssize_t r = recv(fd, buf, sizeof(buf), 0);
            if (r > 0) { incoming.append(buf, static_cast<size_t>(r)); }
            else if (r == 0) { break; }
            else {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                incoming.clear();
                break;
            }
        }
        close(fd);

        try {
            if (!incoming.empty()) reply = json::parse(incoming);
        } catch (...) {
            
        }
        break;
    }

    freeaddrinfo(res);
    return reply;
}

// Helper: converte in_addr -> string
static inline std::string to_ip(const in_addr &addr) {
    char buf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return std::string(buf);
}

bool OrchestratorCLI::SendToDevice(const std::string& destination, const json& payload) {
    if (payload.empty()) return false;
    string dumped = payload.dump(-1);

    int socket_fd, broadcast = 1;
    struct sockaddr_in broadcast_addr;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) return false;

    if (mBindAddr.s_addr == 0) return false;
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, Configuration["Configuration"]["Bind"].get<string>().c_str(), (socklen_t)Configuration["Configuration"]["Bind"].get<string>().size());

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr = mBindAddr;
    (void)bind(socket_fd, (sockaddr*)&local, sizeof(local));

    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        close(socket_fd);
        return false;
    }

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = inet_addr(destination.c_str());
    broadcast_addr.sin_port = htons(Configuration["Configuration"]["Port"].get<uint16_t>());

    if (sendto(socket_fd, dumped.c_str(), dumped.length(), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
        close(socket_fd);
        return false;
    }

    close(socket_fd);
    return true;
}

bool OrchestratorCLI::Discovery(const String &target) {
    json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<String>()},
        {"Command", "Discover"},
        {"Parameter", "All"},
    };

    return SendToDevice(target, JsonCommand);
}

const json OrchestratorCLI::getDevice(const String &target) {
    enum class Mode { ByIP, ByMAC, ByHostname };
    Mode mode = (target.find('.') != string::npos) ? Mode::ByIP : (target.find(':') != string::npos) ? Mode::ByMAC : Mode::ByHostname;

    const vector<string> sections = { "Managed Devices", "Unmanaged Devices" };

    for (const auto &section : sections) {
        if (!Configuration.contains(section) || !Configuration[section].is_object()) continue;

        for (auto it = Configuration[section].begin(); it != Configuration[section].end(); ++it) {
            const string mac_key = it.key();
            const json& obj = it.value();

            bool match = false;
            switch (mode) {
                case Mode::ByIP:
                    if (obj.contains("IP Address") && obj["IP Address"].is_string())
                        match = (obj["IP Address"].get<string>() == target);
                    break;

                case Mode::ByMAC: {
                    if (mac_key == target) {
                        match = true;
                    } else if (obj.contains("MAC Address") && obj["MAC Address"].is_string()) {
                        match = (obj["MAC Address"].get<string>() == target);
                    }
                    break;
                }

                case Mode::ByHostname:
                    if (obj.contains("Hostname") && obj["Hostname"].is_string())
                        match = (obj["Hostname"].get<string>() == target);
                    break;
            }

            if (match) {
                json device = obj;
                device["Where"] = section;
                if (!device.contains("MAC Address") || !device["MAC Address"].is_string()) device["MAC Address"] = mac_key;
                return json{{mac_key, device}};
            }
        }
    }

    return json::object();
}

bool OrchestratorCLI::Restart(const String &target) {
    const uint16_t port = Configuration["Configuration"]["Port"].get<uint16_t>();

    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command", "Restart"},
        {"Parameter", ""}
    };

    auto send_to_group = [&](const char* section, size_t& ok, size_t& fail, size_t& skipped) -> bool {
        if (!Configuration.contains(section) || Configuration[section].empty()) {
            fprintf(stdout, "[Restart] No devices under section: %s\r\n", section);
            return false;
        }

        for (auto &kv : Configuration[section].items()) {
            const std::string mac = kv.key();
            const nlohmann::json &dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                fprintf(stdout, "[Restart] %s has no IP Address — ignored\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            fprintf(stdout, "[Restart] %s — %s\r\n\r\n", mac.c_str(), ip.c_str());

            const bool sent = SendToDevice(ip, JsonCommand);
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
            fprintf(stdout, "[Restart] No devices found for requested group(s).");
            return false;
        }

        fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    json device = getDevice(target);
    if (device.empty()) {
        fprintf(stdout, "[Restart] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const string ip = it.value().at("IP Address").get<string>();

    return SendToDevice(ip, JsonCommand);
}

bool OrchestratorCLI::Refresh(const String &target) {
    const uint16_t port = Configuration["Configuration"]["Port"].get<uint16_t>();

    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<std::string>()},
        {"Command", "Refresh"},
        {"Parameter", ""}
    };

    if (target.Equals("all", true) || target.Equals("managed", true)) {
        if (!Configuration.contains("Managed Devices") || Configuration["Managed Devices"].empty()) {
            fprintf(stdout, "[Refresh] No devices managed\r\n");
            return false;
        }

        size_t ok = 0, fail = 0, skipped = 0;

        for (auto &kv : Configuration["Managed Devices"].items()) {
            const std::string mac = kv.key();
            const nlohmann::json &dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                fprintf(stdout, "[Refresh] %s no IP Address found\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            fprintf(stdout, "[Refresh] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = SendToDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }

        fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        fprintf(stdout, "[Refresh] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<std::string>();
    fprintf(stdout, "[Refresh] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return SendToDevice(ip, JsonCommand);
}

bool OrchestratorCLI::Pull(const String &target) {
    const uint16_t port = Configuration["Configuration"]["Port"].get<uint16_t>();

    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<String>()},
        {"Command", "Pull"},
        {"Parameter", ""}
    };

    if (target.Equals("all", true) || target.Equals("managed", true)) {
        if (!Configuration.contains("Managed Devices") || Configuration["Managed Devices"].empty()) {
            fprintf(stdout, "[Pull] No devices managed\r\n");
            return false;
        }

        size_t ok = 0, fail = 0, skipped = 0;

        for (auto &kv : Configuration["Managed Devices"].items()) {
            const std::string mac = kv.key();
            const nlohmann::json &dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                fprintf(stdout, "[Pull] %s no IP Address found", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            fprintf(stdout, "[Pull] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = SendToDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }

        fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        fprintf(stdout, "[Pull] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<String>();
    fprintf(stdout, "[Pull] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return SendToDevice(ip, JsonCommand);
}

bool OrchestratorCLI::GetLog(const String &target) {
    // TODO
    return true;
}

bool OrchestratorCLI::Push(const String &target) {
    const uint16_t port = Configuration["Configuration"]["Port"].get<uint16_t>();

    nlohmann::json JsonCommand = {
        {"Provider", "Orchestrator"},
        {"Server ID", Configuration["Configuration"]["Server ID"].get<String>()},
        {"Command", "Push"},
        {"Parameter", ""}
    };

    if (target.Equals("all", true) || target.Equals("managed", true)) {
        if (!Configuration.contains("Managed Devices") || Configuration["Managed Devices"].empty()) {
            fprintf(stdout, "[Push] No devices managed\r\n");
            return false;
        }

        size_t ok = 0, fail = 0, skipped = 0;

        for (auto &kv : Configuration["Managed Devices"].items()) {
            const std::string mac = kv.key();
            const nlohmann::json &dev = kv.value();

            if (!dev.contains("IP Address") || dev["IP Address"].is_null()) {
                fprintf(stdout, "[Push] %s no IP Address found\r\n", mac.c_str());
                ++skipped;
                continue;
            }

            const std::string ip = dev["IP Address"].get<std::string>();
            fprintf(stdout, "[Push] %s — %s\r\n", mac.c_str(), ip.c_str());

            const bool sent = SendToDevice(ip, JsonCommand);
            if (sent) ++ok; else ++fail;
        }

        fprintf(stdout, "[Update] Multicast finished: Sent = %s, Failed = %s, Ignored = %s\r\n", std::to_string(ok).c_str(), std::to_string(fail).c_str(), std::to_string(skipped).c_str());

        return ok > 0;
    }

    nlohmann::json device = getDevice(target);
    if (device.empty()) {
        fprintf(stdout, "[Push] Target device not found: %s\r\n", target.c_str());
        return false;
    }

    auto it = device.begin();
    const std::string ip = it.value().at("IP Address").get<String>();
    fprintf(stdout, "[Push] %s — %s\r\n", it.key().c_str(), ip.c_str());

    return SendToDevice(ip, JsonCommand);
}