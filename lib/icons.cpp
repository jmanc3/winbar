
#include "icons.h"
#include "INIReader.h"
#include <cmath>
#include <sys/stat.h>

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

// cache file specification
//
// line 1:
// zero terminated strings of backup theme names
//
// rest of lines:
// [4] bytes representing the Size: integer
// [1] byte representing the Scale:
// [1] byte Type:
// zero terminated string of Context
// zero terminated string of directory name
//    then icon entries until the end of the line
//    an icon entry is
//    [1] byte representing the extension of the image
//    zero terminated string representing the name of the image

void
c3ic_generate_sizes(int target_size,
                    std::vector<int> &target_sizes) {
#ifdef TRACY_ENABLE
    ZoneScoped
#endif

    target_sizes.push_back(8);
    target_sizes.push_back(12);
    target_sizes.push_back(16);
    target_sizes.push_back(18);
    target_sizes.push_back(24);
    target_sizes.push_back(32);
    target_sizes.push_back(48);
    target_sizes.push_back(64);
    target_sizes.push_back(96);
    target_sizes.push_back(128);
    target_sizes.push_back(256);
    target_sizes.push_back(512);

    std::sort(target_sizes.begin(), target_sizes.end(), [target_size](int a, int b) {
        // Prefer higher pixel icons to lower ones
        long absolute_difference_between_a_and_the_target = std::abs(target_size - a);
        bool a_is_too_low = a < target_size;
        long absolute_difference_between_b_and_the_target = std::abs(target_size - b);
        bool b_is_too_low = b < target_size;

        if (a_is_too_low || b_is_too_low) {
            if (a_is_too_low && !b_is_too_low)
                return false;
            if (b_is_too_low && !a_is_too_low)
                return true;
            return absolute_difference_between_a_and_the_target < absolute_difference_between_b_and_the_target;
        }

        return absolute_difference_between_a_and_the_target < absolute_difference_between_b_and_the_target;
    });
}

std::string
get_current_theme_name() {
#ifdef TRACY_ENABLE
    ZoneScoped
#endif

    std::string gtk_settings_file_path(getenv("HOME"));
    gtk_settings_file_path += "/.config/gtk-3.0/settings.ini";

    INIReader gtk_settings(gtk_settings_file_path);
    if (gtk_settings.ParseError() != 0) {
        // hicolor is a theme that is always supposed to be installed
        // so if we aren't able to find a gtk set theme we return hicolor
        return "hicolor";
    }

    return gtk_settings.Get("Settings", "gtk-icon-theme-name", "hicolor");
}

std::vector<Icon *>
c3ic_strict_find_icons(const std::string &name,
                       const std::vector<int> &strict_sizes,
                       const std::vector<int> &strict_scales,
                       const std::vector<IconExtension> &strict_extensions) {
    std::string theme = get_current_theme_name();
    return c3ic_strict_find_icons(theme, name, strict_sizes, strict_scales, strict_extensions);
}

#include <filesystem>
#include <fcntl.h>
#include <zconf.h>
#include <sys/mman.h>
#include <fstream>
#include <dirent.h>

namespace fs = std::filesystem;

static std::optional<int> ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return {};
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return {};
    bool b = strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
    if (!b) {
        return {};
    }
    return lenstr - lensuffix;
}

