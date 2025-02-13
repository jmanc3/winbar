//
// Created by jmanc3 on 10/7/21.
//

#include "wifi_backend.h"
#include "search_menu.h"
#include "settings_menu.h"

#include <sstream>
#include <utility.h>
#include <array>
#include <sys/wait.h>
#include <filesystem>

WifiData *wifi_data = new WifiData;

std::string_view trim(std::string_view s) {
    s.remove_prefix(std::min(s.find_first_not_of(" \t\r\v\n"), s.size()));
    s.remove_suffix(std::min(s.size() - s.find_last_not_of(" \t\r\v\n") - 1, s.size()));
    
    return s;
}

static void wifi_wpa_parse_scan_results(InterfaceLink *link);

void wifi_wpa_has_message(App *app, int fd, void *data) {
    auto link = (InterfaceLink *) data;
    char buf[1000];
    size_t len = 1000;
    
    while (wpa_ctrl_pending(link->wpa_message_listener)) {
        if (wpa_ctrl_recv(link->wpa_message_listener, buf, &len) == 0) {
            std::string text(buf, len);
            if (text.find("CTRL-EVENT-CONNECTED") != std::string::npos) {
            
            } else if (text.find("CTRL-EVENT-DISCONNECTED") != std::string::npos) {
            
            } else if (text.find("CTRL-EVENT-SCAN-STARTED") != std::string::npos) {
            
            } else if (text.find("CTRL-EVENT-SCAN-FAILED") != std::string::npos) {
                if (wifi_data->when_state_changed) {
                    wifi_data->when_state_changed();
                }
            } else if (text.find("CTRL-EVENT-SCAN-RESULTS") != std::string::npos) {
                wifi_wpa_parse_scan_results(link);
            }
        }
    }
}

bool wifi_wpa_start(App *app, const std::string &interface) {
    // TODO: use the interfaces in the preferred_interfaces list, also each interface needs it's own message sender receiver
    //  so we need a new data structure
    for (auto item: wifi_data->links)
        if (item->interface == interface)
            return true; // already have link
    
    std::string wpa_supplicant_path = "/var/run/wpa_supplicant/" + interface;
    auto link = new InterfaceLink();
    link->interface = interface;
    link->wpa_message_sender = wpa_ctrl_open(wpa_supplicant_path.data());
    if (!link->wpa_message_sender)
        return false;
    link->wpa_message_listener = wpa_ctrl_open(wpa_supplicant_path.data());
    if (!link->wpa_message_listener)
        return false;
    if (wpa_ctrl_attach(link->wpa_message_listener) != 0)
        return false;
    
    int fd;
    if ((fd = wpa_ctrl_get_fd(link->wpa_message_listener)) != -1) {
        wifi_data->links.emplace_back(link);
        return poll_descriptor(app, fd, EPOLLIN, wifi_wpa_has_message, link, "wpa");
    }
    return false;
}

void wifi_update_network_cards() {
    // This needs to be called after settings file has been read in so that we can determine if we need to perform a sort
    // after loading, or not. (If there are no preferred interfaces, it must mean that we are launching wifi for the first time.)
    bool do_sort = winbar_settings->preferred_interfaces.empty();
    
    std::string network_interfaces_dir = "/var/run/wpa_supplicant";
    namespace fs = std::filesystem;
    
    wifi_data->seen_interfaces.clear();
    
    try {
        for (const auto &entry: fs::directory_iterator(network_interfaces_dir)) {
            if (!entry.is_directory()) {
                wifi_data->seen_interfaces.push_back(entry.path().filename().string());
            }
        }
    } catch (std::exception &e) {
    
    }
    
    if (do_sort) {
        for (auto i: wifi_data->seen_interfaces) {
            if (i.find("-") == std::string::npos) {
                wifi_set_active(i);
            }
        }
        // Don't prefer p2p or names with - in them
        
        // Maximally prefer get_default_wifi_interface(client_by_name(app, "taskbar"), If it returns non-empty
    }
}

void wifi_start(App *app) {
    wifi_update_network_cards();
    
    for (auto i: wifi_data->seen_interfaces) {
        wifi_wpa_start(app, i);
    }
}

