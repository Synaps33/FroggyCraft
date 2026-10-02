#include "settings.h"
#include "theme.h"
#include "gfx_theme.h"
#include "font.h"
#include "frogos.h"
#include "text_editor.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef SF2000
#include "../../debug.h"
#else
// For non-SF2000 builds, use printf as fallback
#define xlog printf
#endif

static SettingsOption settings[MAX_SETTINGS];
static int settings_count = 0;
static int settings_active = 0;
static int settings_selected = 0;
static int settings_scroll_offset = 0;
static int settings_saving = 0;  // Flag to indicate save in progress

// Track current config file being edited
static char current_config_path[512] = "";

// Get config directory
static const char* get_config_directory(void) {
	return "/mnt/sda1/configs";
}

// Forward declarations
static int settings_load_file(const char *config_path, int load_frogui_defaults);
static void apply_theme_from_settings(void);
static void apply_gfx_theme_from_settings(void);
static void apply_font_from_settings(void);

void settings_init(void) {
    settings_count = 0;
    settings_active = 0;
    settings_selected = 0;
    settings_scroll_offset = 0;
}

// Parse a line like: ### [option_name] :[current] :[val1|val2|val3]
static int parse_option_line(const char *line, SettingsOption *option) {
    if (strncmp(line, "### [", 5) != 0) return 0;

    // Find option name
    const char *name_start = line + 5;
    const char *name_end = strchr(name_start, ']');
    if (!name_end) return 0;

    int name_len = name_end - name_start;
    if (name_len >= MAX_OPTION_NAME_LEN) return 0;

    strncpy(option->name, name_start, name_len);
    option->name[name_len] = '\0';

    // Find current value (between first : and next ])
    const char *current_start = strchr(name_end, ':');
    if (!current_start) return 0;
    current_start++; // Skip ':'

    // Skip whitespace and '[' if present
    while (*current_start == ' ' || *current_start == '\t' || *current_start == '[') current_start++;

    const char *current_end = strchr(current_start, ']');
    if (!current_end) return 0;

    // Trim trailing whitespace from current value
    while (current_end > current_start && (*(current_end - 1) == ' ' || *(current_end - 1) == '\t')) {
        current_end--;
    }

    int current_len = current_end - current_start;
    if (current_len >= MAX_OPTION_VALUE_LEN || current_len <= 0) return 0;

    strncpy(option->current_value, current_start, current_len);
    option->current_value[current_len] = '\0';

    // Find possible values (between [ and last ])
    const char *values_start = strchr(current_end, '[');
    if (!values_start) return 0;
    values_start++; // Skip '['

    // Find the LAST ']' on the line (not the first one)
    const char *values_end = strrchr(values_start, ']');
    if (!values_end) return 0;

    // Parse pipe-separated values
    option->value_count = 0;
    option->current_index = 0;

    static char values_str[4096];
    int values_len = values_end - values_start;
    if (values_len >= (int)sizeof(values_str)) return 0;
    
    strncpy(values_str, values_start, values_len);
    values_str[values_len] = '\0';
    
    char *token = strtok(values_str, "|");
    while (token && option->value_count < MAX_OPTION_VALUES) {
        // Trim leading whitespace
        while (*token == ' ' || *token == '\t') token++;

        // Trim trailing whitespace
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }

        strncpy(option->possible_values[option->value_count], token, MAX_OPTION_VALUE_LEN - 1);
        option->possible_values[option->value_count][MAX_OPTION_VALUE_LEN - 1] = '\0';

        // Check if this is the current value
        if (strcmp(token, option->current_value) == 0) {
            option->current_index = option->value_count;
        }

        option->value_count++;
        token = strtok(NULL, "|");
    }
    
    return 1;
}

int settings_load(void) {
    char config_path[512];

    // v79: Use standard location: /mnt/sda1/configs/frogui.opt
    snprintf(config_path, sizeof(config_path), "/mnt/sda1/configs/frogui.opt");
    strncpy(current_config_path, config_path, sizeof(current_config_path) - 1);
    return settings_load_file(config_path, 1);
}

