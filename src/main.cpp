#include "String.h"
#include "Version.h"
#include "OrchestratorCLI.h"
#include "CommandLineParser.h"

OrchestratorCLI *Orchestrator;

string TargetDevice = "all";
String ConfigFile = "./orchestrator.json";

bool Force = false;
bool Apply = false;

OrchestratorAction Action = ACTION_NOACTION;

void ShowResult(const string &command, const bool result) {
    if (result) {
        fprintf(stdout, "\r\nCommand '%s' successfully sent to '%s'.\r\n\r\n", command.c_str(), TargetDevice.c_str());
        exit(0);
    } else {
        fprintf(stderr, "\r\nFailed to send '%s' command to '%s'.\r\n\r\n", command.c_str(), TargetDevice.c_str());
        exit(1);
    }
}

int main(int argc, char** argv) {
    CommandLineParser clp;

    // Parameters

    clp.OnParameter('v', "version", no_argument, [](char* p_arg) {
        fprintf(stdout, "%s %s version %s\r\n\r\n", Version.ProductFamily.c_str(), Version.ProductName.c_str(), Version.Software.Info().c_str());
        exit(0);
    });

    clp.OnParameter('t', "target", required_argument, [](char* p_arg) {
        if (p_arg[0] == '=') p_arg = ++p_arg;
        TargetDevice = p_arg;
    });

    clp.OnParameter('c', "config", required_argument, [&](char* p_arg) {
        if (p_arg[0] == '=') p_arg = ++p_arg;
        ConfigFile = String(p_arg);
    });

    clp.OnParameter('f', "force", no_argument, [](char* p_arg) {
        Force = true;
    });

    clp.OnParameter('a', "apply", no_argument, [](char* p_arg) {
        Apply = true;
    });

    // Actions
    clp.OnAction("add", []() { Action = ACTION_ADD; });
    clp.OnAction("discover", []() { Action = ACTION_DISCOVER; });
    clp.OnAction("getlog", []() { Action = ACTION_GETLOG; });
    clp.OnAction("list", []() { Action = ACTION_LIST; });
    clp.OnAction("pull", []() { Action = ACTION_PULL; });
    clp.OnAction("push", []() { Action = ACTION_PUSH; });
    clp.OnAction("refresh", []() { Action = ACTION_REFRESH; });
    clp.OnAction("remove", []() { Action = ACTION_REMOVE; });
    clp.OnAction("restart", []() { Action = ACTION_RESTART; });
    clp.OnAction("update", []() { Action = ACTION_UPDATE; });

    clp.Parse(argc, argv);

    try {
        Orchestrator = new OrchestratorCLI(ConfigFile);
    } catch(const std::exception& e) {
        fprintf(stderr, "Error: %s\r\n\r\n", e.what());
        exit(1);
    }

    switch (Action) {
        case ACTION_ADD : {

        } break;
        case ACTION_DISCOVER : {
            ShowResult("discover", Orchestrator->Discovery(TargetDevice));
        } break;
        case ACTION_GETLOG : {
            ShowResult("getlog", Orchestrator->GetLog(TargetDevice));
        } break;
        case ACTION_LIST : {
            Orchestrator->List(TargetDevice);
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