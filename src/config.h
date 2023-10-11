#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include "utility.h"
#include <string>
#include <vector>

struct Config {
    bool found_config = false;
    
    int config_version = 8;
    
    int taskbar_height = 40;
    
    int starting_tab_index = 0;
    
    bool dpi_auto = true;
    
    float dpi = 1;
    
    std::string font = "Segoe UI Variable Mod";

    bool pinned_icon_shortcut = true;

    bool battery_life_extender = false;
    
    std::string open_pinned_icon_editor = "WHEN_ANY_FIELD_EMPTY";
    
    std::string volume_command;
    std::string wifi_command;
    std::string vpn_command;
    std::string date_command;
    std::string battery_command;
    std::string systray_command;
    
    bool date_single_line = false;
    
    ArgbColor color_taskbar_background = ArgbColor("#dd101010");
    ArgbColor color_taskbar_button_icons = ArgbColor("#ffffffff");
    ArgbColor color_taskbar_button_default = ArgbColor("#00ffffff");
    ArgbColor color_taskbar_button_hovered = ArgbColor("#23ffffff");
    ArgbColor color_taskbar_button_pressed = ArgbColor("#35ffffff");
    ArgbColor color_taskbar_windows_button_default_icon = ArgbColor("#ffffffff");
    ArgbColor color_taskbar_windows_button_hovered_icon = ArgbColor("#ff429ce3");
    ArgbColor color_taskbar_windows_button_pressed_icon = ArgbColor("#ff0078d7");
    ArgbColor color_taskbar_search_bar_default_background = ArgbColor("#fff3f3f3");
    ArgbColor color_taskbar_search_bar_hovered_background = ArgbColor("#ffffffff");
    ArgbColor color_taskbar_search_bar_pressed_background = ArgbColor("#ffffffff");
    ArgbColor color_taskbar_search_bar_default_text = ArgbColor("#ff2b2b2b");
    ArgbColor color_taskbar_search_bar_hovered_text = ArgbColor("#ff2d2d2d");
    ArgbColor color_taskbar_search_bar_pressed_text = ArgbColor("#ff020202");
    ArgbColor color_taskbar_search_bar_default_icon = ArgbColor("#ff020202");
    ArgbColor color_taskbar_search_bar_hovered_icon = ArgbColor("#ff020202");
    ArgbColor color_taskbar_search_bar_pressed_icon = ArgbColor("#ff020202");
    ArgbColor color_taskbar_search_bar_default_border = ArgbColor("#ffb4b4b4");
    ArgbColor color_taskbar_search_bar_hovered_border = ArgbColor("#ffb4b4b4");
    ArgbColor color_taskbar_search_bar_pressed_border = ArgbColor("#ff0078d7");
    ArgbColor color_taskbar_date_time_text = ArgbColor("#ffffffff");
    ArgbColor color_taskbar_application_icons_background = ArgbColor("#ffffffff");
    ArgbColor color_taskbar_application_icons_accent = ArgbColor("#ff76b9ed");
    ArgbColor color_taskbar_minimize_line = ArgbColor("#ff222222");
    ArgbColor color_taskbar_attention_accent = ArgbColor("#fffc8803");
    ArgbColor color_taskbar_attention_background = ArgbColor("#fffc8803");
    
    ArgbColor color_systray_background = ArgbColor("#f3282828");
    
    ArgbColor color_battery_background = ArgbColor("#f31f1f1f");
    ArgbColor color_battery_text = ArgbColor("#ffffffff");
    ArgbColor color_battery_icons = ArgbColor("#ffffffff");
    ArgbColor color_battery_slider_background = ArgbColor("#ff797979");
    ArgbColor color_battery_slider_foreground = ArgbColor("#ff0178d6");
    ArgbColor color_battery_slider_active = ArgbColor("#ffffffff");
    
