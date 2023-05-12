//
// Created by jmanc3 on 2/10/22.
//

#ifndef WINBAR_ICONS_H
#define WINBAR_ICONS_H

#include "application.h"

#include <utility>
#include <vector>
#include <string>
#include <algorithm>

// Load icons into memory
void set_icons_path_and_possibly_update(App *app);

// Remove all icons from memory
void unload_icons();

struct Candidate {
    std::string parent_path;
    std::string filename;
    std::string theme;
    int extension;
    int size;
    int scale;
    
    [[nodiscard]] std::string full_path() const {
        std::string temp = std::string(parent_path).append("/").append(filename);
        if (extension == 0) {
            temp.append(".svg");
        } else if (extension == 1) {
            temp.append(".png");
        } else if (extension == 2) {
            temp.append(".xmp");
        }
        temp.erase(std::remove(temp.begin(), temp.end(), '\0'), temp.end());
    
        return temp;
    }
    
    int size_index = 10;
    bool is_part_of_current_theme = false;
};

struct IconTarget {
    std::string name;
    std::vector<Candidate> candidates;
    
    std::string best_full_path;
    
    void *user_data = nullptr;
    
    IconTarget(std::string name) : name(std::move(name)) {}
    
    IconTarget(std::string name, void *user_data) : name(std::move(name)), user_data(user_data) {}
};

void search_icons(std::vector<IconTarget> &targets);

void pick_best(std::vector<IconTarget> &targets, int size);

bool has_options(const std::string &name);

std::string
c3ic_fix_desktop_file_icon(const std::string &given_name,
                           const std::string &given_wm_class,
                           const std::string &given_path,
                           const std::string &given_icon);

std::string
c3ic_fix_wm_class(const std::string &given_wm_class);

#endif //WINBAR_ICONS_H
