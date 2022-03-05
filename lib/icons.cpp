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

#include "../tracy/Tracy.hpp"

#endif

struct IconData {
    std::string theme;
    
    std::string name;
    
    std::string full_path;
    
    uint32_t size = 0; // 0 == unknown, 1 == scalable
    
    uint32_t extension = 0; // 0 == unknown, 1 == png, 2 == svg, 3 == xpm
    
    uint32_t scale = 1; // 1 == default
    
    // Used for sorting
    int size_index = 10;
    bool is_part_of_current_theme = false;
};

static std::vector<std::string> icon_search_paths;

static uint32_t cache_version = 1;
static uint32_t cache_flags = 0;

char *icon_cache_data = nullptr;
long icon_cache_data_length = 0;

void icon_directory_timeout(App *, AppClient *, Timeout *, void *);

void check_icon_cache();

void load_icons(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
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
    
    app_timeout_create(app, nullptr, 50000, icon_directory_timeout, nullptr);
    
    check_icon_cache();
}

// TODO: there has to be a better way to do this
long load_file_into_memory(char const *path, char **buf) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
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

void update_icon_cache() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (icon_cache_data != nullptr) {
        free(icon_cache_data);
        icon_cache_data = nullptr;
        icon_cache_data_length = 0;
    }
    
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
        
        cache_file.open(icon_cache_temp_path);
        if (!cache_file.is_open())
            return;
    }
    
    const std::filesystem::directory_options options = (
            std::filesystem::directory_options::follow_directory_symlink |
            std::filesystem::directory_options::skip_permission_denied
    );
    
    cache_file << std::to_string(cache_version) << '\0' << '\n';
    cache_file << std::to_string(cache_flags) << '\0' << '\n';
    bool first = true;
    
    struct stat st{};
    for (auto search_path: icon_search_paths) {
        // Check if the search path exists
        if (stat(search_path.c_str(), &st) != 0)
            continue;
        
        int parent_directories_to_skip = 0;
        for (auto c: search_path)
            if (c == '/')
                parent_directories_to_skip++;
        parent_directories_to_skip++;
        
        int previous_depth = -1;
        std::string data_full_path;
        std::string data_theme;
        int data_size = 0;
        int data_scale = 1;
        
        for (auto i = std::filesystem::recursive_directory_iterator(search_path,
                                                                    std::filesystem::directory_options(options));
             i != std::filesystem::recursive_directory_iterator();
             ++i) {
            int depth = i.depth();
            auto path = i->path();
            
            if (previous_depth != depth) {
                previous_depth = depth;
                data_full_path.clear();
                data_theme.clear();
                data_size = 0;
                data_scale = 1;
                
                auto p = path;
                if (is_regular_file(p)) {
                    if (exists(p.parent_path())) {
                        p = p.parent_path();
                    }
                }
                
                data_full_path = p.string();
                
                int skip_variable = 0;
                for (const auto &item: p) {
                    if (skip_variable++ < parent_directories_to_skip)
                        continue; // We don't need to check the parent paths which are part of the search path
                    
                    if ((skip_variable == parent_directories_to_skip + 1) && data_theme.empty()) {
                        data_theme = item.string();
                    }
                    
                    // Update data if there is something to update
                    std::regex single_digit("[0-9]*");
                    std::smatch match;
                    const std::string &const_name = item;
                    if (std::regex_match(const_name, match, single_digit)) {
                        if (std::all_of(const_name.begin(), const_name.end(), ::isdigit)) {
                            int size = std::stoi(match[0].str());
                            if (size % 2 == 0)
                                data_size = size;
                        }
                    }
                    std::regex scale_regex("@[0-9]*");
                    if (std::regex_search(const_name.begin(), const_name.end(), match, scale_regex)) {
                        auto sc = match[0].str();
                        if (!sc.empty()) {
                            sc.erase(0, 1);
                            
                            if (!sc.empty() && std::all_of(sc.begin(), sc.end(), ::isdigit)) {
                                data_scale = std::stoi(sc);
                            }
                        }
                    }
                    std::regex digit_x_digit("[0-9]*(?=(x|X)[0-9]*)");
                    if (std::regex_search(const_name.begin(), const_name.end(), match, digit_x_digit)) {
                        auto si = match[0].str();
                        if (!si.empty()) {
                            if (!si.empty() && std::all_of(si.begin(), si.end(), ::isdigit)) {
                                int size = std::stoi(si);
                                if (size % 2 == 0)
                                    data_size = size;
                            }
                        }
                    }
                }
                
                if (!first)
                    cache_file << '\n';
                first = false;
                
                cache_file << data_full_path << '\0';
                cache_file << std::to_string(data_size) << '\0';
                cache_file << std::to_string(data_scale) << '\0';
                if (data_theme.empty()) {
                    cache_file << '\0';
                } else {
                    cache_file << data_theme << '\0';
                }
            }
            if (is_regular_file(path)) {
                cache_file << path.filename().string() << '\0';
            }
        }
    }
    cache_file.close();
    rename(icon_cache_temp_path.data(), icon_cache_path.data());
}