// Load core-specific settings
int settings_load_core(const char *core_name) {
    if (!core_name || core_name[0] == '\0') return 0;

    char config_path[512];
    char core_name_lower[256];

    // Create lowercase version of core name
    strncpy(core_name_lower, core_name, sizeof(core_name_lower) - 1);
    core_name_lower[sizeof(core_name_lower) - 1] = '\0';
    for (int i = 0; core_name_lower[i]; i++) {
        if (core_name_lower[i] >= 'A' && core_name_lower[i] <= 'Z') {
            core_name_lower[i] = core_name_lower[i] + 32;
        }
    }

    const char *base_dir = get_config_directory();

    // 1. Try: /mnt/sda1/configs/{core}/{core}.opt
    snprintf(config_path, sizeof(config_path), "%s/%s/%s.opt", base_dir, core_name, core_name);
    FILE *test = fopen(config_path, "r");
    if (test) {
        fclose(test);
        strncpy(current_config_path, config_path, sizeof(current_config_path) - 1);
        return settings_load_file(config_path, 0);
    }

    // 2. Try: /mnt/sda1/configs/{core_lower}/{core}.opt
    snprintf(config_path, sizeof(config_path), "%s/%s/%s.opt", base_dir, core_name_lower, core_name);
    test = fopen(config_path, "r");
    if (test) {
        fclose(test);
        strncpy(current_config_path, config_path, sizeof(current_config_path) - 1);
        return settings_load_file(config_path, 0);
    }

    // 3. Try: /mnt/sda1/configs/{core_lower}/{core_lower}.opt
    snprintf(config_path, sizeof(config_path), "%s/%s/%s.opt", base_dir, core_name_lower, core_name_lower);
    test = fopen(config_path, "r");
    if (test) {
        fclose(test);
        strncpy(current_config_path, config_path, sizeof(current_config_path) - 1);
        return settings_load_file(config_path, 0);
    }

    // 4. Try: /mnt/sda1/configs/{core}/{core_lower}.opt
    snprintf(config_path, sizeof(config_path), "%s/%s/%s.opt", base_dir, core_name, core_name_lower);
    test = fopen(config_path, "r");
    if (test) {
        fclose(test);
        strncpy(current_config_path, config_path, sizeof(current_config_path) - 1);
        return settings_load_file(config_path, 0);
    }

    // 5. Try scanning /mnt/sda1/configs/{core}/ or {core_lower}/ for any .opt file
    char dir_path[512];
    const char *dirs_to_try[2] = {core_name, core_name_lower};
    for (int d = 0; d < 2; d++) {
        if (d == 1 && strcmp(dirs_to_try[0], dirs_to_try[1]) == 0) continue;
        snprintf(dir_path, sizeof(dir_path), "%s/%s", base_dir, dirs_to_try[d]);
        DIR *dp = opendir(dir_path);
        if (dp) {
            struct dirent *ep;
            while ((ep = readdir(dp)) != NULL) {
                if (ep->d_name[0] == '.') continue;
                size_t nlen = strlen(ep->d_name);
                if (nlen > 4 && strcmp(ep->d_name + nlen - 4, ".opt") == 0 &&
                    strstr(ep->d_name, "_display.opt") == NULL) {
                    snprintf(config_path, sizeof(config_path), "%s/%s", dir_path, ep->d_name);
                    closedir(dp);
                    strncpy(current_config_path, config_path, sizeof(current_config_path) - 1);
                    return settings_load_file(config_path, 0);
                }
            }
            closedir(dp);
        }
    }

    return 0;
}

// Helper to add default settings if missing from config file
static void add_default_setting_if_missing(const char *name, const char *default_val, const char *possible_vals_pipe) {
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings[i].name, name) == 0) return; // already present
    }
    if (settings_count >= MAX_SETTINGS) return;

    SettingsOption *opt = &settings[settings_count];
    strncpy(opt->name, name, MAX_OPTION_NAME_LEN - 1);
    opt->name[MAX_OPTION_NAME_LEN - 1] = '\0';

    strncpy(opt->current_value, default_val, MAX_OPTION_VALUE_LEN - 1);
    opt->current_value[MAX_OPTION_VALUE_LEN - 1] = '\0';

    opt->value_count = 0;
    opt->current_index = 0;

    char vals_buf[512];
    strncpy(vals_buf, possible_vals_pipe, sizeof(vals_buf) - 1);
    vals_buf[sizeof(vals_buf) - 1] = '\0';

    char *token = strtok(vals_buf, "|");
    while (token && opt->value_count < MAX_OPTION_VALUES) {
        strncpy(opt->possible_values[opt->value_count], token, MAX_OPTION_VALUE_LEN - 1);
        opt->possible_values[opt->value_count][MAX_OPTION_VALUE_LEN - 1] = '\0';
        if (strcmp(token, default_val) == 0) {
            opt->current_index = opt->value_count;
        }
        opt->value_count++;
        token = strtok(NULL, "|");
    }
    settings_count++;
}