bool wifi_running(std::string interface) {
    for (auto l: wifi_data->links) {
        if (l->interface == interface) {
            return true;
        }
    }
    return false;
}

void wifi_scan(InterfaceLink *link) {
    if (!link) return;
    char buf[10];
    size_t len = 10;
    if (wpa_ctrl_request(link->wpa_message_listener, "SCAN", 4,
                         buf, &len, NULL) != 0)
        return;
}

void wifi_scan_cached(InterfaceLink *link) {
    if (!link) return;
    wifi_wpa_parse_scan_results(link);
}


void wifi_networks_and_cached_scan(InterfaceLink *link) {
    if (!link) return;
    wifi_scan(link);
    wifi_wpa_parse_scan_results(link);
}

void parse_wifi_flags(ScanResult *result) {
    auto flags = result->flags;
    if (flags.find("[CURRENT") != std::string::npos ||
        flags.find("[DISABLED") != std::string::npos ||
        flags.find("[ENABLED") != std::string::npos ||
        flags.find("[TEMP-DISABLED") != std::string::npos) {
        result->state_for_saved_networks = flags;
        return;
    }
    
    int auth, encr = 0;
    if (flags.find("[WPA2-EAP") != std::string::npos)
        auth = AUTH_WPA2_EAP;
    else if (flags.find("[WPA-EAP") != std::string::npos)
        auth = AUTH_WPA_EAP;
    else if (flags.find("[WPA2-PSK") != std::string::npos)
        auth = AUTH_WPA2_PSK;
    else if (flags.find("[WPA-PSK") != std::string::npos)
        auth = AUTH_WPA_PSK;
    else
        auth = AUTH_NONE_OPEN;
    
    if (flags.find("-CCMP") != std::string::npos)
        encr = 1;
    else if (flags.find("-TKIP") != std::string::npos)
        encr = 0;
    else if (flags.find("WEP") != std::string::npos) {
        encr = 1;
        if (auth == AUTH_NONE_OPEN)
            auth = AUTH_NONE_WEP;
    } else
        encr = 0;
    result->auth = auth;
    result->encr = encr;
}

void get_scan_results(InterfaceLink *link) {
    std::vector<std::string> lines;
    
    char buf[10000];
    size_t len = 10000;
    if (wpa_ctrl_request(link->wpa_message_listener, "SCAN_RESULTS", 12,
                         buf, &len, NULL) != 0) {
        return;
    }
    
    std::string response(buf, len);
    auto response_stream = std::stringstream{response};
    for (std::string line; std::getline(response_stream, line, '\n');)
        lines.push_back(line);
    
    std::vector<ScanResult> to_keep;
    
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
            result.interface = link->interface;
            result.is_scan_result = true;
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
            to_keep.push_back(result);
            bool duplicate = false;
            for (auto &r: link->results) {
//                printf("comparing: %s -- %s\n", );
                if (strcmp(r.mac.c_str(), result.mac.c_str()) == 0 ||
                    (r.saved_network && r.network_name == result.network_name)) {
                    r.frequency = result.frequency;
                    r.connection_quality = result.connection_quality;
                    parse_wifi_flags(&result);
                    r.flags = result.flags;
                    r.network_name = result.network_name;
                    r.is_scan_result = true;
                    r.auth = result.auth;
                    r.encr = result.encr;
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            
            parse_wifi_flags(&result);
            link->results.push_back(result);
        }
    }
    
    for (auto &item: link->results) {
        bool found = false;
        for (auto &keep: to_keep)
            if (keep.network_name == item.network_name)
                found = true;
        item.is_scan_result = found;
    }
}

