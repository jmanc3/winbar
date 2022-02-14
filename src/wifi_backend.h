//
// Created by jmanc3 on 10/7/21.
//

#ifndef WINBAR_WIFI_BACKEND_H
#define WINBAR_WIFI_BACKEND_H

#include "application.h"

#include <string>
#include <vector>

struct ScanResult {
    std::string mac;
    std::string network_name;
    std::string connection_quality;
    std::string frequency;
    std::string flags;

    bool saved_network = false;
    int network_index = -1;
};

void wifi_start(App *app);

bool wifi_running();

void wifi_scan(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &results));

void wifi_scan_cached(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &results));

void wifi_networks_and_cached_scan(void (*function_called_when_results_are_returned)(std::vector<ScanResult> &results));

void wifi_forget_network(ScanResult scanResult);

void wifi_stop();

std::string get_default_wifi_interface();


#endif //WINBAR_WIFI_BACKEND_H