static long last_time_cached_checked = -1;

static std::string first_message;
static std::string second_message;
static std::string third_message;

void paint_warning(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_volume_background));
    cairo_fill(cr);
    
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 14, PangoWeight::PANGO_WEIGHT_BOLD);
    int width;
    int height;
    pango_layout_set_text(layout, first_message.data(), first_message.size());
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  10);
    pango_cairo_show_layout(cr, layout);
    
    
    layout = get_cached_pango_font(cr, config->font, 12, PangoWeight::PANGO_WEIGHT_NORMAL);
    int second_height;
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - 20) * PANGO_SCALE);
    pango_layout_set_text(layout, second_message.data(), second_message.size());
    pango_layout_get_pixel_size(layout, &width, &second_height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10,
                  10 + height + 10);
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_text(layout, third_message.data(), third_message.size());
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - 20) * PANGO_SCALE);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10,
                  10 + height + 10 + second_height + 5);
    pango_cairo_show_layout(cr, layout);
}

void check_icon_cache() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (icon_cache_data != nullptr) {
        if (get_current_time_in_ms() - last_time_cached_checked < 5000) {
            // If it hasn't been five seconds since last time checked
            return;
        }
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
            std::string version = std::string(buf, std::max(strlen(buf), (unsigned long) 0));
            if (!version.empty()) {
                auto cache_version_on_disk = std::stoi(version);
                if (cache_version_on_disk != cache_version) {
                    cache_version_on_disk_acceptable = false;
                }
            }
            fclose(fp);
        }
        
        if (!cache_version_on_disk_acceptable) {
            if (icon_cache_data != nullptr) {
                free(icon_cache_data);
                icon_cache_data_length = 0;
            }
            std::thread t([icon_cache_path]() -> void {
                update_icon_cache();
                icon_cache_data_length = load_file_into_memory(icon_cache_path.data(), &icon_cache_data);
            });
            App *temp_app = app_new();
            std::thread t2([&temp_app]() -> void {
                Settings settings;
                settings.w = 400;
                settings.h = 200;
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
            if (icon_cache_data == nullptr) {
                icon_cache_data_length = load_file_into_memory(icon_cache_path.data(), &icon_cache_data);
            }
        }
    } else {
        // If no cache file exists, we are forced to do it on the main thread (a.k.a. the first launch will be slow)
        std::thread t([icon_cache_path]() -> void {
            update_icon_cache();
            icon_cache_data_length = load_file_into_memory(icon_cache_path.data(), &icon_cache_data);
        });
        App *temp_app = app_new();
        std::thread t2([&temp_app]() -> void {
            Settings settings;
            settings.w = 400;
            settings.h = 200;
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

int has_extension(const char *szFileName, const char *szExt) {
    int i = 0;
    if ('\0' != *szFileName) {
        for (i = strlen(szFileName) - 1; i > 0; --i) {
            if ('.' == szFileName[i])
                break;
        }
    }
    return (0 == strcmp(szFileName + i, szExt));
}

void search_icons(std::vector<IconTarget> &targets) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    check_icon_cache();
    if (!icon_cache_data)
        return;
    
    // Go through cache and match name, return index in results
    unsigned long index_into_file = 0;
#define NOT_DONE index_into_file < icon_cache_data_length
    
    // Skip first two lines
    while (NOT_DONE && icon_cache_data[index_into_file++] != '\n'); // Version
    while (NOT_DONE && icon_cache_data[index_into_file++] != '\n'); // Flags
    
    char buffer[NAME_MAX];
    while (NOT_DONE) {
        unsigned long line_index = index_into_file;
        
        // Copy icon name to buffer
        strcpy(buffer, icon_cache_data + index_into_file);
        long len = strlen(buffer);
        index_into_file += len + 1;
        std::string pre_path = std::string(buffer, std::max(len, (long) 0));
        
        strcpy(buffer, icon_cache_data + index_into_file);
        len = strlen(buffer);
        index_into_file += len + 1;
        std::string size = std::string(buffer, std::max(len, (long) 0));
        
        strcpy(buffer, icon_cache_data + index_into_file);
        len = strlen(buffer);
        index_into_file += len + 1;
        std::string scale = std::string(buffer, std::max(len, (long) 0));
        
        strcpy(buffer, icon_cache_data + index_into_file);
        len = strlen(buffer);
        index_into_file += len + 1;
        std::string theme = std::string(buffer, std::max(len, (long) 0));
        
        while (NOT_DONE && icon_cache_data[index_into_file] != '\n') {
            strcpy(buffer, icon_cache_data + index_into_file);
            len = strlen(buffer);
            std::string name_without_extension = std::string(buffer, std::max(len - 4, (long) 0));
            if (!name_without_extension.empty()) {
                int extension = 0;
                if (strncmp(buffer + len - 4, ".svg", 4) == 0) {
                    extension = 2;
                } else if (strncmp(buffer + len - 4, ".png", 4) == 0) {
                    extension = 1;
                } else if (strncmp(buffer + len - 4, ".xpm", 4) == 0) {
                    extension = 3;
                }
                
                for (int i = 0; i < targets.size(); i++) {
                    if (strcmp(name_without_extension.data(), targets[i].name.data()) == 0) {
                        targets[i].indexes_of_results.emplace_back(
                                pre_path,
                                std::stoi(size),
                                std::stoi(scale),
                                theme,
                                std::string(buffer, len),
                                extension
                        );
                    }
                }
            }
            
            index_into_file += len + 1;
        }
        
        // Skip until the next icon option
        while (NOT_DONE && icon_cache_data[index_into_file++] != '\n');
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
            std::vector<IconData *> possible_icons;
            for (const auto &index: targets[ss].indexes_of_results) {
                auto data = new IconData;
                data->full_path = index.pre_path + "/" + index.name;
                data->extension = index.extension;
                data->theme = index.theme;
                if (data->theme == current_theme) {
                    data->is_part_of_current_theme = true;
                }
                data->size = index.size;
                for (int i = 0; i < strict_sizes.size(); i++)
                    if (strict_sizes[i] == data->size)
                        data->size_index = i;
                data->scale = index.scale;
                possible_icons.push_back(data);
            }
            
            // Sort vector based on quality and size, and current theme
            // Set best_full_path equal to best top option
            std::sort(possible_icons.begin(), possible_icons.end(),
                      [current_theme](IconData *lhs, IconData *rhs) {
                          if (lhs->is_part_of_current_theme == rhs->is_part_of_current_theme) {
                              if (lhs->size_index == rhs->size_index) {
                                  if (lhs->extension == rhs->extension) {
                                      return lhs->scale < rhs->scale;
                                  } else {
                                      return lhs->extension < rhs->extension;
                                  }
                              } else {
                                  return lhs->size_index < rhs->size_index;
                              }
                          }
                          return lhs->is_part_of_current_theme > rhs->is_part_of_current_theme;
                      });
            
            if (!possible_icons.empty())
                targets[ss].best_full_path = possible_icons[0]->full_path;
            
            for (auto p: possible_icons)
                delete p;
            possible_icons.clear();
            possible_icons.shrink_to_fit();
        }
    }
}

void icon_directory_timeout(App *, AppClient *, Timeout *timeout, void *) {
    std::thread t([]() -> void {
#ifdef TRACY_ENABLE
        ZoneScopedN("icon directory timeout");
#endif
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
            
            if (found_newer_folder_than_cache_file)
                update_icon_cache();
        }
    });
    t.detach();
}

void unload_icons() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (icon_cache_data)
        free(icon_cache_data);
    icon_cache_data = nullptr;
    icon_cache_data_length = 0;
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