//
//   TODO: we should write to a temporary cache file and then move it once we
//    finish so we don't get corrupted files when interrupted
//
bool
c3ic_cache_the_theme(const std::string &theme) {
#ifdef TRACY_ENABLE
    ZoneScoped
#endif

    //
    // Find and parse index.theme ini file
    //
    std::string theme_index_path("/usr/share/icons/");
    theme_index_path += theme + "/index.theme";
    INIReader theme_index(theme_index_path);

    if (theme_index.ParseError() != 0) {
//        printf("\"index.theme\" file was not in the root directory of the icon theme: %s\n", theme.c_str());
        return false;
    }

    //
    // Make the cache file that we are going to write to
    //
    std::ofstream cache_file;
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("Create and open cache file")
#endif
        const char *home_directory = getenv("HOME");
        std::string icon_cache_path(home_directory);
        icon_cache_path += "/.cache";

        if (mkdir(icon_cache_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
            if (errno != EEXIST) {
//                printf("Couldn't mkdir %s\n", icon_cache_path.c_str());
                return false;
            }
        }
        icon_cache_path += "/c3_icon_cache";
        if (mkdir(icon_cache_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
            if (errno != EEXIST) {
//                printf("Couldn't mkdir %s\n", icon_cache_path.c_str());
                return false;
            }
        }

        icon_cache_path += "/" + theme + ".cache";

        cache_file.open(icon_cache_path);
        if (!cache_file.is_open()) {
//            printf("Tried and failed at create icon_cache file: %s\n", icon_cache_path.c_str());
            return false;
        }
    }

    //
    // Write the cache
    //
    // read 'cache file specification' found at the top of this file
    //
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("Write backup themes to cache file")
#endif
        std::string backup_themes = theme_index.Get("Icon Theme", "Inherits", "hicolor");
        std::stringstream ss(backup_themes);
        while (ss.good()) {
            std::string substr;
            getline(ss, substr, ',');
            cache_file << substr << '\0';
        }
        cache_file << '\n';
    }

    for (const std::string &section_title : theme_index.Sections()) {
        {
#ifdef TRACY_ENABLE
            ZoneScopedN("Write size, scale, type, and title for section")
#endif
            unsigned int size = (unsigned int) theme_index.GetInteger(section_title, "Size", 0);
            if (size == 0) continue;
            unsigned char scale = (unsigned char) theme_index.GetInteger(section_title, "Scale", 1);
            std::string type_string = theme_index.Get(section_title, "Type", "Fixed");
            unsigned char type = type_string == "Fixed" ? (unsigned char) IconType::FIXED :
                                 type_string == "Scalable" ? (unsigned char) IconType::SCALABLE :
                                 (unsigned char) IconType::THRESHOLD;
            std::string context = theme_index.Get(section_title, "Context", "Unknown");
            cache_file.write(reinterpret_cast<const char *>(&size), sizeof(size));
            cache_file << scale;
            cache_file << type;
            cache_file << context << '\0';
            cache_file << section_title << '\0';
        }

        {
#ifdef TRACY_ENABLE
            ZoneScopedN("Write the file extension and name")
#endif
            std::string section_directory("/usr/share/icons/");
            section_directory += theme + "/";
            section_directory += section_title;

            DIR *dir;
            struct dirent *entry;
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("Open directory")
#endif
                if (!(dir = opendir(section_directory.c_str()))) {
                    cache_file << '\n';
                    continue;
                }
            }
            while ((entry = readdir(dir)) != NULL) {
                {
#ifdef TRACY_ENABLE
                    ZoneScopedN("Entry Start")
#endif
                    if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
                        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                            continue;
                        if ((strstr(entry->d_name, "org.flameshot.Flameshot") != nullptr)) {
                            int k = 0;
                        }
                        // TODO: using strstr doesn't perfectly protect against multiple extensions but it's good enough probably
                        std::optional<int> index;
                        if (index = ends_with(entry->d_name, ".svg")) {
                            cache_file << (unsigned char) IconExtension::SVG;
                        } else if (index = ends_with(entry->d_name, ".png")) {
                            cache_file << (unsigned char) IconExtension::PNG;
                        } else if (index = ends_with(entry->d_name, ".xpm")) {
                            cache_file << (unsigned char) IconExtension::XPM;
                        }
                        if (index) {
                            cache_file.write(entry->d_name, index.value());
                            cache_file << '\0';
                        }
                    }
                }
            }
            closedir(dir);
            cache_file << '\n';
        }
    }

    cache_file.close();

    return true;
}

