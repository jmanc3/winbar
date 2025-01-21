//
// Created by jmanc3 on 10/7/21.
//

#ifndef WINBAR_WIFI_BACKEND_H
#define WINBAR_WIFI_BACKEND_H

#include "application.h"

#include <string>
#include <vector>

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
    std::string mac;
    std::string network_name;
    std::string connection_quality;
    std::string frequency;
    std::string flags;
    
    bool saved_network = false;
    int network_index = -1;
    
    int auth = AUTH_NONE_OPEN;
    int encr = 0;
};

void wifi_start(App *app);

bool wifi_running();

void wifi_scan(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &results));

void wifi_scan_cached(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &results));

void wifi_networks_and_cached_scan(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &results));

void wifi_forget_network(ScanResult scanResult);

void wifi_stop();

std::string get_default_wifi_interface(AppClient *client);


#endif //WINBAR_WIFI_BACKEND_H
