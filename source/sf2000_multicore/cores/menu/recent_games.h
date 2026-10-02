#ifndef RECENT_GAMES_H
#define RECENT_GAMES_H

#define MAX_RECENT_GAMES 10
#define HISTORY_FILE "/mnt/sda1/frogui/game_history.txt"

// Recent games structure
typedef struct {
    char core_name[256];
    char game_name[256];
    char display_name[256];
    char full_path[512];  // Full path for thumbnail lookup
} RecentGame;

// Initialize recent games system
void recent_games_init(void);

// Load recent games from file
void recent_games_load(void);

// Save recent games to file  
void recent_games_save(void);

// Add game to recent history (moves to top if already exists)
void recent_games_add(const char *core_name, const char *game_name, const char *full_path);

// Get recent games list
const RecentGame* recent_games_get_list(void);

// Get count of recent games
int recent_games_get_count(void);

#endif // RECENT_GAMES_H