static void register_all_default_settings(void) {
    add_default_setting_if_missing("frogui_theme", "Default Dark", "Default Dark|Default Light|Ocean Blue|Cyberpunk|Retro Green|Sunset Orange|Purple Neon|Monochrome");
    add_default_setting_if_missing("frogui_gfx_theme", "theme_default", "");
    add_default_setting_if_missing("frogui_font", "Builtin 6x8", "Builtin 6x8|Builtin 8x8|Default 12px|Large 16px|Retro 8-bit");
    add_default_setting_if_missing("frogui_menu_layout", "theme_default", "theme_default|vertical|horizontal|grid|2_columns|3_columns|grid_2_columns|grid_3_columns");
    add_default_setting_if_missing("frogui_game_list_layout", "theme_default", "theme_default|vertical|horizontal|grid|2_columns|3_columns|grid_2_columns|grid_3_columns");
    add_default_setting_if_missing("frogui_grid_navigation", "vertical", "vertical|horizontal|grid|2_columns|3_columns|grid_2_columns|grid_3_columns");
    add_default_setting_if_missing("frogui_hide_system_names", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_hide_header_text", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_hide_game_names", "theme_default", "theme_default|false|true");
    add_default_setting_if_missing("frogui_game_name_color", "theme_default", "theme_default|white|black|red|green|blue|yellow|cyan|magenta|gray|orange|purple");
    add_default_setting_if_missing("frogui_xmb_waves", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_xmb_wave_color", "theme_default", "theme_default|white|black|red|green|blue|yellow|cyan|magenta|gray|orange|purple");
    add_default_setting_if_missing("frogui_xmb_wave_glow", "theme_default", "theme_default|low|medium|high");
    add_default_setting_if_missing("frogui_xmb_wave_variety", "theme_default", "theme_default|single|dual|complex");
    add_default_setting_if_missing("frogui_show_icons", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_show_game_icons", "false", "false|true|theme_default");
    add_default_setting_if_missing("frogui_anim_speed", "theme_default", "theme_default|normal|fast|very_fast|instant|slow|very_slow|snail");
    add_default_setting_if_missing("frogui_bg_anim_mode", "theme_default", "theme_default|fade|scroll_h|scroll_v|none");
    add_default_setting_if_missing("frogui_hide_footer", "false", "false|true");
    add_default_setting_if_missing("frogui_sections_visibility", "Open Submenu", "Open Submenu");
    add_default_setting_if_missing("frogui_text_in_empty_icon", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_game_text_in_empty", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_show_empty_icon_bg", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_show_selected_icon_bg", "theme_default", "theme_default|true|false");
    add_default_setting_if_missing("frogui_dim_unselected_icons", "theme_default", "theme_default|false|true");
    add_default_setting_if_missing("frogui_resume_on_boot", "false", "false|true");
    add_default_setting_if_missing("frogui_hide_empty", "true", "true|false");
    add_default_setting_if_missing("frogui_list_pillbox", "false", "false|true");
    add_default_setting_if_missing("frogui_hidden_sections", "", "");
}

static void settings_update_gfx_themes(void) {
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings[i].name, "frogui_gfx_theme") == 0) {
            // Only populate from scan if multicore.opt didn't already supply a list
            if (settings[i].value_count == 0) {
                int theme_count = gfx_theme_count();
                for (int t = 0; t < theme_count && settings[i].value_count < MAX_OPTION_VALUES; t++) {
                    const char* theme_name = gfx_theme_get_name(t);
                    strncpy(settings[i].possible_values[settings[i].value_count], theme_name, MAX_OPTION_VALUE_LEN - 1);
                    settings[i].possible_values[settings[i].value_count][MAX_OPTION_VALUE_LEN - 1] = '\0';
                    settings[i].value_count++;
                }
            }
            // Fix current_index to match current_value
            for (int j = 0; j < settings[i].value_count; j++) {
                if (strcmp(settings[i].possible_values[j], settings[i].current_value) == 0) {
                    settings[i].current_index = j;
                    break;
                }
            }
            break;
        }
    }
}