std::optional<std::tuple<int, int, char *>>
c3i3_load_theme(const std::string &theme) {
#ifdef TRACY_ENABLE
    ZoneScoped
#endif

    const char *home_directory = getenv("HOME");
    std::string icon_cache_path(home_directory);
    icon_cache_path += "/.cache/c3_icon_cache/" + theme + ".cache";

    struct stat buffer{};
    int cache_exists = stat(icon_cache_path.c_str(), &buffer) == 0;

    // TODO: we have to compare modified time of the folders to see if we are up to date
    if (!cache_exists) {
        if (!(cache_exists = c3ic_cache_the_theme(theme))) {
//            printf("Couldn't cache icon theme: %s", theme.c_str());
            return {};
        }
    }

    // Attempt to mmap the file
    int file_descriptor = open(icon_cache_path.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
    struct stat sb;
    if (fstat(file_descriptor, &sb) == -1) {
//        printf("Couldn't get file size: %s\n", icon_cache_path.c_str());
        close(file_descriptor);
        return {};
    }

    char *cached_theme_data = (char *) mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    if (!cached_theme_data) {
//        printf("Couldn't mmap file.\n");
        close(file_descriptor);
        return {};
    }

    return std::tuple<int, int, char *>(file_descriptor, sb.st_size, cached_theme_data);
}

void
c3ic_strict_load_icons(std::vector<Icon *> &icons,
                       const std::string &theme,
                       const std::string &name,
                       const std::vector<int> &strict_sizes,
                       const std::vector<int> &strict_scales,
                       const std::vector<IconExtension> &strict_extensions,
                       const bool is_parent_theme) {
#ifdef TRACY_ENABLE
    ZoneScoped
#endif

    int file_descriptor;
    char *cached_theme;
    int cached_theme_size;
    if (auto data = c3i3_load_theme(theme)) {
        std::tie(file_descriptor, cached_theme_size, cached_theme) = data.value();
    } else {
//        printf("Couldn't create/open cache for theme: %s\n", theme.c_str());
        return;
    }

    // try to mmap the cache and then parse and add matching icons
    //
    // read 'cache file specification' found at the top of this file

    std::vector<std::string> backup_themes;

    char temp_backup_theme_buffer[1024 * 6];
    unsigned long index_into_file = 0;
#define NOT_DONE index_into_file < cached_theme_size
    while (NOT_DONE) {
        if (cached_theme[index_into_file] == '\n') {
            index_into_file++;
            break;
        }
        strcpy(temp_backup_theme_buffer, cached_theme + index_into_file);
        index_into_file += strlen(temp_backup_theme_buffer) + 1;
        if (is_parent_theme) {
            backup_themes.emplace_back(temp_backup_theme_buffer);
        }
    }

    const char *c_name = name.c_str();

    unsigned int size;
    unsigned int scale;
    IconType type;
    char buffer_context[1024 * 6];
    char buffer_directory[1024 * 6];
    char buffer_icon_name[1024 * 6];

    while (NOT_DONE) {
        size = *(unsigned int *) (cached_theme + index_into_file);
        index_into_file += 4;
        scale = (unsigned int) cached_theme[index_into_file++];
        type = (IconType) cached_theme[index_into_file++];

        strcpy(buffer_context, cached_theme + index_into_file);
        index_into_file += strlen(buffer_context) + 1;

        strcpy(buffer_directory, cached_theme + index_into_file);
        index_into_file += strlen(buffer_directory) + 1;

        while (NOT_DONE && cached_theme[index_into_file] != '\n') {
            IconExtension extension = (IconExtension) cached_theme[index_into_file++];

            strcpy(buffer_icon_name, cached_theme + index_into_file);
            index_into_file += strlen(buffer_icon_name) + 1;

            if (strcmp(buffer_icon_name, c_name) == 0) {
                auto size_matches = std::find(strict_sizes.begin(), strict_sizes.end(), size);
                if (size_matches == strict_sizes.end())
                    continue; // SKIP THIS ICON, DOESN'T MEET REQUIREMENTS
                auto extension_matches = std::find(strict_extensions.begin(), strict_extensions.end(), extension);
                if (extension_matches == strict_extensions.end())
                    continue; // SKIP THIS ICON, DOESN'T MEET REQUIREMENTS
                auto scale_matches = std::find(strict_scales.begin(), strict_scales.end(), scale);
                if (scale_matches == strict_scales.end())
                    continue; // SKIP THIS ICON, DOESN'T MEET REQUIREMENTS

                auto icon = new Icon();
                icon->size = size;
                icon->scale = scale;
                icon->theme = theme;
                std::string file_extension_string;
                switch (extension) {
                    case SVG: {
                        file_extension_string = ".svg";
                        break;
                    }
                    case PNG: {
                        file_extension_string = ".png";
                        break;
                    }
                    case XPM: {
                        file_extension_string = ".xpm";
                        break;
                    }
                }
                icon->path += "/usr/share/icons/";
                icon->path += theme;
                icon->path += "/";
                icon->path += buffer_directory;
                icon->path += "/";
                icon->path += buffer_icon_name + file_extension_string;
                icon->extension = extension;
                icons.emplace_back(icon);
//                printf("Context: %s, Size: %d, Scale: %d, Type: %d, Directory: %s, Icon Name: %s, Extension %d\n",
//                       buffer_context, size, scale, type, buffer_directory, buffer_icon_name, extension);
            }
        }
        index_into_file++;
    }

    munmap(cached_theme, cached_theme_size);
    close(file_descriptor);

    // We don't want to recurse children backup themes, only top-level parent backup themes
    if (is_parent_theme && icons.empty()) {
        bool tried_loading_hicolor = false;
        for (const auto &backup_theme : backup_themes) {
            // so we don't recurse hicolor twice in some instances
            if (backup_theme == "hicolor" && theme == "hicolor") {
                tried_loading_hicolor = true;
                c3ic_strict_load_icons(icons, "Papirus", name, strict_sizes, strict_scales, strict_extensions, false);
                continue;
            }
            if (theme == "hicolor") {
                tried_loading_hicolor = true;
                if (theme != "Papirus") {
                    c3ic_strict_load_icons(icons, "Papirus", name, strict_sizes, strict_scales, strict_extensions, false);
                }
            }
            c3ic_strict_load_icons(icons, backup_theme, name, strict_sizes, strict_scales, strict_extensions, false);
        }
        if (!tried_loading_hicolor) {
            c3ic_strict_load_icons(icons, "Papirus", name, strict_sizes, strict_scales, strict_extensions, false);
            c3ic_strict_load_icons(icons, "hicolor", name, strict_sizes, strict_scales, strict_extensions, false);
        }
    }
}

std::vector<Icon *>
c3ic_strict_find_icons(const std::string &theme,
                       const std::string &name,
                       const std::vector<int> &strict_sizes,
                       const std::vector<int> &strict_scales,
                       const std::vector<IconExtension> &strict_extensions) {
#ifdef TRACY_ENABLE
    ZoneScoped
#endif
    std::vector<Icon *> icons;

    c3ic_strict_load_icons(icons, theme, name, strict_sizes, strict_scales, strict_extensions, true);

    if (icons.empty()) {
        // If we got here, it means we already searched through the currently active gtk theme and
        // at the very least the hicolor theme which is supposed to always exists according to spec
        //
        // therefore:
        //
        // !!!!!!!!!!NUCLEAR OPTION!!!!!!!!!!
        // SEARCH EVERY THEME!!! WAHAHAHAHAHAHA. YOU WILL NOT DETER ME
        // MISCONFIGURED *.DESKTOP FILES,
        // AND WRONGLY SET WM_CLASS NAMES.
        // I WILL PREVAIL.
        //
        // Also, it's not that nuclear. It takes less than 1 millisecond to search through the entirety of Papirus
        // and everything is cached so we aren't hitting the hard-drive/ssd hard at all.
        //
        std::string icons_directory("/usr/share/icons/");
        DIR *dir;
        struct dirent *entry;
        if ((dir = opendir(icons_directory.c_str()))) {
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_DIR) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                        continue;
                    std::string nuclear_theme_name = std::string(entry->d_name);
                    if (nuclear_theme_name != theme && nuclear_theme_name != "hicolor" && nuclear_theme_name != "Papirus") {
                        c3ic_strict_load_icons(icons, nuclear_theme_name, name, strict_sizes, strict_scales,
                                               strict_extensions, false);
                    }
                }
            }
        }
    }

    // Sort by quality, size most important, followed by extension, followed by scale
    for (auto *icon : icons) {
        for (int i = 0; i < strict_sizes.size(); ++i) {
            if (strict_sizes[i] == icon->size) {
                icon->size_index = i;
                break;
            }
        }
        for (int i = 0; i < strict_scales.size(); ++i) {
            if (strict_scales[i] == icon->scale) {
                icon->scale_index = i;
                break;
            }
        }
        for (int i = 0; i < strict_extensions.size(); ++i) {
            if (strict_extensions[i] == icon->extension) {
                icon->extention_index = i;
                break;
            }
        }
    }

    std::sort(icons.begin(), icons.end(), [](const Icon *a, const Icon *b) {
        int weight_a = a->extention_index * 30 + a->size_index * 20 + a->scale_index * 10;
        int weight_b = b->extention_index * 30 + b->size_index * 20 + b->scale_index * 10;

        return weight_a < weight_b;
    });