    ArgbColor color_wifi_background = ArgbColor("#f31f1f1f");
    ArgbColor color_wifi_icons = ArgbColor("#ffffffff");
    ArgbColor color_wifi_default_button = ArgbColor("#00ffffff");
    ArgbColor color_wifi_hovered_button = ArgbColor("#22ffffff");
    ArgbColor color_wifi_pressed_button = ArgbColor("#44ffffff");
    ArgbColor color_wifi_text_title = ArgbColor("#ffffffff");
    ArgbColor color_wifi_text_title_info = ArgbColor("#ffadadad");
    ArgbColor color_wifi_text_settings_default_title = ArgbColor("#ffa5d6fd");
    ArgbColor color_wifi_text_settings_hovered_title = ArgbColor("#ffa4a4a4");
    ArgbColor color_wifi_text_settings_pressed_title = ArgbColor("#ff787878");
    ArgbColor color_wifi_text_settings_title_info = ArgbColor("#ffa4a4a4");
    
    ArgbColor color_date_background = ArgbColor("#f31f1f1f");
    ArgbColor color_date_seperator = ArgbColor("#ff4b4b4b");
    ArgbColor color_date_text = ArgbColor("#ffffffff");
    ArgbColor color_date_text_title = ArgbColor("#ffffffff");
    ArgbColor color_date_text_title_period = ArgbColor("#ffa5a5a5");
    ArgbColor color_date_text_title_info = ArgbColor("#ffa5dafd");
    ArgbColor color_date_text_month_year = ArgbColor("#ffdedede");
    ArgbColor color_date_text_week_day = ArgbColor("#ffffffff");
    ArgbColor color_date_text_current_month = ArgbColor("#ffffffff");
    ArgbColor color_date_text_not_current_month = ArgbColor("#ff808080");
    ArgbColor color_date_cal_background = ArgbColor("#ff006fd8");
    ArgbColor color_date_cal_foreground = ArgbColor("#ff000000");
    ArgbColor color_date_cal_border = ArgbColor("#ff797979");
    ArgbColor color_date_weekday_monthday = ArgbColor("#ffffffff");
    ArgbColor color_date_default_arrow = ArgbColor("#ffdfdfdf");
    ArgbColor color_date_hovered_arrow = ArgbColor("#ffefefef");
    ArgbColor color_date_pressed_arrow = ArgbColor("#ffffffff");
    ArgbColor color_date_text_default_button = ArgbColor("#ffa5d6fd");
    ArgbColor color_date_text_hovered_button = ArgbColor("#ffa4a4a4");
    ArgbColor color_date_text_pressed_button = ArgbColor("#ff787878");
    ArgbColor color_date_cursor = ArgbColor("#ffffffff");
    ArgbColor color_date_text_prompt = ArgbColor("#ffcccccc");
    
    ArgbColor color_volume_background = ArgbColor("#f31f1f1f");
    ArgbColor color_volume_text = ArgbColor("#ffffffff");
    ArgbColor color_volume_default_icon = ArgbColor("#ffd2d2d2");
    ArgbColor color_volume_hovered_icon = ArgbColor("#ffe8e8e8");
    ArgbColor color_volume_pressed_icon = ArgbColor("#ffffffff");
    ArgbColor color_volume_slider_background = ArgbColor("#ff797979");
    ArgbColor color_volume_slider_foreground = ArgbColor("#ff0178d6");
    ArgbColor color_volume_slider_active = ArgbColor("#ffffffff");
    
    ArgbColor color_apps_background = ArgbColor("#f31f1f1f");
    ArgbColor color_apps_text = ArgbColor("#ffffffff");
    ArgbColor color_apps_text_inactive = ArgbColor("#ff505050");
    ArgbColor color_apps_icons = ArgbColor("#ffffffff");
    ArgbColor color_apps_default_item = ArgbColor("#00ffffff");
    ArgbColor color_apps_hovered_item = ArgbColor("#22ffffff");
    ArgbColor color_apps_pressed_item = ArgbColor("#44ffffff");
    ArgbColor color_apps_item_icon_background = ArgbColor("#ff3380cc");
    ArgbColor color_apps_scrollbar_gutter = ArgbColor("#ff353535");
    ArgbColor color_apps_scrollbar_default_thumb = ArgbColor("#ff5d5d5d");
    ArgbColor color_apps_scrollbar_hovered_thumb = ArgbColor("#ff868686");
    ArgbColor color_apps_scrollbar_pressed_thumb = ArgbColor("#ffaeaeae");
    ArgbColor color_apps_scrollbar_default_button = ArgbColor("#ff353535");
    ArgbColor color_apps_scrollbar_hovered_button = ArgbColor("#ff494949");
    ArgbColor color_apps_scrollbar_pressed_button = ArgbColor("#ffaeaeae");
    ArgbColor color_apps_scrollbar_default_button_icon = ArgbColor("#ffffffff");
    ArgbColor color_apps_scrollbar_hovered_button_icon = ArgbColor("#ffffffff");
    ArgbColor color_apps_scrollbar_pressed_button_icon = ArgbColor("#ff545454");
    
