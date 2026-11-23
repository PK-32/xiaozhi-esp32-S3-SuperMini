#ifndef __HOME_NODES_H__
#define __HOME_NODES_H__

#include "mcp_server.h"
#include "board.h"

#include <esp_log.h>
#include <string>
#include <vector>

#define HOME_NODES_TAG "HomeNodes"

// One controllable device living on an ESP8266 node on your WiFi.
struct HomeDevice {
    const char* name;      // what you say, e.g. "fan", "bedroom light"
    const char* host;      // node IP address, e.g. "192.168.1.51"
    int channel;           // relay channel on that node (1, 2, ...)
};

// Lets the assistant switch devices on/off by sending a plain HTTP request
// to ESP8266 nodes. Only devices listed in the table can be controlled, so
// the AI can never send requests to arbitrary addresses.
class HomeNodes {
private:
    std::vector<HomeDevice> devices_;
    std::string token_;

    const HomeDevice* Find(const std::string& name) const {
        for (const auto& d : devices_) {
            if (name == d.name) return &d;
        }
        return nullptr;
    }

    // Returns empty string on success, otherwise an error message.
    std::string Send(const HomeDevice& d, const std::string& path_and_query) {
        std::string url = std::string("http://") + d.host + path_and_query +
                          "&token=" + token_;
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        if (auto opened = http->Open("GET", url); !opened) {
            return std::string("cannot reach ") + d.name + ": " + opened.error().ToString();
        }
        auto status = http->GetStatusCode();
        if (!status || *status != 200) {
            http->Close();
            return std::string(d.name) + " node returned an error";
        }
        http->ReadAll();
        http->Close();
        return "";
    }

public:
    HomeNodes(std::vector<HomeDevice> devices, const char* token)
        : devices_(std::move(devices)), token_(token) {
        auto& mcp = McpServer::GetInstance();

        std::string names;
        for (const auto& d : devices_) {
            if (!names.empty()) names += ", ";
            names += d.name;
        }

        mcp.AddTool("self.home.list_devices",
            "List the home devices that can be switched on or off.",
            PropertyList(),
            [names](const PropertyList&) -> ReturnValue { return names; });

        mcp.AddTool("self.home.set_device",
            "Switch a home device on or off. 'device' must be one of the names "
            "from self.home.list_devices (e.g. fan, light). Use for requests like "
            "'turn on the fan' or 'switch off the bedroom light'.",
            PropertyList({
                Property("device", kPropertyTypeString),
                Property("on", kPropertyTypeBoolean)
            }),
            [this](const PropertyList& p) -> ToolResult {
                auto name = p["device"].value<std::string>();
                bool on = p["on"].value<bool>();
                const HomeDevice* d = Find(name);
                if (d == nullptr) {
                    return std::unexpected("Unknown device: " + name);
                }
                std::string err = Send(*d, "/relay?ch=" + std::to_string(d->channel) +
                                           "&state=" + (on ? "1" : "0"));
                if (!err.empty()) {
                    ESP_LOGW(HOME_NODES_TAG, "%s", err.c_str());
                    return std::unexpected(err);
                }
                return ReturnValue(true);
            });

        mcp.AddTool("self.home.get_device_state",
            "Check whether a home device is currently on or off.",
            PropertyList({ Property("device", kPropertyTypeString) }),
            [this](const PropertyList& p) -> ToolResult {
                auto name = p["device"].value<std::string>();
                const HomeDevice* d = Find(name);
                if (d == nullptr) {
                    return std::unexpected("Unknown device: " + name);
                }
                std::string url = std::string("http://") + d->host + "/state?ch=" +
                                  std::to_string(d->channel) + "&token=" + token_;
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                if (auto opened = http->Open("GET", url); !opened) {
                    return std::unexpected(std::string("cannot reach ") + d->name);
                }
                auto status = http->GetStatusCode();
                if (!status || *status != 200) {
                    http->Close();
                    return std::unexpected(std::string(d->name) + " node returned an error");
                }
                std::string body = http->ReadAll();
                http->Close();
                return ReturnValue(body);
            });
    }
};

#endif // __HOME_NODES_H__