//    if (name == "st") {
//        int k = 0;
//    }

    return icons;
}

std::string
find_icon(const std::string &name, int size) {
    std::vector<int> strict_sizes;
    c3ic_generate_sizes(size, strict_sizes);
    std::vector<int> strict_scales = {1, 2};
    std::vector<IconExtension> strict_extensions = {IconExtension::SVG, IconExtension::PNG};
    std::vector<Icon *> options = c3ic_strict_find_icons(name, strict_sizes, strict_scales, strict_extensions);

    if (options.empty()) {
        return "";
    }

//    printf("%s\n", options[0]->path.c_str());
    std::string path = options[0]->path;
    for (auto i : options)
        delete i;
    options.clear();
    return path;
}

std::string
c3ic_fix_desktop_file_icon(const std::string &given_name,
                           const std::string &given_wm_class,
                           const std::string &given_path,
                           const std::string &given_icon) {
    // mmap tofix.csv file
    const char *home_directory = getenv("HOME");
    std::string to_fix_path(home_directory);
    to_fix_path += "/.config/winbar/tofix.csv";

    struct stat buffer{};
    int cache_exists = stat(to_fix_path.c_str(), &buffer) == 0;

    // TODO: we have to compare modified time of the folders to see if we are up to date
    if (!cache_exists) {
//        printf("%s doesn't exists\n", to_fix_path.c_str());
        return given_icon;
    }

    // Attempt to mmap the file
    int file_descriptor = open(to_fix_path.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
    struct stat sb;
    if (fstat(file_descriptor, &sb) == -1) {
//        printf("Couldn't get file size: %s\n", to_fix_path.c_str());
        close(file_descriptor);
        return given_icon;
    }

    char *to_fix_data = (char *) mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    if (!to_fix_data) {
//        printf("Couldn't mmap file.\n");
        close(file_descriptor);
        return given_icon;
    }

    unsigned long index_into_file = 0;

    // Eat the first line;
#define MNOT_DONE index_into_file < buffer.st_size
    while (MNOT_DONE && to_fix_data[index_into_file] != '\n') {
        index_into_file++;
    }
    index_into_file++;

    int loading_type = 0;
    int offset = 0;
    char first[1024 * 6] = {0};
    char second[1024 * 6] = {0};
    char third[1024 * 6] = {0};
    char fourth[1024 * 6] = {0};
    bool should_change = false;

    const char *given_name_c = given_name.c_str();
    const char *given_wm_class_c = given_wm_class.c_str();

    char given_path_c[1024 * 6] = {0};
    const char *temp_given_path_c = given_path.c_str();

    for (int i = 0; i < given_path.size() + 1; i++) {
        char c = temp_given_path_c[i];
        if (c == '/') {
            offset = 0;
        } else {
            given_path_c[offset++] = c;
        }
    }
    offset = 0;

    while (MNOT_DONE) {
        if (to_fix_data[index_into_file] == ',') {
            switch (loading_type) {
                case 0: {
                    first[offset] = '\0';
                    if (strcmp(first, given_name_c) == 0) {
                        should_change = true;
                    }
                    break;
                }
                case 1: {
                    second[offset] = '\0';
                    if (strcmp(second, given_wm_class_c) == 0) {
                        should_change = true;
                    }
                    break;
                }
                case 2: {
                    third[offset] = '\0';

                    if (strcmp(third, given_path_c) == 0) {
                        should_change = true;
                    }
                    break;
                }
            }
            index_into_file++;
            loading_type++;
            offset = 0;
            continue;
        }
        if (to_fix_data[index_into_file] == '\n') {
            fourth[offset] = '\0';
            index_into_file++;
            if (should_change) {
                should_change = false;
                // return fourth
                munmap(to_fix_data, sb.st_size);
                close(file_descriptor);
                return fourth;
            }
            loading_type = 0;
            offset = 0;
            continue;
        }
        switch (loading_type) {
            case 0: {
                first[offset++] = to_fix_data[index_into_file++];
                break;
            }
            case 1: {
                second[offset++] = to_fix_data[index_into_file++];
                break;
            }
            case 2: {
                // for the directory, we only want to save the file name not the entire directory path
                char previous_char = to_fix_data[index_into_file];
                third[offset++] = to_fix_data[index_into_file++];
                if (previous_char == '/') {
                    offset = 0;
                }
                break;
            }
            default: {
                fourth[offset++] = to_fix_data[index_into_file++];
                break;
            }
        }
    }

    munmap(to_fix_data, sb.st_size);
    close(file_descriptor);

    return given_icon;
}

std::string
c3ic_fix_wm_class(const std::string &given_wm_class) {
    return c3ic_fix_desktop_file_icon(given_wm_class, given_wm_class, given_wm_class, given_wm_class);
}

void
c3ic_strict_load_multiple_icons(std::vector<Icon> &icons,
                                const std::string &theme,
                                std::vector<std::string> &names,
                                const std::vector<int> &strict_sizes,
                                const std::vector<int> &strict_scales,
                                const std::vector<IconExtension> &strict_extensions,
                                const bool is_parent_theme) {
#ifdef TRACY_ENABLE
    ZoneScoped
#endif

    int file_descriptor;
    char *cached_theme;
    int cached_theme_size;
    if (auto data = c3i3_load_theme(theme)) {
        std::tie(file_descriptor, cached_theme_size, cached_theme) = data.value();
    } else {
//        printf("Couldn't create/open cache for theme: %s\n", theme.c_str());
        return;
    }

    // try to mmap the cache and then parse and add matching icons
    //
    // read 'cache file specification' found at the top of this file

    std::vector<std::string> backup_themes;

    char temp_backup_theme_buffer[1024 * 6];
    unsigned long index_into_file = 0;
#define NOT_DONE index_into_file < cached_theme_size
    while (NOT_DONE) {
        if (cached_theme[index_into_file] == '\n') {
            index_into_file++;
            break;
        }
        strcpy(temp_backup_theme_buffer, cached_theme + index_into_file);
        index_into_file += strlen(temp_backup_theme_buffer) + 1;
        if (is_parent_theme) {
            backup_themes.emplace_back(temp_backup_theme_buffer);
        }
    }

    unsigned int size;
    unsigned int scale;
    IconType type;
    char buffer_context[1024 * 6];
    char buffer_directory[1024 * 6];
    char buffer_icon_name[1024 * 6];

    while (NOT_DONE) {
        size = *(unsigned int *) (cached_theme + index_into_file);
        index_into_file += 4;
        scale = (unsigned int) cached_theme[index_into_file++];
        type = (IconType) cached_theme[index_into_file++];

        strcpy(buffer_context, cached_theme + index_into_file);
        index_into_file += strlen(buffer_context) + 1;

        strcpy(buffer_directory, cached_theme + index_into_file);
        index_into_file += strlen(buffer_directory) + 1;

        while (NOT_DONE && cached_theme[index_into_file] != '\n') {
            IconExtension extension = (IconExtension) cached_theme[index_into_file++];

            strcpy(buffer_icon_name, cached_theme + index_into_file);
            index_into_file += strlen(buffer_icon_name) + 1;

            for (const auto &name : names) {
                if (strcmp(buffer_icon_name, name.c_str()) == 0) {
                    auto size_matches = std::find(strict_sizes.begin(), strict_sizes.end(), size);
                    if (size_matches == strict_sizes.end())
                        continue; // SKIP THIS ICON, DOESN'T MEET REQUIREMENTS
                    auto extension_matches = std::find(strict_extensions.begin(), strict_extensions.end(), extension);
                    if (extension_matches == strict_extensions.end())
                        continue; // SKIP THIS ICON, DOESN'T MEET REQUIREMENTS
                    auto scale_matches = std::find(strict_scales.begin(), strict_scales.end(), scale);
                    if (scale_matches == strict_scales.end())
                        continue; // SKIP THIS ICON, DOESN'T MEET REQUIREMENTS

                    Icon icon;
                    icon.size = size;
                    icon.scale = scale;
                    icon.theme = theme;
                    icon.name = name;
                    std::string file_extension_string;
                    switch (extension) {
                        case SVG: {
                            file_extension_string = ".svg";
                            break;
                        }
                        case PNG: {
                            file_extension_string = ".png";
                            break;
                        }
                        case XPM: {
                            file_extension_string = ".xpm";
                            break;
                        }
                    }
                    icon.path += "/usr/share/icons/";
                    icon.path += theme;
                    icon.path += "/";
                    icon.path += buffer_directory;
                    icon.path += "/";
                    icon.path += buffer_icon_name + file_extension_string;
                    icon.extension = extension;
                    icons.emplace_back(icon);
//                printf("Context: %s, Size: %d, Scale: %d, Type: %d, Directory: %s, Icon Name: %s, Extension %d\n",
//                       buffer_context, size, scale, type, buffer_directory, buffer_icon_name, extension);
                }
            }
        }
        index_into_file++;
    }

    munmap(cached_theme, cached_theme_size);
    close(file_descriptor);
}

void
c3ic_strict_load_multiple_icons(std::vector<Icon> &icons,
                                std::vector<std::string> &names,
                                const std::vector<int> &strict_sizes,
                                const std::vector<int> &strict_scales,
                                const std::vector<IconExtension> &strict_extensions,
                                const bool is_parent_theme) {
    std::string theme = get_current_theme_name();

    c3ic_strict_load_multiple_icons(icons, theme, names, strict_sizes, strict_scales,
                                    strict_extensions, false);

    if (theme != "Papirus") {
        c3ic_strict_load_multiple_icons(icons, "Papirus", names, strict_sizes, strict_scales,
                                        strict_extensions, false);
    }

    c3ic_strict_load_multiple_icons(icons, "hicolor", names, strict_sizes, strict_scales,
                                    strict_extensions, false);

    std::string icons_directory("/usr/share/icons/");
    DIR *dir;
    struct dirent *entry;
    if ((dir = opendir(icons_directory.c_str()))) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                std::string nuclear_theme_name = std::string(entry->d_name);
                if (nuclear_theme_name != theme && nuclear_theme_name != "hicolor" && nuclear_theme_name != "Papirus") {
                    c3ic_strict_load_multiple_icons(icons, nuclear_theme_name, names, strict_sizes, strict_scales,
                                                    strict_extensions, false);
                }
            }
        }
    }
}