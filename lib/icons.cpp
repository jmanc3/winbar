//
// Created by jmanc3 on 2/10/22.
//

#include "icons.h"
#include "utility.h"
#include "INIReader.h"
#include "../src/config.h"

#include <string>
#include <vector>
#include <sstream>
#include <sys/stat.h>
#include <fstream>
#include <dirent.h>
#include <cstring>
#include <optional>
#include <filesystem>
#include <fcntl.h>
#include <zconf.h>
#include <sys/mman.h>
#include <pango/pangocairo.h>
#include <math.h>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

static uint32_t cache_version = 2;

int getExtension(unsigned short int i) {
    // return the top two bits
    return i >> 14;
}

int getParentIndex(unsigned short int i) {
    unsigned short int temp = i;
    // turn off the top two bits
    temp &= ~(1UL << (15));
    temp &= ~(1UL << (14));
    return temp;
}

struct Option {
    unsigned short int parentIndexAndExtension;
    unsigned char themeIndex;
};

// Should be serializable.
struct OptionsData {
    // full path
    std::vector<std::string> parentPaths;
    
    std::vector<std::string> themes;
    
    // key is the name
    // we don't use an unordered map because we need to search by key when user is looking for icons
    std::map<std::string, std::vector<Option>> options;
    
    unsigned short int parentIndexOf(const std::string &path) {
        for (int i = parentPaths.size() - 1; i >= 0; --i) {
            if (parentPaths[i] == path) {
                return i;
            }
        }
        parentPaths.emplace_back(path);
        return parentPaths.size() - 1;
    }
    
    unsigned short int themeIndexOf(const std::string &path) {
        for (int i = themes.size() - 1; i >= 0; --i) {
            if (themes[i] == path) {
                return i;
            }
        }
        themes.emplace_back(path);
        return themes.size() - 1;
    }
};

static std::vector<std::string> icon_search_paths;
static auto *data = new OptionsData;

void traverse_dir(const char *path) {
    DIR *dir = opendir(path);
    if (dir == nullptr) {
        return;
    }
    
    std::string path_as_string(path);
    std::string theme;
    for (const auto &item: icon_search_paths) {
        if (path_as_string.find(item) == 0) {
            theme = path_as_string.substr(item.size());
            if (theme.empty()) {
                theme = path;
            } else {
                // Remove the first slash
                theme = theme.substr(1);
                // Remove everything after slash
                theme = theme.substr(0, theme.find('/'));
            }
            break;
        }
    }
    unsigned short int current_theme_index = data->themeIndexOf(theme);
    unsigned short int current_parent_index = data->parentIndexOf(path);
    
    struct dirent *entry;
    struct stat entryStat;
    while ((entry = readdir(dir)) != nullptr) {
        size_t name_len = strlen(entry->d_name);
        if (name_len > 5) {
            if (entry->d_name[name_len - 4] == '.') {
                size_t first = name_len - 3;
                size_t second = name_len - 2;
                size_t third = name_len - 1;
        
                int svgs = entry->d_name[first] == 's';
                int svgv = entry->d_name[second] == 'v';
                int svgg = entry->d_name[third] == 'g';
                int svgsvg = svgs + svgv + svgg;
        
                if (svgsvg == 3) {
                    Option option = {};
                    option.parentIndexAndExtension = (current_parent_index & 0x3FFF) | (0 << 14);
                    option.themeIndex = current_theme_index;
                    entry->d_name[name_len - 4] = '\0';
                    (&data->options[entry->d_name])->push_back(option);
                    continue;
                }
        
                int pngp = entry->d_name[first] == 'p';
                int pngv = entry->d_name[second] == 'n';
                int pngg = entry->d_name[third] == 'g';
                int pngpng = pngg + pngv + pngp;
        
                if (pngpng == 3) {
                    Option option = {};
                    option.parentIndexAndExtension = (current_parent_index & 0x3FFF) | (1 << 14);
                    option.themeIndex = current_theme_index;
                    entry->d_name[name_len - 4] = '\0';
                    (&data->options[entry->d_name])->push_back(option);
                    continue;
                }
            }
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
    
        char file[PATH_MAX];
        snprintf(file, PATH_MAX, "%s/%s", path, entry->d_name);
        if (stat(file, &entryStat) == -1)
            continue;
        if (S_ISDIR(entryStat.st_mode)) {
            traverse_dir(file);
        } else if (S_ISLNK(entryStat.st_mode)) {
            char link[PATH_MAX];
            ssize_t len = readlink(file, link, sizeof(link));
            if (len != -1) {
                link[len] = '\0';
                traverse_dir(link);
            }
        }
    }
    closedir(dir);
}


void generate_data() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto item: data->options)
        item.second.clear();
    data->options.clear();
    data->parentPaths.clear();
    data->themes.clear();
    
    const std::filesystem::directory_options searchOptions = (
            std::filesystem::directory_options::follow_directory_symlink |
            std::filesystem::directory_options::skip_permission_denied
    );
    struct stat st{};
    for (const auto &search_path: icon_search_paths) {
        if (stat(search_path.c_str(), &st) != 0)
            continue;
    
        traverse_dir(search_path.data());
    }
}