// Common settings loading function
static int settings_load_file(const char *config_path, int load_frogui_defaults) {
    settings_count = 0;
    if (load_frogui_defaults) {
        register_all_default_settings();
    }

    FILE *fp = fopen(config_path, "rb");
    if (!fp) {
        apply_theme_from_settings();
        apply_gfx_theme_from_settings();
        apply_font_from_settings();
        return settings_count;
    }

    // Read entire file into memory
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *file_contents = (char*)malloc(file_size + 1);
    if (!file_contents) {
        fclose(fp);
        apply_theme_from_settings();
        apply_gfx_theme_from_settings();
        apply_font_from_settings();
        return settings_count;
    }

    size_t bytes_read = fread(file_contents, 1, file_size, fp);
    file_contents[bytes_read] = '\0';
    fclose(fp);

    static char line[2048];

    // First pass: parse ### comment lines that define options and their possible values
    char *line_start = file_contents;
    char *line_end;

    while (line_start < file_contents + bytes_read) {
        line_end = line_start;
        while (line_end < file_contents + bytes_read && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        int line_len = line_end - line_start;
        if (line_len > 0 && line_len < (int)sizeof(line)) {
            memcpy(line, line_start, line_len);
            line[line_len] = '\0';

            if (strncmp(line, "###", 3) == 0) {
                SettingsOption *temp = (SettingsOption*)malloc(sizeof(SettingsOption));
                if (temp) {
                    memset(temp, 0, sizeof(SettingsOption));
                    if (parse_option_line(line, temp)) {
                        int found = -1;
                        for (int i = 0; i < settings_count; i++) {
                            if (strcmp(settings[i].name, temp->name) == 0) {
                                found = i;
                                break;
                            }
                        }
                        if (found >= 0) {
                            // Overwrite existing entry with values from multicore.opt
                            memcpy(settings[found].possible_values, temp->possible_values, sizeof(temp->possible_values));
                            settings[found].value_count = temp->value_count;
                            settings[found].current_index = temp->current_index;
                            strncpy(settings[found].current_value, temp->current_value, MAX_OPTION_VALUE_LEN - 1);
                            settings[found].current_value[MAX_OPTION_VALUE_LEN - 1] = '\0';
                        } else if (settings_count < MAX_SETTINGS) {
                            settings[settings_count] = *temp;
                            settings_count++;
                        }
                    }
                    free(temp);
                }
            }
        }

        while (line_end < file_contents + bytes_read && (*line_end == '\n' || *line_end == '\r')) {
            line_end++;
        }
        line_start = line_end;
    }

    // Second pass: parse key=value lines (active setting values)
    line_start = file_contents;
    while (line_start < file_contents + bytes_read) {
        line_end = line_start;
        while (line_end < file_contents + bytes_read && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        int line_len = line_end - line_start;
        if (line_len > 0 && line_len < (int)sizeof(line)) {
            memcpy(line, line_start, line_len);
            line[line_len] = '\0';

            if (strncmp(line, "###", 3) != 0) {
                char *equals = strchr(line, '=');
                if (equals) {
                    *equals = '\0';
                    char *option_name = line;
                    char *value_start = equals + 1;

                    // Trim option_name
                    while (*option_name == ' ' || *option_name == '\t') option_name++;
                    char *end = option_name + strlen(option_name) - 1;
                    while (end > option_name && (*end == ' ' || *end == '\t')) end--;
                    *(end + 1) = '\0';

                    // Trim value_start
                    while (*value_start == ' ' || *value_start == '\t' || *value_start == '"') value_start++;
                    end = value_start + strlen(value_start) - 1;
                    while (end > value_start && (*end == ' ' || *end == '\t' || *end == '"')) end--;
                    *(end + 1) = '\0';

                    int found = -1;
                    for (int i = 0; i < settings_count; i++) {
                        if (strcmp(settings[i].name, option_name) == 0) {
                            found = i;
                            break;
                        }
                    }

                    if (found >= 0) {
                        strncpy(settings[found].current_value, value_start, MAX_OPTION_VALUE_LEN - 1);
                        settings[found].current_value[MAX_OPTION_VALUE_LEN - 1] = '\0';

                        // Update current_index to match
                        int val_idx = -1;
                        for (int j = 0; j < settings[found].value_count; j++) {
                            if (strcmp(settings[found].possible_values[j], value_start) == 0) {
                                val_idx = j;
                                break;
                            }
                        }
                        if (val_idx >= 0) {
                            settings[found].current_index = val_idx;
                        }
                        // If value not in possible_values list, just keep current_index as is
                    }
                }
            }
        }

        while (line_end < file_contents + bytes_read && (*line_end == '\n' || *line_end == '\r')) {
            line_end++;
        }
        line_start = line_end;
    }

    free(file_contents);

    // Apply loaded settings
    apply_theme_from_settings();
    apply_gfx_theme_from_settings();
    apply_font_from_settings();

    return settings_count;
}


int settings_save(void) {
    if (current_config_path[0] == '\0') {
        snprintf(current_config_path, sizeof(current_config_path), "/mnt/sda1/configs/frogui.opt");
    }

    settings_saving = 1;

    FILE *fp = fopen(current_config_path, "w");
    if (!fp) {
        snprintf(current_config_path, sizeof(current_config_path), "/mnt/sda1/ROMS/frogui/frogui.opt");
        fp = fopen(current_config_path, "w");
    }

    if (!fp) {
        settings_saving = 0;
        return 0;
    }

    for (int i = 0; i < settings_count; i++) {
        const char *name = settings[i].name;
        const char *cur_val = settings[i].current_value;

        fprintf(fp, "### [%s] :[%s] :[", name, cur_val);
        for (int j = 0; j < settings[i].value_count; j++) {
            fprintf(fp, "%s%s", settings[i].possible_values[j], (j < settings[i].value_count - 1) ? "|" : "");
        }
        fprintf(fp, "]\n");

        fprintf(fp, "%s = \"%s\"\n\n", name, cur_val);
    }

    fflush(fp);
    fclose(fp);

    settings_saving = 0;

    // Apply theme and font changes after saving settings
    apply_theme_from_settings();
    apply_gfx_theme_from_settings();
    apply_font_from_settings();

    return 1;
}

int settings_get_count(void) {
    return settings_count;
}

int settings_find_index_by_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < settings_count; i++) {
        if (strcasecmp(settings[i].name, name) == 0) return i;
    }
    return -1;
}

const SettingsOption* settings_get_option(int index) {
    if (index < 0 || index >= settings_count) return NULL;
    return &settings[index];
}

void settings_cycle_option(int index) {
    if (index < 0 || index >= settings_count) return;
    if (settings[index].value_count == 0) return;
    
    settings[index].current_index = (settings[index].current_index + 1) % settings[index].value_count;
    strncpy(settings[index].current_value, 
           settings[index].possible_values[settings[index].current_index], 
           MAX_OPTION_VALUE_LEN - 1);
    settings[index].current_value[MAX_OPTION_VALUE_LEN - 1] = '\0';

    // Apply immediately for certain settings
    if (strcmp(settings[index].name, "frogui_gfx_theme") == 0) {
        gfx_theme_apply_by_name(settings[index].current_value);
    } else if (strcmp(settings[index].name, "frogui_theme") == 0) {
        apply_theme_from_settings();
    } else if (strcmp(settings[index].name, "frogui_font") == 0) {
        apply_font_from_settings();
    }
}

void settings_cycle_option_prev(int index) {
    if (index < 0 || index >= settings_count) return;
    if (settings[index].value_count == 0) return;
    
    settings[index].current_index = (settings[index].current_index - 1 + settings[index].value_count) % settings[index].value_count;
    strncpy(settings[index].current_value, 
           settings[index].possible_values[settings[index].current_index], 
           MAX_OPTION_VALUE_LEN - 1);
    settings[index].current_value[MAX_OPTION_VALUE_LEN - 1] = '\0';

    // Apply immediately for certain settings
    if (strcmp(settings[index].name, "frogui_gfx_theme") == 0) {
        gfx_theme_apply_by_name(settings[index].current_value);
    } else if (strcmp(settings[index].name, "frogui_theme") == 0) {
        apply_theme_from_settings();
    } else if (strcmp(settings[index].name, "frogui_font") == 0) {
        apply_font_from_settings();
    }
}

