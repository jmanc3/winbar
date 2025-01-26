//
// Created by jmanc3 on 10/7/21.
//

#ifndef WINBAR_WIFI_BACKEND_H
#define WINBAR_WIFI_BACKEND_H

#include "application.h"

#include <string>
#include <vector>
#include <wpa_ctrl.h>

enum {
	AUTH_NONE_OPEN,
	AUTH_NONE_WEP,
	AUTH_NONE_WEP_SHARED,
	AUTH_IEEE8021X,
	AUTH_WPA_PSK,
	AUTH_WPA_EAP,
	AUTH_WPA2_PSK,
	AUTH_WPA2_EAP
};

struct ScanResult {
    std::string interface;
    
    std::string mac;
    std::string network_name;
    std::string connection_quality;
    std::string frequency;
    std::string flags;
    std::string state_for_saved_networks;
    
    bool saved_network = false;
    int network_index = -1;
    
    int auth = AUTH_NONE_OPEN;
    int encr = 0;
};

struct InterfaceLink {
    std::string interface;
    wpa_ctrl *wpa_message_sender = nullptr;
    wpa_ctrl *wpa_message_listener = nullptr;
    std::vector<ScanResult> results;
};

struct WifiData {
    std::vector<std::string> seen_interfaces;
    
    std::vector<InterfaceLink *> links; // Holds the communication links for each interface
    
    void (*when_state_changed)() = nullptr;
};

extern WifiData *wifi_data;

void wifi_start(App *app);

bool wifi_running(std::string interface);

void wifi_scan(InterfaceLink *link);

void wifi_scan_cached(InterfaceLink *link);

void wifi_networks_and_cached_scan(InterfaceLink *link);

void wifi_forget_network(ScanResult scanResult);

void wifi_global_disable(InterfaceLink *link);

void wifi_global_enable(InterfaceLink *link);

bool wifi_global_status(InterfaceLink *link);

void wifi_stop();

void wifi_set_active(std::string interface);

std::string get_default_wifi_interface(AppClient *client);


#endif //WINBAR_WIFI_BACKEND_H