//
//
// IF WM_NAME OR NAME SET ON WINDOW, CHECK THROUGH ALL .DESKTOP FILES FOR MATCH, AND USE ICON SPECIFIED
// or
// _KDE_NET_WM_DESKTOP_FILE property set
// or
// _GTK_APPLICATION_ID property set
//
//

void save_data() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    const char *home_directory = getenv("HOME");
    std::string icon_cache_path(home_directory);
    icon_cache_path += "/.cache/winbar_icon_cache/icon.cache";
    
    std::string icon_cache_temp_path(home_directory);
    icon_cache_temp_path += "/.cache";
    std::ofstream cache_file;
    {
        if (mkdir(icon_cache_temp_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1)
            if (errno != EEXIST)
                return;
        icon_cache_temp_path += "/winbar_icon_cache";
        if (mkdir(icon_cache_temp_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1)
            if (errno != EEXIST)
                return;
        icon_cache_temp_path += "/icon.cache.tmp";
        
        cache_file.open(icon_cache_temp_path, std::ios_base::out | std::ios_base::binary);
        if (!cache_file.is_open())
            return;
    }
    
    // version (string)
    cache_file << std::to_string(cache_version) << '\0';
    
    // parent paths size (int)
    cache_file << std::to_string(data->parentPaths.size()) << '\0';
    for (const auto &item: data->parentPaths) {
        // parent paths (string)
        cache_file << item << '\0';
    }
    
    // themes paths size (int)
    cache_file << std::to_string(data->themes.size()) << '\0';
    for (const auto &item: data->themes) {
        // parent paths (string)
        cache_file << item << '\0';
    }

#define WRITE_NUM(num) \
    reinterpret_cast<const char *>(&num), sizeof(num) \

    // options size (int)
    cache_file << std::to_string(data->options.size()) << '\0';
    for (const auto &item: data->options) {
        // option name (string)
        cache_file << item.first << '\0';
        
        // option size (unsigned short int)
        unsigned short int optionsVectorSize = item.second.size();
        cache_file.write(WRITE_NUM(optionsVectorSize));
        
        for (const auto &option: item.second) {
            // (unsigned short int)
            cache_file.write(WRITE_NUM(option.parentIndexAndExtension));
//            cache_file << std::to_string(option.parentIndexAndExtension) << '\0';
            // (char)
            cache_file.write(WRITE_NUM(option.themeIndex));
//            cache_file << std::to_string(option.themeIndex) << '\0';
        }
    }
    
    cache_file.close();
    rename(icon_cache_temp_path.data(), icon_cache_path.data());
}

// TODO: there has to be a better way to do this
long load_file_into_memory(char const *path, char **buf) {
    FILE *fp;
    size_t fsz;
    long off_end;
    int rc;
    fp = fopen(path, "rb");
    if (nullptr == fp)
        return -1L;
    rc = fseek(fp, 0L, SEEK_END);
    if (0 != rc)
        return -1L;
    if (0 > (off_end = ftell(fp)))
        return -1L;
    fsz = (size_t) off_end;
    *buf = static_cast<char *>(malloc(fsz));
    if (nullptr == *buf)
        return -1L;
    rewind(fp);
    if (fsz != fread(*buf, 1, fsz, fp)) {
        free(*buf);
        return -1L;
    }
    if (EOF == fclose(fp)) {
        free(*buf);
        return -1L;
    }
    return (long) fsz;
}

void load_data() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto item: data->options)
        item.second.clear();
    data->options.clear();
    data->parentPaths.clear();
    data->themes.clear();
    
    // Load data from disk
    const char *home_directory = getenv("HOME");
    std::string icon_cache_path(home_directory);
    icon_cache_path += "/.cache/winbar_icon_cache/icon.cache";
    
    char *icon_cache_data = nullptr;
    
    struct stat cache_stat{};
    if (stat(icon_cache_path.c_str(), &cache_stat) == 0) { // exists
        // Quick check version
        FILE *fp;
        char buf[1024];
        if ((fp = fopen(icon_cache_path.data(), "rb"))) {
            fread(buf, 1, 10, fp);
            std::string versionString = std::string(buf, std::max(strlen(buf), (unsigned long) 0));
            int version = atoi(versionString.data());
            fclose(fp);
            if (version < cache_version) {
                generate_data();
                save_data();
            }
        }
        
        load_file_into_memory(icon_cache_path.data(), &icon_cache_data);
        if (!icon_cache_data)
            return;
        
        unsigned long index_into_file = 0;
        char buffer[NAME_MAX];
        long len;

#define READ_STRING(tess) \
        strcpy(buffer, icon_cache_data + index_into_file); \
        len = strlen(buffer); \
        index_into_file += len + 1; \
        std::string tess = std::string(buffer, std::max(len, (long) 0));

#define READ_NUM(tem) \
    *reinterpret_cast<tem *>((icon_cache_data + index_into_file)); \
    index_into_file += sizeof(tem)
        
        // Version
        READ_STRING(version_number)
        
        READ_STRING(amountOfParentsString)
        int amountOfParents = std::stoi(amountOfParentsString);
        for (int i = 0; i < amountOfParents; ++i) {
            READ_STRING(parentPath)
            data->parentPaths.push_back(std::move(parentPath));
        }
        
        READ_STRING(amountOfThemesString)
        int amountOfThemes = std::stoi(amountOfThemesString);
        for (int i = 0; i < amountOfThemes; ++i) {
            READ_STRING(theme)
            data->themes.push_back(std::move(theme));
        }
        
        READ_STRING(optionsSizeString)
        int optionsSize = std::stoi(optionsSizeString);
        for (int i = 0; i < optionsSize; ++i) {
            READ_STRING(name)
            std::vector<Option> *options = &data->options[name];
            
            auto optionSize = READ_NUM(unsigned short int);
            for (int j = 0; j < optionSize; ++j) {
                auto parentIndexAndExtension = READ_NUM(unsigned short int);
                auto theme = READ_NUM(unsigned char);
                
                Option opt = {};
                opt.parentIndexAndExtension = parentIndexAndExtension;
                opt.themeIndex = theme;
                
                options->push_back(opt);
            }
        }
    }
    
    free(icon_cache_data);
}