void settings_show_menu(void) {
    settings_active = 1;
    // v32: Remember position between openings - don't reset selection
    // settings_selected = 0;
    // settings_scroll_offset = 0;
}

static int sections_menu_active = 0;
static char section_names[64][32];
static int section_count = 0;
static int section_selected = 0;
static int section_scroll_offset = 0;

int sections_visibility_is_active(void) {
    return sections_menu_active;
}

int sections_visibility_get_count(void) {
    return section_count;
}

int sections_visibility_get_selected(void) {
    return section_selected;
}

int sections_visibility_get_scroll_offset(void) {
    return section_scroll_offset;
}

const char* sections_visibility_get_name(int index) {
    if (index < 0 || index >= section_count) return "";
    return section_names[index];
}

void open_sections_visibility_menu(void) {
    sections_menu_active = 1;
    section_count = 0;
    section_selected = 0;
    section_scroll_offset = 0;

    // Add special items first
    strncpy(section_names[section_count], "Recent games", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "Favorites", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "Random game", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "TOOLS", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "menu", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "MUSIC", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "VIDEOS", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "IMAGES", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    strncpy(section_names[section_count], "TEXT", 31);
    section_names[section_count][31] = '\0';
    section_count++;

    DIR *dir = opendir(ROMS_PATH);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            if (strcasecmp(ent->d_name, "frogui") == 0 || strcasecmp(ent->d_name, "saves") == 0 || strcasecmp(ent->d_name, "save") == 0) continue;

            char full[512];
            snprintf(full, sizeof(full), "%s/%s", ROMS_PATH, ent->d_name);
            int is_dir = 0;
            if (ent->d_type == DT_DIR) {
                is_dir = 1;
            } else {
                struct stat st;
                if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = 1;
            }
            if (is_dir) {
                if (section_count < 64) {
                    strncpy(section_names[section_count], ent->d_name, 31);
                    section_names[section_count][31] = '\0';
                    section_count++;
                }
            }
        }
        closedir(dir);
    }
}

int settings_handle_input(int up, int down, int left, int right, int a, int b, int x, int y) {
    if (!settings_active) return 0;

    // Handle sections visibility submenu if open
    if (sections_menu_active) {
        if (up) {
            if (section_selected > 0) section_selected--;
            else if (section_count > 0) section_selected = section_count - 1;

            if (section_selected < section_scroll_offset) section_scroll_offset = section_selected;
            else if (section_selected >= section_scroll_offset + 5) section_scroll_offset = section_selected - 5 + 1;
            return 1;
        }
        if (down) {
            if (section_selected < section_count - 1) section_selected++;
            else if (section_count > 0) section_selected = 0;

            if (section_selected < section_scroll_offset) section_scroll_offset = section_selected;
            else if (section_selected >= section_scroll_offset + 5) section_scroll_offset = section_selected - 5 + 1;
            return 1;
        }
        if (a || x || right || left) {
            if (section_selected >= 0 && section_selected < section_count) {
                toggle_section_hidden(section_names[section_selected]);
            }
            return 1;
        }
        if (b) {
            sections_menu_active = 0;
            return 1;
        }
        return 1;
    }

    // Don't allow any input while saving is in progress
    if (settings_saving) return 1;

    int max_visible = 7; // LVGL card layout

    if (up) {
        if (settings_selected > 0) {
            settings_selected--;
        } else {
            settings_selected = settings_count - 1;
        }

        // Adjust scroll offset
        if (settings_selected < settings_scroll_offset) {
            settings_scroll_offset = settings_selected;
        } else if (settings_selected >= settings_scroll_offset + max_visible) {
            settings_scroll_offset = settings_selected - max_visible + 1;
        }
        return 1;
    }

    if (down) {
        if (settings_selected < settings_count - 1) {
            settings_selected++;
        } else {
            settings_selected = 0;
        }

        // Adjust scroll offset
        if (settings_selected < settings_scroll_offset) {
            settings_scroll_offset = settings_selected;
        } else if (settings_selected >= settings_scroll_offset + max_visible) {
            settings_scroll_offset = settings_selected - max_visible + 1;
        }
        return 1;
    }

    if (right) {
        if (settings_selected >= 0 && settings_selected < settings_count) {
            if (strcmp(settings[settings_selected].name, "frogui_sections_visibility") == 0) {
                open_sections_visibility_menu();
                return 1;
            }
            if (strcmp(settings[settings_selected].name, "frogui_edit_theme_ini") == 0) {
                const GfxTheme *theme = gfx_theme_get_current();
                if (theme && theme->path[0] != '\0') {
                    char theme_ini_path[512];
                    snprintf(theme_ini_path, sizeof(theme_ini_path), "%s/theme.ini", theme->path);
                    text_editor_open(theme_ini_path);
                }
                return 1;
            }
        }
        // Cycle to next value
        settings_cycle_option(settings_selected);
        return 1;
    }

    if (left) {
        // Cycle to previous value
        if (settings_selected >= 0 && settings_selected < settings_count) {
            settings_cycle_option_prev(settings_selected);
        }
        return 1;
    }

    if (y) {
        // Reset to defaults
        if (settings_reset_to_defaults()) {
            // Successfully reset, settings are reloaded automatically
        }
        return 1;
    }

    if (a) {
        if (settings_selected >= 0 && settings_selected < settings_count) {
            if (strcmp(settings[settings_selected].name, "frogui_sections_visibility") == 0) {
                open_sections_visibility_menu();
                return 1;
            }
            if (strcmp(settings[settings_selected].name, "frogui_edit_theme_ini") == 0) {
                const GfxTheme *theme = gfx_theme_get_current();
                if (theme && theme->path[0] != '\0') {
                    char theme_ini_path[512];
                    snprintf(theme_ini_path, sizeof(theme_ini_path), "%s/theme.ini", theme->path);
                    text_editor_open(theme_ini_path);
                }
                return 1;
            }
        }
        if (settings_save()) {
            settings_active = 0;
            show_multicore_opt = false;
        }
        return 1;
    }

    if (b) {
        if (settings_save()) {
            settings_active = 0;
            show_multicore_opt = false;
        }
        return 1;
    }

    return 1;
}