void get_network_list(InterfaceLink *link) {
    std::vector<std::string> lines;
    
    char buf[10000];
    size_t len = 10000;
    if (wpa_ctrl_request(link->wpa_message_listener, "LIST_NETWORKS", 13,
                         buf, &len, NULL) != 0) {
        return;
    }
    
    std::string response(buf, len);
    auto response_stream = std::stringstream{response};
    for (std::string line; std::getline(response_stream, line, '\n');)
        lines.push_back(line);
    
    std::vector<ScanResult> to_keep;
    
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
            result.interface = link->interface;
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
            to_keep.push_back(result);
            bool duplicate = false;
            for (auto &r: link->results) {
                if (r.network_name == result.network_name) {
                    duplicate = true;
                    parse_wifi_flags(&result);
                    r.state_for_saved_networks = result.state_for_saved_networks;
                    r.saved_network = true;
                    r.network_index = result.network_index;
                    break;
                }
            }
            if (duplicate)
                continue;
            
            parse_wifi_flags(&result);
            link->results.push_back(result);
        }
    }
    
    for (int i = link->results.size() - 1; i >= 0; i--) {
        auto item = &link->results[i];
        bool found = false;
        for (auto &keep: to_keep)
            if (keep.network_name == item->network_name && keep.network_index == item->network_index)
                found = true;
        item->saved_network = found;
        
        if (!item->saved_network && !item->is_scan_result) {
            link->results.erase(link->results.begin() + i);
        }
    }
}

void wifi_wpa_parse_scan_results(InterfaceLink *link) {
    if (!link) return;
    
    if (wifi_data->when_state_changed) {
        get_scan_results(link);
        
        get_network_list(link);
        
        wifi_data->when_state_changed();
    }
}

void wifi_stop() {
    for (auto l: wifi_data->links) {
        wpa_ctrl_close(l->wpa_message_sender);
        wpa_ctrl_close(l->wpa_message_listener);
        wpa_ctrl_detach(l->wpa_message_listener);
        delete l;
    }
    
    delete wifi_data;
    wifi_data = new WifiData;
}

int set_network_param(int id, const char *field,
                      const char *value, bool quote, InterfaceLink *link) {
    char reply[10], cmd[256];
    size_t reply_len;
    snprintf(cmd, sizeof(cmd), "SET_NETWORK %d %s %s%s%s",
             id, field, quote ? "\"" : "", value, quote ? "\"" : "");
    reply_len = sizeof(reply);
    wpa_ctrl_request(link->wpa_message_listener, cmd, strlen(cmd),
                     reply, &reply_len, NULL);
    return strncmp(reply, "OK", 2) == 0 ? 0 : -1;
}

InterfaceLink *get_link(const ScanResult &scan) {
    InterfaceLink *link = nullptr;
    for (auto l: wifi_data->links) {
        if (l->interface == scan.interface) {
            link = l;
            break;
        }
    }
    return link;
}

void wifi_forget_network(ScanResult result) {
    auto link = get_link(result);
    if (!link) return;
    defer(wifi_networks_and_cached_scan(link));
    char buf[1000];
    size_t len = 1000;
    std::string message = "REMOVE_NETWORK " + std::to_string(result.network_index);
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
}

void wifi_connect_network(ScanResult result, std::string in) {
    auto link = get_link(result);
    if (!link) return;
    defer(wifi_networks_and_cached_scan(link));
    char buf[1000];
    size_t len = 1000;
    std::string message = "ADD_NETWORK";
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return; // I think this is the failure case, and
    }
    auto ans = std::string(buf, len);
    if (trim(std::string(result.network_name)).empty()) {
        message = "SET_NETWORK " + ans + " bssid \"" + result.mac + "\"";
    } else {
        message = "SET_NETWORK " + ans + " ssid \"" + result.network_name + "\"";
    }
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
    auto not_enc = result.auth == AUTH_NONE_OPEN || result.auth == AUTH_NONE_WEP;
    if (not_enc) {
        message = "SET_NETWORK " + ans + " key_mgmt NONE";
    } else if (in.size() == 64) {
        message = "SET_NETWORK " + ans + " psk " + in;
    } else {
        message = "SET_NETWORK " + ans + " psk \"" + in + "\"";
    }
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
    message = "SET_NETWORK " + ans + " mesh_fwding 0";
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
    message = "ENABLE_NETWORK " + ans;
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
    message = "RECONNECT";
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
}

std::string exec(const char *cmd) {
    std::array<char, 6000> buffer{};
    std::string result;
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    pclose(pipe); // Ensure the pipe is closed to avoid resource leaks.
    return result;
}