void check_if_cache_needs_update(App *, AppClient *, Timeout *timeout, void *);

void check_cache_file();

void set_icons_path_and_possibly_update(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (data == nullptr)
        data = new OptionsData();
    icon_search_paths.clear();
    
    // Setup search paths for icons
    //
    const char *h = getenv("HOME");
    std::string home;
    if (h) home = std::string(h);
    icon_search_paths.emplace_back("/usr/share/icons");
    icon_search_paths.emplace_back("/usr/local/share/icons");
    icon_search_paths.emplace_back(home + "/.icons");
    icon_search_paths.emplace_back(home + "/.local/share/icons");
    icon_search_paths.emplace_back("/var/lib/flatpak/exports/share/icons");
    icon_search_paths.emplace_back(home + "/.local/share/flatpak/exports/share/icons");
    const char *d = getenv("XDG_DATA_DIRS");
    std::string dirs;
    if (d) dirs = std::string(d);
    if (!dirs.empty()) {
        auto header_stream = std::stringstream{dirs};
        for (std::string dir; std::getline(header_stream, dir, ':');) {
            bool already_going_to_search = false;
            for (const auto &search_path: icon_search_paths) {
                if (dir == search_path) {
                    already_going_to_search = true;
                    break;
                }
            }
            if (!already_going_to_search)
                if (dir.find("icons") != std::string::npos)
                    icon_search_paths.emplace_back(dir);
        }
    }
    icon_search_paths.emplace_back("/usr/share/pixmaps");
    
    app_timeout_create(app, nullptr, 50000, check_if_cache_needs_update, nullptr);
    
    check_cache_file();
}