void settings_close(void) {
    settings_active = 0;
}

int settings_is_active(void) {
    return settings_active;
}

int settings_get_selected_index(void) {
    return settings_selected;
}

int settings_get_scroll_offset(void) {
    return settings_scroll_offset;
}

// Apply theme changes from loaded settings
static void apply_theme_from_settings(void) {
    // Look for the frogui_theme setting
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings[i].name, "frogui_theme") == 0) {
            theme_load_from_settings(settings[i].current_value);
            break;
        }
    }
}

// Apply font changes from loaded settings
static void apply_font_from_settings(void) {
    // Look for the frogui_font setting
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings[i].name, "frogui_font") == 0) {
            font_load_from_settings(settings[i].current_value);
        }
        // v23: Apply font smoothing setting
        if (strcmp(settings[i].name, "frogui_font_smooth") == 0) {
            font_set_smooth(strcmp(settings[i].current_value, "true") == 0);
        }
        // v32: Apply font spacing setting
        if (strcmp(settings[i].name, "frogui_font_spacing") == 0) {
            int spacing = atoi(settings[i].current_value);
            font_set_spacing(spacing);
        }
    }
}

// Apply GFX theme changes from loaded settings
static void apply_gfx_theme_from_settings(void) {
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings[i].name, "frogui_gfx_theme") == 0) {
            gfx_theme_apply_by_name(settings[i].current_value);
            return;
        }
    }
    // No frogui_gfx_theme setting found - don't change current theme
}

// Get setting value by name
const char* settings_get_value(const char *setting_name) {
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings[i].name, setting_name) == 0) {
            return settings[i].current_value;
        }
    }
    return NULL;
}

// Set setting value by name
void settings_set_value(const char *setting_name, const char *value) {
    if (!setting_name || !value) return;
    for (int i = 0; i < settings_count; i++) {
        if (strcmp(settings[i].name, setting_name) == 0) {
            strncpy(settings[i].current_value, value, MAX_OPTION_VALUE_LEN - 1);
            settings[i].current_value[MAX_OPTION_VALUE_LEN - 1] = '\0';

            for (int j = 0; j < settings[i].value_count; j++) {
                if (strcmp(settings[i].possible_values[j], value) == 0) {
                    settings[i].current_index = j;
                    break;
                }
            }
            return;
        }
    }
    if (settings_count < MAX_SETTINGS) {
        strncpy(settings[settings_count].name, setting_name, MAX_OPTION_NAME_LEN - 1);
        settings[settings_count].name[MAX_OPTION_NAME_LEN - 1] = '\0';
        strncpy(settings[settings_count].current_value, value, MAX_OPTION_VALUE_LEN - 1);
        settings[settings_count].current_value[MAX_OPTION_VALUE_LEN - 1] = '\0';
        strncpy(settings[settings_count].possible_values[0], value, MAX_OPTION_VALUE_LEN - 1);
        settings[settings_count].value_count = 1;
        settings[settings_count].current_index = 0;
        settings_count++;
    }
}

