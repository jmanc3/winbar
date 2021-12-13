//
// Created by jmanc3 on 10/7/21.
//

#include "wifi_backend.h"

#include <wpa_ctrl.h>
#include <sstream>

struct WifiData {
    int type = 0; // 0 will be nothing, 1 wpa_supplicant, 2 NetworkManager eventually

    wpa_ctrl *wpa_message_sender = nullptr;
    wpa_ctrl *wpa_message_listener = nullptr;

    void (*function_called_when_results_are_returned)(std::vector<ScanResult> &) = nullptr;
};

static WifiData *wifi_data = new WifiData;

std::string_view trim(std::string_view s) {
    s.remove_prefix(std::min(s.find_first_not_of(" \t\r\v\n"), s.size()));
    s.remove_suffix(std::min(s.size() - s.find_last_not_of(" \t\r\v\n") - 1, s.size()));

    return s;
}

static void wifi_wpa_parse_scan_results();

void wifi_wpa_has_message(App *app, int fd) {
    char buf[1000];
    size_t len = 1000;

    while (wpa_ctrl_pending(wifi_data->wpa_message_listener)) {
        if (wpa_ctrl_recv(wifi_data->wpa_message_listener, buf, &len) == 0) {
            std::string text(buf, len);
            if (text.find("CTRL-EVENT-CONNECTED") != std::string::npos) {

            } else if (text.find("CTRL-EVENT-DISCONNECTED") != std::string::npos) {

            } else if (text.find("CTRL-EVENT-SCAN-STARTED") != std::string::npos) {

            } else if (text.find("CTRL-EVENT-SCAN-FAILED") != std::string::npos) {
                if (wifi_data->function_called_when_results_are_returned) {
                    std::vector<ScanResult> empty;
                    wifi_data->function_called_when_results_are_returned(empty);
                }
            } else if (text.find("CTRL-EVENT-SCAN-RESULTS") != std::string::npos) {
                wifi_wpa_parse_scan_results();
            }
        }
    }
}

// TODO: wlp7s0 is hardcoded we need to check the folder instead
bool wifi_wpa_start(App *app) {
    wifi_data->wpa_message_sender = wpa_ctrl_open("/var/run/wpa_supplicant/wlp7s0");
    if (!wifi_data->wpa_message_sender)
        return false;
    wifi_data->wpa_message_listener = wpa_ctrl_open("/var/run/wpa_supplicant/wlp7s0");
    if (!wifi_data->wpa_message_listener)
        return false;
    if (wpa_ctrl_attach(wifi_data->wpa_message_listener) != 0)
        return false;

    int fd;
    if ((fd = wpa_ctrl_get_fd(wifi_data->wpa_message_listener)) != -1)
        return poll_descriptor(app, fd, EPOLLIN, wifi_wpa_has_message);
    return false;
}

void wifi_start(App *app) {
    if (wifi_wpa_start(app)) {
        wifi_data->type = 1;
        // TODO: we need to collect all the wifi cards and scans and such should be specific to a card
    }
}

void wifi_scan(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &)) {
    char buf[10];
    size_t len = 10;
    if (wpa_ctrl_request(wifi_data->wpa_message_listener, "SCAN", 4,
                         buf, &len, NULL) != 0)
        return;

    wifi_data->function_called_when_results_are_returned = function_called_when_results_are_returned;
}

void wifi_scan_cached(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &)) {
    wifi_data->function_called_when_results_are_returned = function_called_when_results_are_returned;
    wifi_wpa_parse_scan_results();
}