    ArgbColor color_pin_menu_background = ArgbColor("#f31f1f1f");
    ArgbColor color_pin_menu_hovered_item = ArgbColor("#22ffffff");
    ArgbColor color_pin_menu_pressed_item = ArgbColor("#44ffffff");
    ArgbColor color_pin_menu_text = ArgbColor("#ffffffff");
    ArgbColor color_pin_menu_icons = ArgbColor("#ffffffff");
    
    ArgbColor color_windows_selector_default_background = ArgbColor("#f3282828");
    ArgbColor color_windows_selector_hovered_background = ArgbColor("#f33d3d3d");
    ArgbColor color_windows_selector_pressed_background = ArgbColor("#f3535353");
    ArgbColor color_windows_selector_close_icon = ArgbColor("#ffffffff");
    ArgbColor color_windows_selector_close_icon_hovered = ArgbColor("#ffffffff");
    ArgbColor color_windows_selector_close_icon_pressed = ArgbColor("#ffffffff");
    ArgbColor color_windows_selector_text = ArgbColor("#ffffffff");
    ArgbColor color_windows_selector_close_icon_hovered_background = ArgbColor("#ffc61a28");
    ArgbColor color_windows_selector_close_icon_pressed_background = ArgbColor("#ffe81123");
    ArgbColor color_windows_selector_attention_background = ArgbColor("#fffc8803");
    
    ArgbColor color_search_tab_bar_background = ArgbColor("#f31f1f1f");
    ArgbColor color_search_accent = ArgbColor("#ff0078d7");
    ArgbColor color_search_tab_bar_default_text = ArgbColor("#ffbfbfbf");
    ArgbColor color_search_tab_bar_hovered_text = ArgbColor("#ffd9d9d9");
    ArgbColor color_search_tab_bar_pressed_text = ArgbColor("#ffa6a6a6");
    ArgbColor color_search_tab_bar_active_text = ArgbColor("#ffffffff");
    ArgbColor color_search_empty_tab_content_background = ArgbColor("#f32a2a2a");
    ArgbColor color_search_empty_tab_content_icon = ArgbColor("#ff6b6b6b");
    ArgbColor color_search_empty_tab_content_text = ArgbColor("#ffaaaaaa");
    ArgbColor color_search_content_left_background = ArgbColor("#fff0f0f0");
    ArgbColor color_search_content_right_background = ArgbColor("#fff5f5f5");
    ArgbColor color_search_content_right_foreground = ArgbColor("#ffffffff");
    ArgbColor color_search_content_right_splitter = ArgbColor("#fff2f2f2");
    ArgbColor color_search_content_text_primary = ArgbColor("#ff010101");
    ArgbColor color_search_content_text_secondary = ArgbColor("#ff606060");
    ArgbColor color_search_content_right_button_default = ArgbColor("#00000000");
    ArgbColor color_search_content_right_button_hovered = ArgbColor("#26000000");
    ArgbColor color_search_content_right_button_pressed = ArgbColor("#51000000");
    ArgbColor color_search_content_left_button_splitter = ArgbColor("#ffffffff");
    ArgbColor color_search_content_left_button_default = ArgbColor("#00000000");
    ArgbColor color_search_content_left_button_hovered = ArgbColor("#24000000");
    ArgbColor color_search_content_left_button_pressed = ArgbColor("#48000000");
    ArgbColor color_search_content_left_button_active = ArgbColor("#ffa8cce9");
    ArgbColor color_search_content_left_set_active_button_default = ArgbColor("#00000000");
    ArgbColor color_search_content_left_set_active_button_hovered = ArgbColor("#22000000");
    ArgbColor color_search_content_left_set_active_button_pressed = ArgbColor("#19000000");
    ArgbColor color_search_content_left_set_active_button_active = ArgbColor("#ff97b8d2");
    ArgbColor color_search_content_left_set_active_button_icon_default = ArgbColor("#ff606060");
    ArgbColor color_search_content_left_set_active_button_icon_pressed = ArgbColor("#ffffffff");
    
