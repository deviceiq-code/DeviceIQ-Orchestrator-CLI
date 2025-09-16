#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <getopt.h>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>
#include <nlohmann/json.hpp>

#include "../include/String.h"
#include "../include/Version.h"
#include "../include/OrchestratorCLI.h"
#include "../include/CommandLineParser.h"

using namespace std;
using json = nlohmann::json;

OrchestratorCLI *Orchestrator;

string TargetInterface;
string TargetDevice;

bool Force = DEF_FORCE;
bool Apply = DEF_APPLY;

OrchestratorAction Action = ACTION_NOACTION;
DiscoveryMode Mode = DISCOVERY_NONE;

void ShowResult(const string &command, const bool result) {
    if (result) {
        fprintf(stdout, "Command '%s' successfully sent to '%s'\r\n\r\n", command.c_str(), TargetDevice.c_str());
        exit(0);
    } else {
        fprintf(stderr, "Failed to send '%s' command to '%s'\r\n\r\n", command.c_str(), TargetDevice.c_str());
        exit(1);
    }
}

int main(int argc, char** argv) {
    CommandLineParser clp;

    Orchestrator = new OrchestratorCLI();

    // Parameters

    clp.OnParameter('v', "version", no_argument, [](char* p_arg) {
        fprintf(stdout, "%s %s version %s\r\n\r\n", Version.ProductFamily.c_str(), Version.ProductName.c_str(), Version.Software.Info().c_str());
        exit(0);
    });

    clp.OnParameter('t', "target", required_argument, [](char* p_arg) {
        if (p_arg[0] == '=') p_arg = ++p_arg;
        TargetDevice = p_arg;

        if (String(TargetDevice).Equals("all", true)) { Mode = DISCOVERY_ALL; };
        if (String(TargetDevice).Equals("managed", true)) { Mode = DISCOVERY_MANAGED; };
        if (String(TargetDevice).Equals("unmanaged", true)) { Mode = DISCOVERY_UNMANAGED; };
    });

    clp.OnParameter('c', "config", required_argument, [&](char* p_arg) {
        if (p_arg[0] == '=') p_arg = ++p_arg;
        Orchestrator->ConfigFile(p_arg);
    });

    clp.OnParameter('f', "force", no_argument, [](char* p_arg) {
        Force = true;
    });

    clp.OnParameter('a', "apply", no_argument, [](char* p_arg) {
        Apply = true;
    });

    clp.OnParameter('i', "interface", required_argument, [&](char* p_arg) {
        if (p_arg[0] == '=') p_arg = ++p_arg;
        TargetInterface = p_arg;
    });

    // Actions
    clp.OnAction("add", []() { Action = ACTION_ADD; });
    clp.OnAction("checkonline", []() { Action = ACTION_CHECKONLINE; });
    clp.OnAction("discover", []() { Action = ACTION_DISCOVERY; });
    clp.OnAction("getlog", []() { Action = ACTION_GETLOG; });
    clp.OnAction("list", []() { Action = ACTION_LIST; });
    clp.OnAction("pull", []() { Action = ACTION_PULL; });
    clp.OnAction("push", []() { Action = ACTION_PUSH; });
    clp.OnAction("refresh", []() { Action = ACTION_REFRESH; });
    clp.OnAction("remove", []() { Action = ACTION_REMOVE; });
    clp.OnAction("restart", []() { Action = ACTION_RESTART; });
    clp.OnAction("update", []() { Action = ACTION_UPDATE; });

    clp.Parse(argc, argv);

    if (Orchestrator->ReadConfiguration()) {
        if (!TargetInterface.empty()) Orchestrator->Configuration["Configuration"]["Bind"] = TargetInterface;
        Orchestrator->Initialize();
    } else {
        fprintf(stdout, "Configuration file %s is invalid.\r\n\r\n", Orchestrator->ConfigFile().c_str());
        exit(1);
    }

    switch (Action) {
        case ACTION_ADD : {

        } break;
        case ACTION_CHECKONLINE : {
            fprintf(stdout, "Orchestrator server %s is: %s\r\n\r\n", TargetDevice.c_str(), (Orchestrator->CheckOnline(TargetDevice, 30030) ? "online" : "offline"));
            exit(0);
        } break;
        case ACTION_DISCOVERY : {
            ShowResult("discover", Orchestrator->Discovery(TargetDevice));
        } break;
        case ACTION_GETLOG : {
            ShowResult("getlog", Orchestrator->GetLog(TargetDevice));
        } break;
        case ACTION_LIST : {
            Orchestrator->List();
        } break;
        case ACTION_PULL : {
            ShowResult("pull", Orchestrator->Pull(TargetDevice));
        } break;
        case ACTION_PUSH : {
            ShowResult("push" , Orchestrator->Push(TargetDevice));
        } break;
        case ACTION_REFRESH : {
            ShowResult("refresh", Orchestrator->Refresh(TargetDevice));
        } break;
        case ACTION_REMOVE : {

        } break;
        case ACTION_RESTART : {
            ShowResult("restart", Orchestrator->Restart(TargetDevice));
        } break;
        case ACTION_UPDATE : {
            ShowResult("update", Orchestrator->Update(TargetDevice));
        } break;
        
        default: {
            fprintf(stderr, "Invalid request.\r\n\r\n");
            exit(1);
        }
    }
}