// Check if a section/platform is hidden
int is_section_hidden(const char *section_name) {
    if (!section_name || section_name[0] == '\0') return 0;

    const char *hidden = settings_get_value("frogui_hidden_sections");
    if (!hidden || hidden[0] == '\0') return 0;

    // Build list of names/aliases to test against hidden tokens
    const char *test_names[8];
    int test_count = 0;

    test_names[test_count++] = section_name;

    if (strcasecmp(section_name, "Recent games") == 0 || strcasecmp(section_name, "RECENT_GAMES") == 0 || strcasecmp(section_name, "Recent_games") == 0) {
        test_names[test_count++] = "Recent games";
        test_names[test_count++] = "RECENT_GAMES";
        test_names[test_count++] = "Recent_games";
    } else if (strcasecmp(section_name, "Favorites") == 0 || strcasecmp(section_name, "FAVORITES") == 0) {
        test_names[test_count++] = "Favorites";
        test_names[test_count++] = "FAVORITES";
    } else if (strcasecmp(section_name, "Random game") == 0 || strcasecmp(section_name, "RANDOM_GAME") == 0 || strcasecmp(section_name, "Random_game") == 0) {
        test_names[test_count++] = "Random game";
        test_names[test_count++] = "RANDOM_GAME";
        test_names[test_count++] = "Random_game";
    } else if (strcasecmp(section_name, "FC") == 0 || strcasecmp(section_name, "NES") == 0 || strcasecmp(section_name, "FAMICOM") == 0) {
        test_names[test_count++] = "FC";
        test_names[test_count++] = "NES";
        test_names[test_count++] = "FAMICOM";
    } else if (strcasecmp(section_name, "SFC") == 0 || strcasecmp(section_name, "SNES") == 0 || strcasecmp(section_name, "SUPER FAMICOM") == 0) {
        test_names[test_count++] = "SFC";
        test_names[test_count++] = "SNES";
        test_names[test_count++] = "SUPER FAMICOM";
    } else if (strcasecmp(section_name, "MD") == 0 || strcasecmp(section_name, "GENESIS") == 0 || strcasecmp(section_name, "MEGADRIVE") == 0) {
        test_names[test_count++] = "MD";
        test_names[test_count++] = "GENESIS";
        test_names[test_count++] = "MEGADRIVE";
    } else if (strcasecmp(section_name, "GBA") == 0 || strcasecmp(section_name, "GAMEBOY ADVANCE") == 0) {
        test_names[test_count++] = "GBA";
        test_names[test_count++] = "GAMEBOY ADVANCE";
    } else if (strcasecmp(section_name, "GB") == 0 || strcasecmp(section_name, "GAMEBOY") == 0) {
        test_names[test_count++] = "GB";
        test_names[test_count++] = "GAMEBOY";
    } else if (strcasecmp(section_name, "GBC") == 0 || strcasecmp(section_name, "GAMEBOY COLOR") == 0) {
        test_names[test_count++] = "GBC";
        test_names[test_count++] = "GAMEBOY COLOR";
    } else if (strcasecmp(section_name, "PCE") == 0 || strcasecmp(section_name, "TURBOGRAFX") == 0 || strcasecmp(section_name, "PC ENGINE") == 0) {
        test_names[test_count++] = "PCE";
        test_names[test_count++] = "TURBOGRAFX";
        test_names[test_count++] = "PC ENGINE";
    } else if (strcasecmp(section_name, "ARCADE") == 0 || strcasecmp(section_name, "MAME") == 0 || strcasecmp(section_name, "CPS1") == 0 || strcasecmp(section_name, "CPS2") == 0 || strcasecmp(section_name, "NEOGEO") == 0) {
        test_names[test_count++] = "ARCADE";
        test_names[test_count++] = "MAME";
        test_names[test_count++] = "CPS1";
        test_names[test_count++] = "CPS2";
        test_names[test_count++] = "NEOGEO";
    }

    char buf[512];
    strncpy(buf, hidden, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, ",;");
    while (token) {
        while (*token == ' ' || *token == '\t' || *token == '"' || *token == '\'' || *token == '\r' || *token == '\n') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t' || *end == '"' || *end == '\'' || *end == '\r' || *end == '\n')) {
            *end = '\0';
            end--;
        }
        if (token[0] != '\0') {
            for (int k = 0; k < test_count; k++) {
                if (strcasecmp(token, test_names[k]) == 0) return 1;
            }
        }
        token = strtok(NULL, ",;");
    }
    return 0;
}