    ArgbColor color_pinned_icon_editor_background = ArgbColor("#ffffffff");
    ArgbColor color_pinned_icon_editor_field_default_text = ArgbColor("#ff000000");
    ArgbColor color_pinned_icon_editor_field_hovered_text = ArgbColor("#ff2d2d2d");
    ArgbColor color_pinned_icon_editor_field_pressed_text = ArgbColor("#ff020202");
    ArgbColor color_pinned_icon_editor_field_default_border = ArgbColor("#ffb4b4b4");
    ArgbColor color_pinned_icon_editor_field_hovered_border = ArgbColor("#ff646464");
    ArgbColor color_pinned_icon_editor_field_pressed_border = ArgbColor("#ff0078d7");
    ArgbColor color_pinned_icon_editor_cursor = ArgbColor("#ff000000");
    ArgbColor color_pinned_icon_editor_button_default = ArgbColor("#ffcccccc");
    ArgbColor color_pinned_icon_editor_button_text_default = ArgbColor("#ff000000");
    
    ArgbColor color_notification_content_background = ArgbColor("#ff1f1f1f");
    ArgbColor color_notification_title_background = ArgbColor("#ff191919");
    ArgbColor color_notification_content_text = ArgbColor("#ffffffff");
    ArgbColor color_notification_title_text = ArgbColor("#ffffffff");
    ArgbColor color_notification_button_default = ArgbColor("#ff545454");
    ArgbColor color_notification_button_hovered = ArgbColor("#ff616161");
    ArgbColor color_notification_button_pressed = ArgbColor("#ff474747");
    ArgbColor color_notification_button_text_default = ArgbColor("#ffffffff");
    ArgbColor color_notification_button_text_hovered = ArgbColor("#ffffffff");
    ArgbColor color_notification_button_text_pressed = ArgbColor("#ffffffff");
    ArgbColor color_notification_button_send_to_action_center_default = ArgbColor("#ff9c9c9c");
    ArgbColor color_notification_button_send_to_action_center_hovered = ArgbColor("#ffcccccc");
    ArgbColor color_notification_button_send_to_action_center_pressed = ArgbColor("#ff888888");
    
    ArgbColor color_action_center_background = ArgbColor("#ff1f1f1f");
    ArgbColor color_action_center_history_text = ArgbColor("#ffa5d6fd");
    ArgbColor color_action_center_no_new_text = ArgbColor("#ffffffff");
    ArgbColor color_action_center_notification_content_background = ArgbColor("#ff282828");
    ArgbColor color_action_center_notification_title_background = ArgbColor("#ff1f1f1f");
    ArgbColor color_action_center_notification_content_text = ArgbColor("#ffffffff");
    ArgbColor color_action_center_notification_title_text = ArgbColor("#ffffffff");
    ArgbColor color_action_center_notification_button_default = ArgbColor("#ff545454");
    ArgbColor color_action_center_notification_button_hovered = ArgbColor("#ff616161");
    ArgbColor color_action_center_notification_button_pressed = ArgbColor("#ff474747");
    ArgbColor color_action_center_notification_button_text_default = ArgbColor("#ffffffff");
    ArgbColor color_action_center_notification_button_text_hovered = ArgbColor("#ffffffff");
    ArgbColor color_action_center_notification_button_text_pressed = ArgbColor("#ffffffff");
};

extern Config *config;

void config_load();

#endif