void get_scan_results(std::vector<ScanResult> *results) {
    std::vector<std::string> lines;

    char buf[10000];
    size_t len = 10000;
    if (wpa_ctrl_request(wifi_data->wpa_message_listener, "SCAN_RESULTS", 12,
                         buf, &len, NULL) != 0) {
        return;
    }

    std::string response(buf, len);
    auto response_stream = std::stringstream{response};
    for (std::string line; std::getline(response_stream, line, '\n');)
        lines.push_back(line);

    if (!lines.empty()) {
        auto header = lines[0];
        auto header_stream = std::stringstream{header};

        std::vector<std::string> header_order;
        for (std::string line; std::getline(header_stream, line, '/');) {
            auto trimmed = trim(line);
            header_order.emplace_back(trimmed.data(), trimmed.length());
        }

        for (int i = 1; i < lines.size(); i++) {
            auto line = lines[i];
            auto line_stream = std::stringstream{line};
            int x = 0;
            ScanResult result;
            for (std::string chunk; std::getline(line_stream, chunk, '\t');) {
                if (header_order[x] == "bssid") { // MAC Address
                    result.mac = chunk;
                } else if (header_order[x] == "frequency") { // 2g or 5g
                    result.frequency = chunk;
                } else if (header_order[x] == "signal level") { // Connection quality
                    result.connection_quality = chunk;
                } else if (header_order[x] == "flags") { // type of encryption on router?
                    result.flags = chunk;
                } else if (header_order[x] == "ssid") { // Network name
                    result.network_name = chunk;
                }
                x++;
            }
            bool duplicate = false;
            for (const auto &r: *results) {
                if (r.mac == result.mac || r.network_name == result.network_name) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            results->push_back(result);
        }
    }
}

void get_network_list(std::vector<ScanResult> *results) {
    std::vector<std::string> lines;

    char buf[10000];
    size_t len = 10000;
    if (wpa_ctrl_request(wifi_data->wpa_message_listener, "LIST_NETWORKS", 13,
                         buf, &len, NULL) != 0) {
        return;
    }

    std::string response(buf, len);
    auto response_stream = std::stringstream{response};
    for (std::string line; std::getline(response_stream, line, '\n');)
        lines.push_back(line);

    if (!lines.empty()) {
        auto header = lines[0];
        auto header_stream = std::stringstream{header};

        std::vector<std::string> header_order;
        for (std::string line; std::getline(header_stream, line, '/');) {
            auto trimmed = trim(line);
            header_order.emplace_back(trimmed.data(), trimmed.length());
        }

        for (int i = 1; i < lines.size(); i++) {
            auto line = lines[i];
            auto line_stream = std::stringstream{line};
            int x = 0;
            ScanResult result;
            result.saved_network = true;
            for (std::string chunk; std::getline(line_stream, chunk, '\t');) {
                if (header_order[x] == "network id") {
                    result.network_index = std::stoi(chunk);
                } else if (header_order[x] == "ssid") {
                    result.network_name = chunk;
                } else if (header_order[x] == "bssid") {
                } else if (header_order[x] == "flags") {
                    result.flags = chunk;
                }
                x++;
            }
            results->push_back(result);
        }
    }
}

void wifi_wpa_parse_scan_results() {
    if (wifi_data->function_called_when_results_are_returned) {
        std::vector<ScanResult> results;

        get_network_list(&results);

        get_scan_results(&results);

        wifi_data->function_called_when_results_are_returned(results);
    }
}

void wifi_networks_and_cached_scan(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &)) {
    std::vector<ScanResult> results;
    if (!function_called_when_results_are_returned) {
        return;
    }

    get_network_list(&results);

    get_scan_results(&results);

    function_called_when_results_are_returned(results);
}

void wifi_stop() {
    if (wifi_data->type == 1) {
        wpa_ctrl_close(wifi_data->wpa_message_sender);
        wpa_ctrl_close(wifi_data->wpa_message_listener);
        wpa_ctrl_detach(wifi_data->wpa_message_listener);
    }

    delete wifi_data;
    wifi_data = new WifiData;
}

void wifi_forget_network(ScanResult scanResult) {
    char buf[10000];
    size_t len = 10000;
    std::string message = "REMOVE_NETWORK " + std::to_string(scanResult.network_index);
    if (wpa_ctrl_request(wifi_data->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
}