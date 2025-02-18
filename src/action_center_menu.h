//
// Created by jmanc3 on 6/2/21.
//

#ifndef WINBAR_ACTION_CENTER_MENU_H
#define WINBAR_ACTION_CENTER_MENU_H

struct App;

void start_action_center(App *app);

bool is_blacklisted(std::string title, std::string body, std::string subtitle);

#endif //WINBAR_ACTION_CENTER_MENU_H