static long last_time_cached_checked = -1;

static std::string first_message;
static std::string second_message;
static std::string third_message;

void paint_warning(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_volume_background));
    cairo_fill(cr);
    
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 14 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    int width;
    int height;
    pango_layout_set_text(layout, first_message.data(), first_message.size());
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  ((int) ((container->real_bounds.x + container->real_bounds.w / 2 - width / 2))),
                  10 * config->dpi);
    pango_cairo_show_layout(cr, layout);
    
    
    layout = get_cached_pango_font(cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    int second_height;
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - (20 * config->dpi)) * PANGO_SCALE);
    pango_layout_set_text(layout, second_message.data(), second_message.size());
    pango_layout_get_pixel_size(layout, &width, &second_height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10 * config->dpi,
                  (10 + height + 10) * config->dpi);
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_text(layout, third_message.data(), third_message.size());
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - (20 * config->dpi)) * PANGO_SCALE);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10 * config->dpi,
                  (10 + height + 10 + second_height + 10) * config->dpi);
    pango_cairo_show_layout(cr, layout);
}

void check_cache_file() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (get_current_time_in_ms() - last_time_cached_checked < 5000) {
        // If it hasn't been five seconds since last time checked
        return;
    }
    
    // Check if cache file exists and that it is up-to date, and refresh cache, if it is not.
    const char *home_directory = getenv("HOME");
    std::string icon_cache_path(home_directory);
    icon_cache_path += "/.cache/winbar_icon_cache/icon.cache";
    
    struct stat cache_stat{};
    if (stat(icon_cache_path.c_str(), &cache_stat) == 0) { // exists
        bool cache_version_on_disk_acceptable = true;
        FILE *fp;
        char buf[1024];
        if ((fp = fopen(icon_cache_path.data(), "rb"))) {
            fread(buf, 1, 10, fp);
            std::string versionString = std::string(buf, std::max(strlen(buf), (unsigned long) 0));
            int version = atoi(versionString.data());
            if (version != cache_version)
                cache_version_on_disk_acceptable = false;
            fclose(fp);
        }
        
        if (!cache_version_on_disk_acceptable) {
            std::thread t([icon_cache_path]() -> void {
                generate_data();
                save_data();
            });
            App *temp_app = app_new();
            std::thread t2([&temp_app]() -> void {
                Settings settings;
                settings.w = 400 * config->dpi;
                settings.h = 200 * config->dpi;
                auto c = client_new(temp_app, settings, "winbar_temp_warning");
                c->root->when_paint = paint_warning;
                first_message = "Icon cache version file out of date";
                second_message = "We are re-caching all icons, so this may take a while. Subsequent launches won't do this, so they'll be faster.";
                third_message = "We'll start WinBar when we are done! (Feel free to close this window)";
                client_show(temp_app, c);
                app_main(temp_app);
                app_clean(temp_app);
            });
            if (t.joinable())
                t.join();
            if (t2.joinable()) {
                temp_app->running = false;
                t2.join();
            }
        } else {
            load_data();
        }
    } else {
        // If no cache file exists, we are forced to do it on the main thread (a.k.a. the first launch will be slow)
        std::thread t([icon_cache_path]() -> void {
            generate_data();
            save_data();
        });
        App *temp_app = app_new();
        std::thread t2([&temp_app]() -> void {
            Settings settings;
            settings.w = 400 * config->dpi;
            settings.h = 200 * config->dpi;
            auto c = client_new(temp_app, settings, "winbar_temp_warning");
            c->root->when_paint = paint_warning;
            first_message = "First time launching WinBar";
            second_message = "We are caching all icons so this may take a while. Subsequent launches will be faster.";
            third_message = "We'll start WinBar when we are done! (Feel free to close this window)";
            client_show(temp_app, c);
            app_main(temp_app);
            app_clean(temp_app);
        });
        if (t.joinable())
            t.join();
        if (t2.joinable()) {
            temp_app->running = false;
            t2.join();
        }
    }
    
    last_time_cached_checked = get_current_time_in_ms();
}