std::string get_default_wifi_interface(AppClient *client) {
    static std::string cached_interface;
    
    if (!client)
        return cached_interface;
    
    static long program_start_time = get_current_time_in_ms();
    static long previous_cache_time = 0; // only set to 0 the first time this function is called because of: "static"
    
    long current_time = get_current_time_in_ms();
    int cache_timeout = 1000 * 60;
    if (current_time - program_start_time < 1000 * 20)
        cache_timeout = 2000;
    if (current_time - previous_cache_time > cache_timeout) { // Re-cache Wifi interface every five minutes
        previous_cache_time = current_time;
        
        if (script_exists("route")) { // Try to use legacy "route" to find default interface
            std::string response = exec("route");
            LineParser parser(response);
            parser.simple = true;
            while (parser.current_token != LineParser::END_OF_LINE) {
                defer(parser.next());
                auto line = parser.until(LineParser::NEWLINE);
                auto p = LineParser(line);
                int column = 0;
                bool found_iface_column = false;
                while (p.current_token != LineParser::END_OF_LINE) {
                    defer(p.next());
                    if (p.current_token == LineParser::IDENT) {
                        if (p.text() == "Iface") {
                            found_iface_column = true;
                            break;
                        }
                        column++;
                    }
                }
                if (found_iface_column) {
                    while (parser.current_token != LineParser::END_OF_LINE) {
                        defer(parser.next());
                        auto line = parser.until(LineParser::NEWLINE);
                        auto p = LineParser(line);
                        if (p.current_token == LineParser::IDENT) {
                            if (p.text() == "default") {
                                int col = 0;
                                while (p.current_token != LineParser::END_OF_LINE) {
                                    defer(p.next());
                                    if (p.current_token == LineParser::IDENT) {
                                        if (col == column) {
                                            cached_interface = p.text();
                                            return cached_interface;
                                        }
                                        col++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        if (script_exists("ip")) { // Try to use "ip" to find default interface
            std::string response = exec("ip route");
            LineParser parser(response);
            parser.simple = true;
            while (parser.current_token != LineParser::END_OF_LINE) {
                defer(parser.next());
                auto line = parser.until(LineParser::NEWLINE);
                LineParser p(line);
                if (p.current_token == LineParser::IDENT) {
                    auto title = p.text();
                    if (title == "default") {
                        for (int i = 0; i < 8; ++i)
                            p.next();
                        if (p.current_token == LineParser::IDENT) {
                            cached_interface = p.text();
                            return cached_interface;
                        }
                    }
                }
            }
        }
    }
    
    return cached_interface;
}

bool wifi_global_status(InterfaceLink *link) {
    if (!link) return false;
    
    std::vector<std::string> lines;
    
    char buf[10000];
    size_t len = 10000;
    if (wpa_ctrl_request(link->wpa_message_listener, "STATUS", 6,
                         buf, &len, NULL) != 0) {
        return false;
    }
    
    std::string response(buf, len);
    auto response_stream = std::stringstream{response};
    for (std::string line; std::getline(response_stream, line, '\n');)
        lines.push_back(line);
    
    if (!lines.empty()) {
        for (const auto &line: lines) {
            if (line == "wpa_state=COMPLETED") { // SCANNING other options
                return true;
            }
        }
    }
    
    return false;
}

void wifi_global_disable(InterfaceLink *link) {
    if (!link) return;
    
    char buf[1000];
    size_t len = 1000;
    std::string message = "DISCONNECT";
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
}

void wifi_global_enable(InterfaceLink *link) {
    if (!link) return;
    
    char buf[1000];
    size_t len = 1000;
    std::string message = "RECONNECT";
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
}

void wifi_set_active(std::string interface) {
    for (const auto &i: wifi_data->seen_interfaces) {
        if (i == interface) {
            winbar_settings->set_preferred_interface(i);
        }
    }
}

void wifi_save_config(InterfaceLink *link) {
    if (!link) return;
    
    char buf[1000];
    size_t len = 1000;
    std::string message = "SAVE_CONFIG";
    if (wpa_ctrl_request(link->wpa_message_listener, message.c_str(), message.length(),
                         buf, &len, NULL) != 0) {
        return;
    }
}