// Toggle section/platform visibility (hide/show)
void toggle_section_hidden(const char *section_name) {
    if (!section_name || section_name[0] == '\0') return;

    const char *hidden = settings_get_value("frogui_hidden_sections");
    char new_hidden[512] = "";
    int was_hidden = 0;

    // Build alias list to match against
    const char *test_names[8];
    int test_count = 0;
    test_names[test_count++] = section_name;

    if (strcasecmp(section_name, "Recent games") == 0 || strcasecmp(section_name, "RECENT_GAMES") == 0 || strcasecmp(section_name, "Recent_games") == 0) {
        test_names[test_count++] = "Recent games";
        test_names[test_count++] = "RECENT_GAMES";
        test_names[test_count++] = "Recent_games";
    } else if (strcasecmp(section_name, "Favorites") == 0 || strcasecmp(section_name, "FAVORITES") == 0) {
        test_names[test_count++] = "Favorites";
        test_names[test_count++] = "FAVORITES";
    } else if (strcasecmp(section_name, "Random game") == 0 || strcasecmp(section_name, "RANDOM_GAME") == 0 || strcasecmp(section_name, "Random_game") == 0) {
        test_names[test_count++] = "Random game";
        test_names[test_count++] = "RANDOM_GAME";
        test_names[test_count++] = "Random_game";
    } else if (strcasecmp(section_name, "FC") == 0 || strcasecmp(section_name, "NES") == 0 || strcasecmp(section_name, "FAMICOM") == 0) {
        test_names[test_count++] = "FC";
        test_names[test_count++] = "NES";
        test_names[test_count++] = "FAMICOM";
    } else if (strcasecmp(section_name, "SFC") == 0 || strcasecmp(section_name, "SNES") == 0 || strcasecmp(section_name, "SUPER FAMICOM") == 0) {
        test_names[test_count++] = "SFC";
        test_names[test_count++] = "SNES";
        test_names[test_count++] = "SUPER FAMICOM";
    } else if (strcasecmp(section_name, "MD") == 0 || strcasecmp(section_name, "GENESIS") == 0 || strcasecmp(section_name, "MEGADRIVE") == 0) {
        test_names[test_count++] = "MD";
        test_names[test_count++] = "GENESIS";
        test_names[test_count++] = "MEGADRIVE";
    } else if (strcasecmp(section_name, "GBA") == 0 || strcasecmp(section_name, "GAMEBOY ADVANCE") == 0) {
        test_names[test_count++] = "GBA";
        test_names[test_count++] = "GAMEBOY ADVANCE";
    } else if (strcasecmp(section_name, "GB") == 0 || strcasecmp(section_name, "GAMEBOY") == 0) {
        test_names[test_count++] = "GB";
        test_names[test_count++] = "GAMEBOY";
    } else if (strcasecmp(section_name, "GBC") == 0 || strcasecmp(section_name, "GAMEBOY COLOR") == 0) {
        test_names[test_count++] = "GBC";
        test_names[test_count++] = "GAMEBOY COLOR";
    } else if (strcasecmp(section_name, "PCE") == 0 || strcasecmp(section_name, "TURBOGRAFX") == 0 || strcasecmp(section_name, "PC ENGINE") == 0) {
        test_names[test_count++] = "PCE";
        test_names[test_count++] = "TURBOGRAFX";
        test_names[test_count++] = "PC ENGINE";
    } else if (strcasecmp(section_name, "ARCADE") == 0 || strcasecmp(section_name, "MAME") == 0 || strcasecmp(section_name, "CPS1") == 0 || strcasecmp(section_name, "CPS2") == 0 || strcasecmp(section_name, "NEOGEO") == 0) {
        test_names[test_count++] = "ARCADE";
        test_names[test_count++] = "MAME";
        test_names[test_count++] = "CPS1";
        test_names[test_count++] = "CPS2";
        test_names[test_count++] = "NEOGEO";
    }

    if (hidden && hidden[0] != '\0') {
        char buf[512];
        strncpy(buf, hidden, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *token = strtok(buf, ",;");
        while (token) {
            while (*token == ' ' || *token == '\t' || *token == '"' || *token == '\'' || *token == '\r' || *token == '\n') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && (*end == ' ' || *end == '\t' || *end == '"' || *end == '\'' || *end == '\r' || *end == '\n')) {
                *end = '\0';
                end--;
            }
            if (token[0] != '\0') {
                int matches = 0;
                for (int k = 0; k < test_count; k++) {
                    if (strcasecmp(token, test_names[k]) == 0) {
                        matches = 1;
                        break;
                    }
                }
                if (matches) {
                    was_hidden = 1;
                } else {
                    if (new_hidden[0] != '\0') strncat(new_hidden, ",", sizeof(new_hidden) - strlen(new_hidden) - 1);
                    strncat(new_hidden, token, sizeof(new_hidden) - strlen(new_hidden) - 1);
                }
            }
            token = strtok(NULL, ",;");
        }
    }

    if (!was_hidden) {
        if (new_hidden[0] != '\0') strncat(new_hidden, ",", sizeof(new_hidden) - strlen(new_hidden) - 1);
        strncat(new_hidden, section_name, sizeof(new_hidden) - strlen(new_hidden) - 1);
    }

    settings_set_value("frogui_hidden_sections", new_hidden);
    settings_save();
}

// Get default configs directory - always use /mnt/sda1/default_configs
static const char* get_default_config_directory(void) {
    return "/mnt/sda1/default_configs";
}

// Reset settings to defaults by copying from default_configs
int settings_reset_to_defaults(void) {
    if (current_config_path[0] == '\0') {
        return 0;  // No config file loaded
    }

    settings_saving = 1;

    // Determine the default config path
    char default_path[512];
    const char *default_base = get_default_config_directory();

    // Extract core name from filename
    const char *filename = strrchr(current_config_path, '/');
    if (filename) {
        filename++;
    } else {
        filename = current_config_path;
    }

    // Get core name by removing .opt extension
    char core_name[256];
    strncpy(core_name, filename, sizeof(core_name) - 1);
    core_name[sizeof(core_name) - 1] = '\0';
    char *ext = strstr(core_name, ".opt");
    if (ext) {
        *ext = '\0';
    }

    // Build path: /mnt/sda1/default_configs/{coreName}/{coreName}.opt
    if (strcmp(core_name, "multicore") == 0) {
        snprintf(default_path, sizeof(default_path), "%s/multicore.opt", default_base);
    } else {
        snprintf(default_path, sizeof(default_path), "%s/%s/%s.opt", default_base, core_name, core_name);
    }

    FILE *default_file = fopen(default_path, "r");
    if (!default_file) {
        settings_saving = 0;
        return 0;
    }

    FILE *dest_file = fopen(current_config_path, "w");
    if (!dest_file) {
        fclose(default_file);
        settings_saving = 0;
        return 0;
    }

    // Copy default config directly to current config
    char buffer[1024];
    size_t bytes;
    int copy_error = 0;
    while ((bytes = fread(buffer, 1, sizeof(buffer), default_file)) > 0) {
        if (fwrite(buffer, 1, bytes, dest_file) != bytes) {
            copy_error = 1;
            break;
        }
    }

    fclose(default_file);

    if (!copy_error) {
        if (fflush(dest_file) != 0) {
            copy_error = 1;
        }
    }

    fclose(dest_file);


    if (copy_error) {
        settings_saving = 0;
        return 0;
    }

    settings_saving = 0;

    // Reload settings from the reset file
    settings_load_file(current_config_path, 1);

    // Reset UI state
    settings_selected = 0;
    settings_scroll_offset = 0;

    return 1;
}

// Check if settings are currently being saved
int settings_is_saving(void) {
    return settings_saving;
}