void search_icons(std::vector<IconTarget> &targets) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    check_cache_file();
    
    for (int i = 0; i < targets.size(); ++i) {
        auto target = targets[i];
        auto options = data->options[target.name.c_str()];
        
        std::vector<Candidate> candidates;
        for (const auto &item: options) {
            Candidate candidate;
            candidate.parent_path = data->parentPaths[getParentIndex(item.parentIndexAndExtension)];
            candidate.filename = target.name;
            candidate.theme = data->themes[item.themeIndex];
            candidate.extension = getExtension(item.parentIndexAndExtension);
            
            unsigned long startIndex = candidate.parent_path.find(candidate.theme);
            if (startIndex == std::string::npos)
                startIndex = 0;
            startIndex += candidate.theme.size() + 1;
            
            char buffer[64];
            int buffer_len = 0;
            bool found_at = false;
            int scale = 0;
            int size = 0;
            
            // Iterate through the characters in the path string
            for (int i = startIndex; i < candidate.parent_path.length(); i++) {
                if (scale != 0 && size != 0)
                    break;
                
                char c = candidate.parent_path[i];
                if (isdigit(c)) {
                    // Save the digit character to the buffer
                    buffer[buffer_len] = c;
                    buffer_len++;
                } else if (c == '@') {
                    if (buffer_len != 0) {
                        buffer[buffer_len] = '\0';
                        size = atoi(buffer);
                    }
                    found_at = true;
                    buffer_len = 0;
                } else if (c == '/' || c == 'x' || c == 'X' || i == candidate.parent_path.length() - 1) {
                    if (found_at && buffer_len != 0) {
                        // Convert the buffer to an integer and save it to the scale variable
                        buffer[buffer_len] = '\0';
                        scale = atoi(buffer);
                    } else if (buffer_len != 0) {
                        // Convert the buffer to an integer and save it to the size variable
                        buffer[buffer_len] = '\0';
                        size = atoi(buffer);
                    }
                    // Reset the buffer and the found_at flag
                    buffer_len = 0;
                    found_at = false;
                }
            }
            if (found_at && buffer_len != 0) {
                // Convert the buffer to an integer and save it to the scale variable
                buffer[buffer_len] = '\0';
                scale = atoi(buffer);
            } else if (buffer_len != 0) {
                // Convert the buffer to an integer and save it to the size variable
                buffer[buffer_len] = '\0';
                size = atoi(buffer);
            }
            candidate.size = size;
            candidate.scale = scale;
            candidates.push_back(candidate);
        }
        
        for (const auto &item: candidates)
            targets[i].candidates.push_back(item);
    }
}

static std::string get_current_theme_name() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::string gtk_settings_file_path(getenv("HOME"));
    gtk_settings_file_path += "/.config/gtk-3.0/settings.ini";
    
    INIReader gtk_settings(gtk_settings_file_path);
    if (gtk_settings.ParseError() != 0)
        return "hicolor";
    
    return gtk_settings.Get("Settings", "gtk-icon-theme-name", "hicolor");
}

static void c3ic_generate_sizes(int target_size, std::vector<int> &target_sizes) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    target_sizes.push_back(8);
    target_sizes.push_back(12);
    target_sizes.push_back(16);
    target_sizes.push_back(18);
    target_sizes.push_back(24);
    target_sizes.push_back(32);
    target_sizes.push_back(42);
    target_sizes.push_back(48);
    target_sizes.push_back(64);
    target_sizes.push_back(84);
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

