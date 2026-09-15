#ifndef MENU_H
#define MENU_H

int menu_run(void);
void menu_help(void);
void menu_about(void);
const char* menu_get_id(int idx);
const char* menu_get_dir(int idx);
const char* menu_get_name(int idx);

#endif