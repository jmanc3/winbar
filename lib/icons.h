//
// Created by jmanc3 on 2/10/22.
//

#ifndef WINBAR_ICONS_H
#define WINBAR_ICONS_H

#include "application.h"

#include <utility>
#include <vector>
#include <string>

// Load icons into memory
void load_icons(App *app);

// Remove all icons from memory
void unload_icons();

struct IndexResult {
    std::string pre_path;
    int size = 0;
    int scale = 1;
    std::string theme;
    std::string name;
    int extension = 0;

    IndexResult(std::string prePath, int size, int scale, std::string theme, std::string name, int extension)
            : pre_path(std::move(prePath)), size(size), scale(scale), theme(std::move(theme)), name(std::move(name)),
              extension(extension) {}
};

struct IconTarget {
    std::string name;
    std::vector<IndexResult> indexes_of_results;

    std::string best_full_path;

    void *user_data = nullptr;

    IconTarget(std::string name) : name(std::move(name)) {}

    IconTarget(std::string name, void *user_data) : name(std::move(name)), user_data(user_data) {}
};

void search_icons(std::vector<IconTarget> &targets);

void pick_best(std::vector<IconTarget> &targets, int size);

std::string
c3ic_fix_desktop_file_icon(const std::string &given_name,
                           const std::string &given_wm_class,
                           const std::string &given_path,
                           const std::string &given_icon);

std::string
c3ic_fix_wm_class(const std::string &given_wm_class);

#endif //WINBAR_ICONS_H