void pick_best(std::vector<IconTarget> &targets, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto current_theme = get_current_theme_name();
    std::vector<int> strict_sizes;
    c3ic_generate_sizes(target_size, strict_sizes);
    for (int ss = 0; ss < targets.size(); ss++) {
        if (!targets[ss].name.empty() && targets[ss].name[0] == '/') {
            // If the target is just a full path, then just return the full path
            targets[ss].best_full_path = targets[ss].name;
        } else {
            for (auto item: targets[ss].candidates)
                item.is_part_of_current_theme = current_theme == item.theme;
            // Sort vector based on quality and size, and current theme
            // Set best_full_path equal to best top option
            std::sort(targets[ss].candidates.begin(), targets[ss].candidates.end(),
                      [current_theme](Candidate lhs, Candidate rhs) {
                          if (lhs.is_part_of_current_theme == rhs.is_part_of_current_theme) {
                              if (lhs.size_index == rhs.size_index) {
                                  if (lhs.extension == rhs.extension) {
                                      return lhs.scale < rhs.scale;
                                  } else {
                                      return lhs.extension < rhs.extension;
                                  }
                              } else {
                                  return lhs.size_index < rhs.size_index;
                              }
                          }
                          return lhs.is_part_of_current_theme > rhs.is_part_of_current_theme;
                      });
    
            if (!targets[ss].candidates.empty())
                targets[ss].best_full_path = targets[ss].candidates[0].full_path();
        }
    }
}

void check_if_cache_needs_update(App *app, AppClient *, Timeout *timeout, void *) {
    std::thread t([app]() -> void {
#ifdef TRACY_ENABLE
        ZoneScopedN("icon directory timeout");
#endif
        // TODO: this is crashin in some cases
        const char *home_directory = getenv("HOME");
        std::string icon_cache_path(home_directory);
        icon_cache_path += "/.cache/winbar_icon_cache/icon.cache";
        
        struct stat cache_stat{};
        if (stat(icon_cache_path.c_str(), &cache_stat) == 0) { // exists
            bool found_newer_folder_than_cache_file = false;
            
            const std::filesystem::directory_options options = (
                    std::filesystem::directory_options::follow_directory_symlink |
                    std::filesystem::directory_options::skip_permission_denied
            );
            struct stat search_stat{};
            for (const auto &search_path: icon_search_paths) {
                // Check if the search path exists
                if (stat(search_path.c_str(), &search_stat) != 0)
                    continue;
                
                if (search_stat.st_mtim.tv_sec > cache_stat.st_mtim.tv_sec) {
                    found_newer_folder_than_cache_file = true;
                    break;
                }
                
                for (auto i = std::filesystem::recursive_directory_iterator(search_path,
                                                                            std::filesystem::directory_options(
                                                                                    options));
                     i != std::filesystem::recursive_directory_iterator();
                     ++i) {
                    if (i.depth() == 0 && i->is_directory() && i->exists()) {
                        if (stat(i->path().string().data(), &search_stat) != 0)
                            continue;
    
                        if (search_stat.st_mtim.tv_sec > cache_stat.st_mtim.tv_sec) {
                            found_newer_folder_than_cache_file = true;
                            break;
                        }
                    }
                }
                if (found_newer_folder_than_cache_file)
                    break;
            }
            
            if (found_newer_folder_than_cache_file) {
                std::lock_guard lock(app->running_mutex);
                generate_data();
                save_data();
            }
        }
    });
    t.detach();
}

void unload_icons() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (data != nullptr) {
        data->parentPaths.clear();
        data->parentPaths.shrink_to_fit();
        data->themes.clear();
        data->themes.shrink_to_fit();
        for (auto item: data->options)
            item.second.clear();
        data->options.clear();
        delete data;
        data = nullptr;
    }
    
    icon_search_paths.clear();
    icon_search_paths.shrink_to_fit();
    icon_search_paths = std::vector<std::string>();
}

std::string
c3ic_fix_desktop_file_icon(const std::string &given_name,
                           const std::string &given_wm_class,
                           const std::string &given_path,
                           const std::string &given_icon) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
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

bool has_options(const std::string &name) {
    return !data->options[name.c_str()].empty();
}
