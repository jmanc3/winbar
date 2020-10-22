/* date = October 18th 2020 9:15 am */

#ifndef ICONS_H
#define ICONS_H

#include <string>
#include <vector>

enum IconExtension {
    SVG,
    PNG,
    XPM
};

enum IconType {
    FIXED,
    SCALABLE,
    THRESHOLD,
};

class Icon {
public:
    // Used for sorting, don't worry about these
    int size_index;
    int scale_index;
    int extention_index;

    int size;
    int scale;
    IconExtension extension;
    std::string theme;
    std::string path;
};

std::string
c3ic_fix_desktop_file_icon(const std::string &given_name,
                           const std::string &given_wm_class,
                           const std::string &given_icon);

std::string
c3ic_fix_wm_class(const std::string &given_wm_class);

void
c3ic_generate_sizes(int target_size,
                    std::vector<int> &target_sizes);

std::string
find_icon(const std::string &name, int size);

std::vector<Icon *>
c3ic_strict_find_icons(const std::string &name,
                       const std::vector<int> &strict_sizes,
                       const std::vector<int> &strict_scales,
                       const std::vector<IconExtension> &strict_extensions);

std::vector<Icon *>
c3ic_strict_find_icons(const std::string &theme,
                       const std::string &name,
                       const std::vector<int> &strict_sizes,
                       const std::vector<int> &strict_scales,
                       const std::vector<IconExtension> &strict_extensions);

#endif //ICONS_H
