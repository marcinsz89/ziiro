#include "studio.h"
#include "archive_artwork.hpp"
#include "catalog.hpp"
#include "nfc.h"

#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <set>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
extern char **environ;

namespace {
constexpr uint32_t ink = 0xF5F0E7;
constexpr uint32_t muted = 0xACB2C3;
constexpr uint32_t background = 0x141720;
constexpr uint32_t panel = 0x202638;
constexpr uint32_t selection = 0x31394E;
constexpr uint32_t line = 0x3D465E;
constexpr uint32_t mint = 0xAFF2BA;
constexpr uint32_t amber = 0xF7C28A;
constexpr lv_point_t nfc_ripple_center = {244, 91}; // Center of the figure artwork at rest.
constexpr uint32_t nfc_ripple_duration = 1600;
constexpr uint32_t nfc_ripple_fade_start = 550;
constexpr uint32_t nfc_ripple_frame = 45;

struct NfcWave {
    uint32_t delay;
    int width;
    int core_width;
    uint32_t opacity;
    int sway;
};

constexpr std::array<NfcWave, 3> nfc_waves = {{
    {0,   20, 9, 110,  1},
    {300, 12, 5,  92, -1},
    {600,  7, 2,  76,  1},
}};
constexpr uint32_t nfc_ripple_end = nfc_ripple_duration + nfc_waves.back().delay;
constexpr uint32_t nfc_ripple_period = nfc_ripple_end + 260;
constexpr int row_start = 52;
constexpr int row_step = 29;
constexpr int section_width = 157;
constexpr std::size_t row_count = 3;
constexpr std::size_t settings_count = 6;

struct Group {
    std::string name;
    std::vector<const Figure *> figures;
    std::vector<Group> games;
    std::size_t backed = 0;
    std::size_t total = 0;
};

struct Row {
    lv_obj_t *badge;
    lv_obj_t *glyph;
    lv_obj_t *image;
    lv_obj_t *title;
    lv_obj_t *note;
};

enum class Screen { Welcome, Groups, PlatformGames, Figures, Search, Figure, Games, Backup, Settings };
enum class Grouping { Series, Franchise, Platform };

struct {
    Catalog catalog;
    fs::path profile;
    fs::path share;
    fs::path metadata;
    fs::path backups;
    fs::path custom_artwork;
    fs::path artwork_cache;
    fs::path downloaded_artwork;
    fs::path downloaded_logos;
    ArchiveArtwork archive_artwork;
    std::vector<Group> groups;
    std::size_t group = 0;
    std::size_t platform_game = 0;
    std::size_t item = 0;
    std::size_t first = 0;
    Screen screen = Screen::Groups;
    Screen return_to = Screen::Groups;
    Screen search_origin = Screen::Groups;
    std::string query;
    std::vector<const Figure *> results;
    std::size_t search_item = 0;
    std::size_t search_first = 0;
    bool editing_search = false;
    bool owned = false;
    Grouping grouping = Grouping::Series;
    std::size_t welcome_item = 0;
    std::size_t settings_item = 0;
    bool show_miniatures = true;
    bool download_artwork = false;
    pid_t artwork_pid = 0;
    bool artwork_is_logo = false;
    std::string artwork_id;
    std::set<std::string> artwork_failures;
    std::set<std::string> unindexed_logos;
    std::string preview_artwork;
    uint32_t preview_focused_at = 0;
    bool preview_logo_failed = false;
    pid_t batch_pid = 0;
    bool batch_logos = false;
    int batch_output = -1;
    bool batch_cancelled = false;
    std::string batch_partial;
    std::size_t batch_checked = 0;
    std::size_t batch_total = 0;
    std::size_t batch_failed = 0;
    std::size_t game = 0;
    std::vector<const GameUsage *> game_usages;
    pid_t sync_pid = 0;
    bool sync_cancelled = false;
    const char *sync_status = "";
    NfcState nfc = NFC_UNAVAILABLE;
#ifdef ZIIRO_DEVICE
    pid_t nfc_sound_pid = 0;
#endif
    lv_obj_t *nfc_ripple;
    lv_timer_t *nfc_ripple_timer = nullptr;
    uint32_t nfc_ripple_started_at = 0;
    uint32_t nfc_ripple_phase = 0;
    lv_obj_t *browse;
    lv_obj_t *activity;
    lv_obj_t *selection_box;
    lv_obj_t *section;
    lv_obj_t *empty_list;
    lv_obj_t *view;
    lv_obj_t *nfc_label;
    std::array<Row, row_count> rows;
    lv_obj_t *hero;
    lv_obj_t *art_stage;
    lv_obj_t *disc;
    lv_obj_t *art_glyph;
    lv_obj_t *art_image;
    lv_obj_t *art_status;
    lv_obj_t *art_check;
    lv_obj_t *name;
    lv_obj_t *detail;
    lv_obj_t *activity_caption;
    lv_obj_t *activity_title;
    lv_obj_t *activity_detail;
    lv_obj_t *activity_badge;
    lv_obj_t *activity_option;
    lv_obj_t *settings_artwork;
    lv_obj_t *settings_updated;
    lv_obj_t *settings_batch;
    lv_obj_t *settings_logos;
    lv_obj_t *game_effect;
    lv_obj_t *activity_art;
    lv_obj_t *activity_glyph;
    lv_obj_t *activity_image;
    lv_obj_t *artwork_hint;
    lv_obj_t *activity_status;
    lv_obj_t *activity_check;
    lv_obj_t *footer;
} ui;

lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height,
              uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

lv_obj_t *label(lv_obj_t *parent, int x, int y, const char *text,
                const lv_font_t *font, uint32_t color)
{
    auto *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    return obj;
}

lv_obj_t *short_label(lv_obj_t *parent, int x, int y, int width,
                      const lv_font_t *font, uint32_t color)
{
    auto *obj = label(parent, x, y, "", font, color);
    lv_obj_set_width(obj, width);
    lv_obj_set_height(obj, font->line_height);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    return obj;
}

void visible(lv_obj_t *obj, bool show)
{
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void backup_status(lv_obj_t *badge, lv_obj_t *check, bool backed)
{
    lv_obj_set_style_bg_color(badge, lv_color_hex(backed ? mint : background), 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(backed ? background : muted), 0);
    visible(check, backed);
}

uint32_t color_for(const std::string &key)
{
    constexpr uint32_t colors[]{mint, 0xF5C483, 0x9CD9F9, 0xEDACCC, 0xC3B6FE, 0xFFB69E};
    uint32_t hash = 2166136261u;
    for (unsigned char ch : key) hash = (hash ^ ch) * 16777619u;
    return colors[hash % std::size(colors)];
}

std::string monogram(const std::string &name)
{
    const auto slug = artwork_slug(name);
    if (slug.empty()) return "?";
    std::string mark(1, slug.front());
    const auto separator = slug.find('-');
    if (separator == std::string::npos && slug.size() > 1) mark += slug[1];
    if (separator != std::string::npos) mark += slug[separator + 1];
    for (char &ch : mark) {
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    }
    return mark;
}

bool regular(const fs::path &path)
{
    std::error_code error;
    return fs::is_regular_file(fs::symlink_status(path, error));
}

bool set_art(lv_obj_t *image, lv_obj_t *fallback, const fs::path &path, int width, int height,
             unsigned dimension_limit = 256)
{
    if (path.empty() || !regular(path)) return false;
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size > 1024u * 1024u) return false;
    const auto absolute = fs::absolute(path, error);
    if (error) return false;
    const std::string source = "A:" + absolute.string();
    lv_image_header_t header{};
    if (lv_image_decoder_get_info(source.c_str(), &header) != LV_RESULT_OK ||
        header.w == 0 || header.h == 0 || header.w > dimension_limit || header.h > dimension_limit) return false;

    lv_image_set_src(image, source.c_str());
    const auto scale = std::min(256 * width / header.w, 256 * height / header.h);
    lv_image_set_scale(image, static_cast<uint32_t>(scale));
    lv_obj_center(image);
    visible(fallback, false);
    visible(image, true);
    return true;
}

bool browsing_logos()
{
    return ui.screen == Screen::PlatformGames || (ui.screen == Screen::Groups && ui.grouping != Grouping::Platform);
}

bool browsing_figures()
{
    return ui.screen == Screen::Figures || (ui.screen == Screen::Search && !ui.editing_search);
}

fs::path logo_key(const std::string &group)
{
    if (ui.screen == Screen::PlatformGames) {
        return fs::path("titles") / artwork_slug(ui.groups[ui.group].name) / artwork_slug(group);
    }
    return fs::path(ui.grouping == Grouping::Franchise ? "games" : "series") / artwork_slug(group);
}

void show_art(lv_obj_t *image, lv_obj_t *fallback, const std::string &group,
              const Figure *figure, int width, int height)
{
    if (figure) {
        for (auto path : figure->backups) {
            path.replace_extension(".png");
            if (set_art(image, fallback, path, width, height)) return;
        }
    }
    const bool platform = !figure && ui.grouping == Grouping::Platform && ui.screen == Screen::Groups;
    const char *folder = "series";
    if (ui.grouping == Grouping::Franchise) folder = "games";
    if (ui.grouping == Grouping::Platform) folder = platform ? "platforms" : "titles";
    const auto relative_path = figure ? fs::path("figures") / (figure->id + ".png") :
        fs::path(folder) / (artwork_slug(group) + ".png");
    const auto logo = !figure && browsing_logos() ? fs::path(logo_key(group).string() + ".png") : fs::path{};
    if (!ui.custom_artwork.empty()) {
        if (ui.screen == Screen::PlatformGames && !logo.empty() &&
            set_art(image, fallback, ui.custom_artwork / logo, width, height)) return;
        if (set_art(image, fallback, ui.custom_artwork / relative_path, width, height)) return;
    }
    if (platform && set_art(image, fallback, ui.share / "artwork" / relative_path, width, height)) return;
    const auto archive_image = figure ? ui.archive_artwork.figure_image(*figure) :
        ui.archive_artwork.group_image(group);
    if (set_art(image, fallback, archive_image, width, height)) return;
    if (!logo.empty() && set_art(image, fallback, ui.downloaded_logos / logo, width, height)) return;
    if (figure && set_art(image, fallback, ui.downloaded_artwork / (figure->id + ".png"), width, height, 512)) return;
    visible(image, false);
    visible(fallback, true);
}

void set_y(void *object, int32_t y)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(object), y);
}

void animate_y(lv_obj_t *object, int from, int to, bool animate)
{
    lv_anim_delete(object, set_y);
    if (!animate || from == to) {
        lv_obj_set_y(object, to);
        return;
    }
    lv_anim_t motion;
    lv_anim_init(&motion);
    lv_anim_set_var(&motion, object);
    lv_anim_set_exec_cb(&motion, set_y);
    lv_anim_set_values(&motion, from, to);
    lv_anim_set_duration(&motion, 155);
    lv_anim_set_path_cb(&motion, lv_anim_path_ease_out);
    lv_anim_start(&motion);
}

const std::string &group_name(const Figure &figure)
{
    return ui.grouping == Grouping::Franchise ? figure.franchise : figure.series;
}

std::string local_backup_note(const Figure &figure)
{
    if (figure.type != "Raw NFC tag" || figure.backups.empty()) return {};
    const auto folder = figure.backups.front().lexically_relative(ui.backups).parent_path().filename();
    return folder.empty() ? figure.id : folder.string();
}

void add_figure(Group &group, const std::string &name, const Figure &figure)
{
    group.name = name;
    const bool backed = !figure.backups.empty();
    ++group.total;
    group.backed += backed;
    if (!ui.owned || backed) group.figures.push_back(&figure);
}

void sort_group(Group &group)
{
    std::sort(group.figures.begin(), group.figures.end(), [](const Figure *a, const Figure *b) {
        return a->name == b->name ? a->id < b->id : a->name < b->name;
    });
}

void rebuild_groups()
{
    ui.game_usages.clear();
    std::map<std::string, Group> groups;
    std::map<std::pair<std::string, std::string>, Group> platform_games;
    for (const auto &figure : ui.catalog.figures) {
        if (ui.grouping != Grouping::Platform) {
            const auto &name = group_name(figure);
            add_figure(groups[name], name, figure);
            continue;
        }
        // A figure can have several effects for one game and games on several platforms.
        std::map<std::string, std::set<std::string>> compatibility;
        for (const auto &use : figure.games) compatibility[use.platform].insert(use.name);
        for (const auto &[platform, games] : compatibility) {
            add_figure(groups[platform], platform, figure);
            for (const auto &game : games) add_figure(platform_games[{platform, game}], game, figure);
        }
    }
    for (auto &[key, game] : platform_games) {
        if (game.figures.empty()) continue;
        sort_group(game);
        groups.at(key.first).games.push_back(std::move(game));
    }
    ui.groups.clear();
    for (auto &[name, group] : groups) {
        if (group.figures.empty()) continue;
        sort_group(group);
        ui.groups.push_back(std::move(group));
    }
    ui.screen = Screen::Groups;
    ui.group = ui.platform_game = ui.item = ui.first = 0;
}

const Group &current_figure_group()
{
    const auto &group = ui.groups[ui.group];
    return ui.grouping == Grouping::Platform ? group.games[ui.platform_game] : group;
}

bool matches_platform_game(const GameUsage &usage)
{
    return usage.platform == ui.groups[ui.group].name && usage.name == current_figure_group().name;
}

bool using_search_results()
{
    return ui.screen == Screen::Search ||
        ((ui.screen == Screen::Figure || ui.screen == Screen::Games) && ui.return_to == Screen::Search);
}

const std::vector<const Figure *> &figure_list()
{
    return using_search_results() ? ui.results : current_figure_group().figures;
}

std::size_t &figure_selection()
{
    return using_search_results() ? ui.search_item : ui.item;
}

std::string search_key(std::string text)
{
    // Keep the catalog's Latin accent variants searchable from the Zero keyboard.
    for (const auto &[accent, letter] : {std::pair{"É", 'e'}, {"é", 'e'}, {"Ō", 'o'}, {"ō", 'o'}}) {
        const std::string spelling = accent;
        for (auto index = text.find(spelling); index != std::string::npos; index = text.find(spelling, index + 1)) {
            text.replace(index, spelling.size(), 1, letter);
        }
    }
    return artwork_slug(text);
}

void filter_search()
{
    auto query = search_key(ui.query);
    std::replace(query.begin(), query.end(), '-', ' ');
    std::istringstream input(query);
    std::vector<std::string> terms;
    for (std::string term; input >> term;) terms.push_back(term);

    ui.results.clear();
    for (const auto &figure : ui.catalog.figures) {
        if (ui.owned && figure.backups.empty()) continue;
        if (!terms.empty()) {
            const auto key = search_key(figure.name + " " + figure.series + " " + figure.franchise +
                                        " " + local_backup_note(figure));
            if (!std::all_of(terms.begin(), terms.end(), [&](const auto &term) {
                return key.find(term) != std::string::npos;
            })) continue;
        }
        ui.results.push_back(&figure);
    }
    std::sort(ui.results.begin(), ui.results.end(), [](const Figure *a, const Figure *b) {
        if (a->name != b->name) return a->name < b->name;
        if (a->series != b->series) return a->series < b->series;
        return a->id < b->id;
    });
    ui.search_item = ui.search_first = 0;
}

std::string search_heading()
{
    if (ui.query.empty()) return ui.editing_search ? "Type to find amiibo" : "Search results";
    for (std::size_t first = 0; first < ui.query.size(); ++first) {
        std::string text = ui.editing_search ? "Find " : "Results ";
        if (first) text += "...";
        text += ui.query.substr(first);
        if (ui.editing_search) text += '_';
        lv_point_t size;
        lv_text_get_size(&size, text.c_str(), &lv_font_montserrat_12, 0, 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= section_width) return text;
    }
    return ui.editing_search ? "Find" : "Search results";
}

std::string count_label(std::size_t count, const char *singular, const char *plural)
{
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

bool nfc_needs_retry(NfcState state)
{
    return state != NFC_READY && state != NFC_UNAVAILABLE && state != NFC_CHECKING &&
        state != NFC_STARTING && state != NFC_PRESENTING && state != NFC_STOPPING;
}

void draw_browser_footer()
{
    if (ui.screen == Screen::Search) {
        lv_label_set_text(ui.footer, ui.editing_search ? "Enter Search  Esc Back"
            : ui.results.empty() ? "Esc Back" : "F/X Move  Enter Open  Esc Back");
        return;
    }
    lv_label_set_text(ui.footer, ui.catalog.updated.empty() ? "S Search  G Group  T Rescan  U Settings"
                                                           : "S Search  G Group  Tab View  U Settings");
}

void draw_browser(bool animate, bool change_view = false)
{
    const bool search = ui.screen == Screen::Search;
    const bool list_figures = search || ui.screen == Screen::Figures;
    const bool platform_games = ui.screen == Screen::PlatformGames;
    const auto &groups = platform_games ? ui.groups[ui.group].games : ui.groups;
    const std::size_t selected = list_figures ? figure_selection() : platform_games ? ui.platform_game : ui.group;
    const std::size_t count = list_figures ? figure_list().size() : groups.size();
    auto &first = search ? ui.search_first : ui.first;
    const auto old_first = first;
    if (selected < first) first = selected;
    else if (selected >= first + row_count) first = selected - row_count + 1;

    visible(ui.activity, false);
    visible(ui.browse, true);
    visible(ui.hero, ui.show_miniatures);
    visible(ui.empty_list, !ui.show_miniatures && count == 0);
    lv_obj_set_width(ui.selection_box, ui.show_miniatures ? 158 : 297);
    visible(ui.selection_box, count > 0 && !ui.editing_search);
    lv_label_set_text(ui.view, ui.owned ? "LIBRARY" : "CATALOG");
    draw_browser_footer();

    const char *heading = "AMIIBO SERIES";
    if (ui.grouping == Grouping::Franchise) heading = "GAME FRANCHISES";
    if (ui.grouping == Grouping::Platform) heading = "PLATFORMS";
    const std::string section = search ? search_heading() : list_figures ? current_figure_group().name :
        platform_games ? ui.groups[ui.group].name : heading;
    lv_label_set_text(ui.section, section.c_str());
    if (count) {
        lv_obj_update_layout(ui.selection_box);
        animate_y(ui.selection_box, lv_obj_get_y(ui.selection_box),
                  row_start + row_step * static_cast<int>(selected - first),
                  animate && old_first == first);
    }

    for (std::size_t i = 0; i < row_count; ++i) {
        auto &row = ui.rows[i];
        const auto index = first + i;
        const bool show = index < count;
        visible(row.badge, show && ui.show_miniatures);
        visible(row.title, show);
        visible(row.note, show);
        if (!show) continue;

        const Figure *figure = list_figures ? figure_list()[index] : nullptr;
        const auto &group = figure ? group_name(*figure) : groups[index].name;
        const auto &name = figure ? figure->name : group;
        lv_obj_set_x(row.title, ui.show_miniatures ? 46 : 16);
        lv_obj_set_x(row.note, ui.show_miniatures ? 46 : 16);
        lv_obj_set_width(row.title, ui.show_miniatures ? 118 : 285);
        lv_obj_set_width(row.note, ui.show_miniatures ? 117 : 285);
        lv_label_set_text(row.title, name.c_str());
        std::string note;
        if (figure && figure->type == "Raw NFC tag") {
            note = local_backup_note(*figure);
        } else if (search) {
            note = figure->series;
        } else if (figure && ui.grouping == Grouping::Platform) {
            const auto use = std::find_if(figure->games.begin(), figure->games.end(), matches_platform_game);
            if (use != figure->games.end()) note = use->effect;
        } else if (figure) {
            if (figure->type != "Figure") note = figure->type;
            if (!figure->backups.empty()) note += note.empty() ? "Backed up" : "  /  Backed up";
        } else {
            const auto &entry = groups[index];
            note = entry.games.empty() ? count_label(entry.figures.size(), "entry", "entries") :
                                        count_label(entry.games.size(), "game", "games");
        }
        lv_label_set_text(row.note, note.c_str());
        if (!ui.show_miniatures) continue;
        lv_label_set_text(row.glyph, monogram(name).c_str());
        lv_obj_set_style_bg_color(row.badge, lv_color_hex(color_for(name)), 0);
        show_art(row.image, row.glyph, group, figure, 20, 20);
    }

    if (count == 0) {
        const bool platforms = !search && ui.grouping == Grouping::Platform;
        lv_label_set_text(ui.empty_list, search ? "No matches" : platforms ? "No games listed" : "No backups yet");
    }
    if (!count || !ui.show_miniatures) {
        ui.preview_artwork.clear();
        ui.preview_logo_failed = false;
    }
    if (!ui.show_miniatures) return;

    lv_obj_set_height(ui.art_stage, list_figures ? 65 : 53);
    lv_obj_set_y(ui.name, list_figures ? 74 : 62);
    lv_obj_set_width(ui.name, list_figures ? 95 : 115);
    visible(ui.detail, !list_figures || count == 0);
    if (count == 0) {
        lv_obj_set_height(ui.art_stage, 53);
        lv_obj_set_y(ui.name, 62);
        lv_obj_set_width(ui.name, 115);
        const bool platform_view = !search && ui.grouping == Grouping::Platform;
        lv_label_set_text(ui.name, search ? "No matches" : platform_view ? "No games listed" : "No backups yet");
        lv_label_set_text(ui.detail, search ? "Edit your search" :
                          ui.catalog.scan_error ? "Check library folder" :
                          platform_view && ui.catalog.updated.empty() ? "U downloads catalog" :
                          ui.catalog.updated.empty() ? "T rescans backups" : "Tab opens the catalog");
        lv_label_set_text(ui.art_glyph, search ? "?" : "+");
        lv_obj_set_style_bg_color(ui.art_stage, lv_color_hex(mint), 0);
        visible(ui.art_image, false);
        visible(ui.disc, true);
        visible(ui.art_glyph, true);
        visible(ui.art_status, false);
        return;
    }

    const Figure *figure = list_figures ? figure_list()[selected] : nullptr;
    const auto &group = figure ? group_name(*figure) : groups[selected].name;
    const auto &name = figure ? figure->name : group;
    lv_label_set_text(ui.name, name.c_str());
    if (!figure) {
        const auto &selected_group = groups[selected];
        const auto detail = std::to_string(selected_group.backed) + "/" +
            std::to_string(selected_group.total) + " backed up";
        lv_label_set_text(ui.detail, detail.c_str());
    }
    lv_label_set_text(ui.art_glyph, monogram(name).c_str());
    lv_obj_set_style_bg_color(ui.art_stage, lv_color_hex(color_for(group)), 0);
    show_art(ui.art_image, ui.art_glyph, group, figure, figure ? 66 : 105, figure ? 58 : 46);
    std::string focused;
    if (lv_obj_has_flag(ui.art_image, LV_OBJ_FLAG_HIDDEN)) {
        if (figure && browsing_figures() && figure->type != "Raw NFC tag") focused = figure->id;
        else if (browsing_logos()) focused = logo_key(group).string();
    }
    if (focused != ui.preview_artwork) {
        ui.preview_artwork = std::move(focused);
        ui.preview_focused_at = lv_tick_get();
        ui.preview_logo_failed = false;
    }
    if (browsing_logos() && !ui.preview_artwork.empty() && ui.download_artwork && !ui.catalog.updated.empty()) {
        if (ui.artwork_pid && ui.artwork_is_logo && ui.artwork_id == ui.preview_artwork) {
            lv_label_set_text(ui.detail, "Fetching logo");
        } else if (ui.unindexed_logos.count(ui.preview_artwork)) {
            lv_label_set_text(ui.detail, "No logo indexed");
        } else if (ui.preview_logo_failed) {
            lv_label_set_text(ui.detail, "Logo unavailable");
        }
    }
    visible(ui.disc, !lv_obj_has_flag(ui.art_glyph, LV_OBJ_FLAG_HIDDEN));
    visible(ui.art_status, figure != nullptr);
    if (figure) backup_status(ui.art_status, ui.art_check, !figure->backups.empty());
    if (change_view || animate) animate_y(ui.hero, change_view ? 48 : 45, 42, true);
}

bool presenting(NfcState state)
{
    return state == NFC_STARTING || state == NFC_PRESENTING || state == NFC_STOPPING;
}

int nfc_ripple_radius(uint32_t phase)
{
    // The final radius reaches past the opposite corners of the LCD.
    return 34 + static_cast<int>(244 * phase / nfc_ripple_duration);
}

void draw_nfc_ring(lv_layer_t *layer, uint32_t age, const NfcWave &wave)
{
    if (age >= nfc_ripple_duration) return;

    const uint32_t fade_duration = nfc_ripple_duration - nfc_ripple_fade_start;
    const uint32_t fade_remaining = nfc_ripple_duration - std::max(age, nfc_ripple_fade_start);
    const auto opacity = wave.opacity * fade_remaining / fade_duration;
    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.color = lv_color_hex(mint);
    ring.center = nfc_ripple_center;
    ring.end_angle = 360;

    // Fade the wide band and its brighter core together as the wave expands.
    ring.radius = nfc_ripple_radius(age);
    ring.width = wave.width;
    ring.opa = static_cast<lv_opa_t>(opacity / 2);
    lv_draw_arc(layer, &ring);

    ring.radius -= (wave.width - wave.core_width) / 2;
    ring.width = wave.core_width;
    ring.opa = static_cast<lv_opa_t>(opacity);
    lv_draw_arc(layer, &ring);
}

void draw_nfc_ripple(lv_event_t *event)
{
    auto *layer = lv_event_get_layer(event);
    const auto phase = ui.nfc_ripple_phase;
    for (const auto &wave : nfc_waves) {
        if (phase >= wave.delay) draw_nfc_ring(layer, phase - wave.delay, wave);
    }
}

int nfc_ripple_extent(uint32_t phase)
{
    for (const auto &wave : nfc_waves) {
        if (phase >= wave.delay && phase - wave.delay < nfc_ripple_duration) {
            return nfc_ripple_radius(phase - wave.delay);
        }
    }
    return 0;
}

int nfc_wobble_offset(uint32_t phase)
{
    for (const auto &wave : nfc_waves) {
        if (phase >= wave.delay + nfc_ripple_frame && phase < wave.delay + 4 * nfc_ripple_frame) {
            return wave.sway;
        }
    }
    return 0;
}

void set_nfc_wobble(int offset)
{
    for (auto *art : {ui.activity_image, ui.activity_glyph}) {
        lv_obj_set_style_translate_x(art, offset, 0);
    }
}

void tick_nfc_ripple(lv_timer_t *)
{
    const uint32_t phase = std::min(lv_tick_elaps(ui.nfc_ripple_started_at) % nfc_ripple_period,
                                    nfc_ripple_end);
    const uint32_t previous = ui.nfc_ripple_phase;
    if (phase == previous) return;
    ui.nfc_ripple_phase = phase;

    const int offset = nfc_wobble_offset(phase);
    if (offset != nfc_wobble_offset(previous)) set_nfc_wobble(offset);

    // Include the previous bounds so a fading ring clears cleanly.
    const int radius = std::max(nfc_ripple_extent(phase), nfc_ripple_extent(previous));
    const auto [x, y] = nfc_ripple_center;
    const lv_area_t dirty = {x - radius, y - radius, x + radius - 1, y + radius - 1};
    lv_obj_invalidate_area(ui.nfc_ripple, &dirty);
}

void update_nfc_ripple()
{
    const bool active = ui.screen == Screen::Figure && ui.nfc == NFC_PRESENTING;
    if (active == (ui.nfc_ripple_timer != nullptr)) return;

    if (active) {
        ui.nfc_ripple_started_at = lv_tick_get();
        ui.nfc_ripple_phase = 0;
        ui.nfc_ripple_timer = lv_timer_create(tick_nfc_ripple, nfc_ripple_frame, nullptr);
        if (!ui.nfc_ripple_timer) return;
    } else {
        lv_timer_delete(ui.nfc_ripple_timer);
        ui.nfc_ripple_timer = nullptr;
        if (nfc_wobble_offset(ui.nfc_ripple_phase)) set_nfc_wobble(0);
    }
    visible(ui.nfc_ripple, active);
    lv_obj_set_style_bg_opa(ui.activity, active ? LV_OPA_60 : LV_OPA_COVER, 0);
}

struct NfcLabels {
    const char *status;
    const char *detail;
};

NfcLabels nfc_labels(NfcState state)
{
    switch (state) {
    case NFC_UNAVAILABLE: return {"NFC UNAVAILABLE", "NFC needs the Zero"};
    case NFC_CHECKING: return {"NFC CHECKING", "Checking the Cap"};
    case NFC_READY: return {"NFC READY", "Present saved backup"};
    case NFC_TOP_USB: return {"NFC TOP USB", "Set top port to GPIO"};
    case NFC_POWER_OFF: return {"NFC POWER OFF", "Enable EXT 5V"};
    case NFC_SPI_MISSING: return {"NFC SPI MISSING", "Load the SPI overlay"};
    case NFC_NO_CAP: return {"NFC NO CAP", "Check Cap setup"};
    case NFC_PERMISSION: return {"NFC NO ACCESS", "Check permissions"};
    case NFC_BUSY: return {"NFC BUSY", "Close other NFC app"};
    case NFC_STARTING: return {"NFC STARTING", "Starting presentation"};
    case NFC_PRESENTING: return {"NFC PRESENTING", "Hold Cap near console"};
    case NFC_STOPPING: return {"NFC STOPPING", "Releasing the Cap"};
    case NFC_ERROR: return {"NFC ERROR", "Check Cap or logs"};
    }
    return {"NFC UNAVAILABLE", "NFC needs the Zero"};
}

void draw_nfc()
{
    update_nfc_ripple();
    const auto labels = nfc_labels(ui.nfc);
    lv_label_set_text(ui.nfc_label, labels.status);
    lv_obj_set_style_text_color(ui.nfc_label,
        lv_color_hex(ui.nfc == NFC_READY || ui.nfc == NFC_PRESENTING ? mint : amber), 0);
    if (ui.screen == Screen::Backup) {
        lv_label_set_text(ui.activity_detail, nfc_needs_retry(ui.nfc) ? labels.detail : "Tag capture unavailable");
        lv_label_set_text(ui.footer, ui.nfc == NFC_UNAVAILABLE ? "Esc Back" : "R Check NFC  Esc Back");
    }
    if (ui.screen != Screen::Figure) return;
    const bool has_backup = !figure_list()[figure_selection()]->backups.empty();
    lv_label_set_text(ui.activity_detail, ui.nfc == NFC_READY && !has_backup ? "No saved backup" : labels.detail);
    const char *controls = "F/X Browse  I Games  Esc Back";
    if (ui.nfc == NFC_READY && has_backup) controls = "F/X Browse  I Games  Enter NFC  Esc Back";
    else if (ui.nfc == NFC_STOPPING) controls = "Stopping NFC...";
    else if (presenting(ui.nfc)) controls = "Esc Stop";
    else if (nfc_needs_retry(ui.nfc)) controls = "F/X Browse  I Games  R Retry  Esc Back";
    lv_label_set_text(ui.footer, controls);
}

void update_nfc(lv_timer_t *)
{
#ifdef ZIIRO_DEVICE
    if (ui.nfc_sound_pid) {
        const auto finished = waitpid(ui.nfc_sound_pid, nullptr, WNOHANG);
        if (finished > 0 || (finished < 0 && errno == ECHILD)) ui.nfc_sound_pid = 0;
    }
#endif
    const auto state = nfc_poll();
    if (state == ui.nfc) return;
    ui.nfc = state;
    draw_nfc();
#ifdef ZIIRO_DEVICE
    if (state == NFC_PRESENTING && ui.screen == Screen::Figure && !ui.nfc_sound_pid) {
        const auto cue = ui.share / "nfc-presenting.wav";
        const char *args[] = {"aplay", "-qN", "--", cue.c_str(), nullptr};
        if (posix_spawnp(&ui.nfc_sound_pid, args[0], nullptr, nullptr,
                         const_cast<char *const *>(args), environ) != 0) ui.nfc_sound_pid = 0;
    }
#endif
}

int spawn_download(pid_t &pid, const fs::path &destination, const char *option, const char *value = nullptr,
                   const posix_spawn_file_actions_t *actions = nullptr)
{
    const auto script = ui.share / "sync_catalog.py";
    const char *python = "python3";
    const char *args[] = {python, "-I", script.c_str(), destination.c_str(), option, value, nullptr};
    const int error = posix_spawnp(&pid, python, actions, nullptr, const_cast<char *const *>(args), environ);
    if (error) pid = 0;
    return error;
}

void start_artwork_download(const std::string &key, bool logo)
{
    if (logo ? ui.preview_logo_failed || ui.unindexed_logos.count(key) : ui.artwork_failures.count(key)) return;
    const auto &destination = logo ? ui.downloaded_logos : ui.downloaded_artwork;
    if (spawn_download(ui.artwork_pid, destination, logo ? "--logo" : "--artwork", key.c_str()) == 0) {
        ui.artwork_is_logo = logo;
        ui.artwork_id = key;
    } else if (logo) {
        ui.preview_logo_failed = true;
    } else {
        ui.artwork_failures.insert(key);
    }
}

void maybe_download_artwork(const Figure &figure)
{
    const bool missing = lv_obj_has_flag(ui.activity_image, LV_OBJ_FLAG_HIDDEN);
    const bool eligible = missing && ui.download_artwork && figure.type != "Raw NFC tag" &&
        !ui.catalog.updated.empty();
    if (eligible && !ui.artwork_pid) start_artwork_download(figure.id, false);
    const bool pending = ui.artwork_pid && ui.artwork_id == figure.id;
    const bool failed = !pending && ui.artwork_failures.count(figure.id) != 0;
    visible(ui.artwork_hint, eligible && (pending || failed));
    lv_label_set_text(ui.artwork_hint, pending ? "Fetching artwork" : "Art unavailable");
}

void poll_artwork(lv_timer_t *)
{
    if (!ui.artwork_pid) {
        if (ui.download_artwork && !ui.batch_pid && (browsing_logos() || browsing_figures()) && !ui.catalog.updated.empty() &&
            !ui.preview_artwork.empty() && lv_tick_elaps(ui.preview_focused_at) >= 500) {
            const bool logo = browsing_logos();
            const bool failed_before = ui.preview_logo_failed;
            start_artwork_download(ui.preview_artwork, logo);
            if (logo && (ui.artwork_pid || failed_before != ui.preview_logo_failed)) draw_browser(false);
        }
        return;
    }
    int status = 0;
    const pid_t finished = waitpid(ui.artwork_pid, &status, WNOHANG);
    if (finished == 0 || (finished < 0 && errno == EINTR)) return;
    ui.artwork_pid = 0;
    const int result = finished > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const bool saved = result == 0;
    if (ui.artwork_is_logo) {
        if (result == 3 && ui.download_artwork) ui.unindexed_logos.insert(ui.artwork_id);
        if (browsing_logos()) {
            if (ui.download_artwork && !saved && result != 3 && ui.artwork_id == ui.preview_artwork) {
                ui.preview_logo_failed = true;
            }
            draw_browser(false);
            if (saved && ui.artwork_id == ui.preview_artwork) {
                ui.preview_logo_failed = true;
                lv_label_set_text(ui.detail, "Logo unavailable");
            }
        }
        if (ui.screen == Screen::Figure) maybe_download_artwork(*figure_list()[figure_selection()]);
        return;
    }
    if (!saved && ui.download_artwork) ui.artwork_failures.insert(ui.artwork_id);
    if (ui.screen == Screen::Figure) {
        const auto &figure = *figure_list()[figure_selection()];
        if (saved && figure.id == ui.artwork_id) {
            show_art(ui.activity_image, ui.activity_glyph, group_name(figure), &figure, 104, 87);
            if (lv_obj_has_flag(ui.activity_image, LV_OBJ_FLAG_HIDDEN)) ui.artwork_failures.insert(figure.id);
        }
        maybe_download_artwork(figure);
    } else if (saved && ui.show_miniatures && (ui.screen == Screen::Figures || ui.screen == Screen::Search)) {
        draw_browser(false);
    }
}

void draw_figure_activity()
{
    const Figure &figure = *figure_list()[figure_selection()];
    const auto &group = group_name(figure);
    const auto local_note = local_backup_note(figure);
    const bool game_context = ui.grouping == Grouping::Platform && ui.return_to == Screen::Figures;
    const auto &caption = game_context ? current_figure_group().name : local_note.empty() ? figure.series : local_note;
    lv_label_set_text(ui.activity_caption, caption.c_str());
    lv_label_set_text(ui.activity_title, figure.name.c_str());
    lv_obj_set_style_bg_color(ui.activity_art, lv_color_hex(color_for(group)), 0);
    lv_label_set_text(ui.activity_glyph, monogram(figure.name).c_str());
    show_art(ui.activity_image, ui.activity_glyph, group, &figure, 104, 87);
    maybe_download_artwork(figure);
    backup_status(ui.activity_status, ui.activity_check, !figure.backups.empty());
    draw_nfc();
}

void menu_option(lv_obj_t *field, const char *name, bool selected)
{
    lv_label_set_text_fmt(field, "%s%s", selected ? "> " : "   ", name);
    lv_obj_set_style_text_color(field, lv_color_hex(selected ? mint : muted), 0);
}

void load_preferences()
{
    std::ifstream input(ui.profile / "preferences.tsv");
    for (std::string line; std::getline(input, line);) {
        if (line == "miniatures\t0") ui.show_miniatures = false;
        if (line == "artwork\t1") ui.download_artwork = true;
    }
}

bool save_preferences()
{
    auto temporary = (ui.profile / ".preferences-XXXXXX").string();
    const int fd = mkstemp(temporary.data());
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "w");
    bool complete = false;
    if (file) {
        const int written = std::fprintf(file, "miniatures\t%d\nartwork\t%d\n", ui.show_miniatures, ui.download_artwork);
        complete = std::fclose(file) == 0 && written > 0;
    } else {
        close(fd);
    }
    std::error_code error;
    if (complete) {
        fs::rename(temporary, ui.profile / "preferences.tsv", error);
        if (!error) return true;
    }
    fs::remove(temporary, error);
    return false;
}

void draw_settings()
{
    const auto updated = ui.catalog.updated.empty() ? std::string("No catalog") :
        "Updated " + ui.catalog.updated.substr(0, 10);
    lv_label_set_text(ui.settings_updated, updated.c_str());
    lv_label_set_text(ui.activity_title, "Settings");
    if (ui.sync_pid || ui.batch_pid) {
        for (auto *field : {ui.activity_detail, ui.activity_badge, ui.activity_option,
                            ui.settings_artwork, ui.settings_batch, ui.settings_logos}) {
            lv_label_set_text(field, "");
        }
        lv_obj_set_style_text_color(ui.activity_detail, lv_color_hex(ink), 0);
        if (ui.batch_pid) {
            lv_label_set_text(ui.activity_detail, ui.batch_logos ? "Downloading game and series logos" : "Downloading artwork");
            if (ui.batch_total) {
                lv_label_set_text_fmt(ui.activity_badge, "%u / %u checked", static_cast<unsigned>(ui.batch_checked),
                                      static_cast<unsigned>(ui.batch_total));
            } else {
                lv_label_set_text(ui.activity_badge, "Checking cached images...");
            }
            if (ui.batch_failed) {
                lv_label_set_text_fmt(ui.activity_option, "%u unavailable", static_cast<unsigned>(ui.batch_failed));
            }
        } else {
            lv_label_set_text(ui.activity_detail, "Checking catalog...");
        }
        lv_label_set_text(ui.footer, ui.batch_pid && ui.batch_cancelled ? "Cancelling artwork..." : "Esc Cancel");
        return;
    }
    menu_option(ui.activity_detail, ui.catalog.updated.empty() ? "Download catalog" : "Check for catalog updates",
                ui.settings_item == 0);
    menu_option(ui.activity_badge, "Rescan local backups", ui.settings_item == 1);
    menu_option(ui.activity_option, ui.show_miniatures ? "Miniature preview   On" : "Miniature preview   Off",
                ui.settings_item == 2);
    menu_option(ui.settings_artwork, ui.download_artwork ? "Download artwork   On" : "Download artwork   Off",
                ui.settings_item == 3);
    menu_option(ui.settings_batch, "Download all figure artwork", ui.settings_item == 4);
    menu_option(ui.settings_logos, "Download game & series logos", ui.settings_item == 5);
    lv_label_set_text(ui.footer, *ui.sync_status ? ui.sync_status : "F/X Select  Enter Confirm  Esc Back");
}

void start_artwork_batch(bool logos)
{
    ui.sync_status = "Could not start artwork download";
    if (ui.catalog.updated.empty()) {
        ui.sync_status = "Download the catalog first";
        return;
    }
    if (ui.artwork_pid) {
        ui.sync_status = "Artwork download in progress. Try again.";
        return;
    }
    int output[2];
    if (pipe(output) < 0) return;
    const auto &destination = logos ? ui.downloaded_logos : ui.downloaded_artwork;
    const char *option = logos ? "--batch-logos" : "--batch-artwork";
    if (fcntl(output[0], F_SETFD, FD_CLOEXEC) == 0 &&
        fcntl(output[1], F_SETFD, FD_CLOEXEC) == 0 && fcntl(output[0], F_SETFL, O_NONBLOCK) == 0) {
        posix_spawn_file_actions_t actions;
        if (posix_spawn_file_actions_init(&actions) == 0) {
            if (posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO) == 0 &&
                spawn_download(ui.batch_pid, destination, option, ui.metadata.c_str(), &actions) == 0) {
                ui.batch_output = output[0];
                ui.batch_logos = logos;
                ui.batch_cancelled = false;
                ui.batch_partial.clear();
                ui.batch_checked = ui.batch_total = ui.batch_failed = 0;
            }
            posix_spawn_file_actions_destroy(&actions);
        }
    }
    close(output[1]);
    if (!ui.batch_pid) close(output[0]);
}

void poll_artwork_batch(lv_timer_t *)
{
    if (!ui.batch_pid) return;
    int status = 0;
    const pid_t finished = waitpid(ui.batch_pid, &status, WNOHANG);
    if (finished < 0 && errno == EINTR) return;

    char buffer[256];
    for (ssize_t size; (size = read(ui.batch_output, buffer, sizeof(buffer))) > 0;) {
        ui.batch_partial.append(buffer, static_cast<std::size_t>(size));
    }
    const auto end = ui.batch_partial.rfind('\n');
    if (end != std::string::npos) {
        const auto previous = end ? ui.batch_partial.rfind('\n', end - 1) : std::string::npos;
        const auto start = previous == std::string::npos ? 0 : previous + 1;
        std::istringstream progress(ui.batch_partial.substr(start, end - start));
        progress >> ui.batch_checked >> ui.batch_total >> ui.batch_failed;
        ui.batch_partial.erase(0, end + 1);
    }
    if (finished != 0) {
        close(ui.batch_output);
        ui.batch_output = -1;
        ui.batch_pid = 0;
        if (ui.batch_logos) ui.preview_logo_failed = false;
        else ui.artwork_failures.clear();
        const int result = finished > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        switch (result) {
        case 0: ui.sync_status = ui.batch_logos ? "All indexed logos cached" : "All artwork cached"; break;
        case 3: ui.sync_status = "No logos indexed for this catalog"; break;
        case 4: ui.sync_status = "Artwork cache full"; break;
        case 5: ui.sync_status = "Some artwork unavailable. Run again to retry."; break;
        default: ui.sync_status = "Artwork download failed. Run again to retry."; break;
        }
        if (ui.batch_cancelled && result != 0) ui.sync_status = "Artwork paused. Run again to resume.";
    }
    if (end != std::string::npos || finished != 0) draw_settings();
}

void draw_sync_status()
{
    if (ui.screen == Screen::Settings) {
        draw_settings();
        return;
    }
    lv_label_set_text(ui.view, "SETUP");
    lv_label_set_text(ui.activity_caption, *ui.sync_status ? ui.sync_status : "FIRST RUN");
    lv_label_set_text(ui.activity_title, "Welcome to Ziiro");
    lv_obj_set_y(ui.activity_detail, 51);
    lv_obj_set_y(ui.activity_badge, 66);
    visible(ui.activity_option, false);
    if (ui.sync_pid) {
        lv_label_set_text(ui.activity_detail, "Checking catalog and game details...");
        lv_obj_set_style_text_color(ui.activity_detail, lv_color_hex(muted), 0);
        lv_label_set_text(ui.activity_badge, "");
        lv_label_set_text(ui.footer, "Esc Cancel");
    } else if (!ui.catalog.updated.empty()) {
        menu_option(ui.activity_detail, "Browse offline catalog", true);
        lv_label_set_text(ui.activity_badge, "");
        lv_label_set_text(ui.footer, "Enter Open  Esc Quit");
    } else {
        menu_option(ui.activity_detail, "Download catalog", ui.welcome_item == 0);
        menu_option(ui.activity_badge, "Use local backups", ui.welcome_item == 1);
        menu_option(ui.activity_option, "Settings", ui.welcome_item == 2);
        visible(ui.activity_option, true);
        lv_label_set_text(ui.footer, "F/X Select  Enter Confirm  Esc Quit");
    }
}

void draw_game_activity()
{
    const auto &games = ui.game_usages;
    if (games.empty()) {
        lv_label_set_text(ui.activity_caption, "GAME COMPATIBILITY");
        lv_label_set_text(ui.activity_title, "No games listed");
        lv_label_set_text(ui.game_effect, "");
        lv_label_set_text(ui.footer, "Esc Back");
        return;
    }
    const auto &game = *games[ui.game];
    const auto caption = game.platform + "  /  " + game.access + "   " +
        std::to_string(ui.game + 1) + "/" + std::to_string(games.size());
    lv_label_set_text(ui.activity_caption, caption.c_str());
    lv_label_set_text(ui.activity_title, game.name.c_str());
    lv_label_set_text(ui.game_effect, game.effect.c_str());
    lv_label_set_text(ui.footer, "F/X Games  Esc Back");
}

void draw_activity(Screen screen)
{
    ui.screen = screen;
    lv_anim_delete(ui.selection_box, set_y);
    lv_anim_delete(ui.hero, set_y);
    visible(ui.browse, false);
    visible(ui.activity, true);
    const bool is_figure = screen == Screen::Figure;
    lv_label_set_text(ui.footer, is_figure ? "F/X Browse  Esc Back" : "Esc Back");
    visible(ui.activity_art, is_figure);
    visible(ui.activity_badge, screen == Screen::Settings || screen == Screen::Welcome);
    const bool settings = screen == Screen::Settings;
    lv_obj_set_height(ui.activity, settings ? 104 : 98);
    visible(ui.activity_option, settings);
    visible(ui.settings_artwork, settings);
    visible(ui.settings_updated, settings);
    visible(ui.settings_batch, settings);
    visible(ui.settings_logos, settings);
    visible(ui.activity_caption, !settings);
    visible(ui.activity_detail, screen != Screen::Games);
    visible(ui.game_effect, screen == Screen::Games);
    lv_obj_set_style_text_color(ui.activity_detail, lv_color_hex(muted), 0);
    const auto *title_font = is_figure || screen == Screen::Games || settings ?
        &lv_font_montserrat_18 : &lv_font_montserrat_24;
    lv_obj_set_style_text_font(ui.activity_title, title_font, 0);
    lv_obj_set_height(ui.activity_title, title_font->line_height * (is_figure || screen == Screen::Games ? 2 : 1));
    lv_obj_set_y(ui.activity_detail, is_figure ? 76 : 55);
    lv_obj_set_y(ui.activity_title, settings ? 5 : 23);
    lv_obj_set_y(ui.activity_badge, settings ? 38 : 77);
    lv_obj_set_y(ui.activity_option, settings ? 51 : 81);
    if (settings) lv_obj_set_y(ui.activity_detail, 25);
    for (auto *field : {ui.activity_caption, ui.activity_title, ui.activity_detail, ui.activity_badge}) {
        lv_obj_set_width(field, is_figure ? 155 : 272);
    }
    if (settings) lv_obj_set_width(ui.activity_title, 130);
    if (is_figure) {
        draw_figure_activity();
    } else if (screen == Screen::Games) {
        draw_game_activity();
    } else if (screen == Screen::Backup) {
        lv_label_set_text(ui.activity_caption, "BACKUP");
        lv_label_set_text(ui.activity_title, "NFC backup");
        draw_nfc();
    } else {
        draw_sync_status();
    }
    animate_y(ui.activity, settings ? 44 : 48, 42, true);
}

void show_activity(Screen screen)
{
    ui.return_to = ui.screen;
    if (screen == Screen::Settings) ui.settings_item = 0;
    draw_activity(screen);
}

bool refresh()
{
    Catalog fresh;
    if (regular(ui.metadata) && !fresh.load(ui.metadata) && !ui.catalog.updated.empty()) return false;
    fresh.scan(ui.backups);
    ui.archive_artwork.load(ui.backups, ui.artwork_cache);
    if (!ui.backups.empty()) {
        std::fprintf(stderr, "Library: %zu matched, %zu unidentified, %zu skipped%s\n",
                     fresh.matched, fresh.unknown, fresh.skipped, fresh.scan_error ? " (scan incomplete)" : "");
    }
    ui.catalog = std::move(fresh);
    ui.unindexed_logos.clear();
    ui.preview_logo_failed = false;
    ui.owned = true;
    return true;
}

void start_catalog_sync()
{
    const int error = spawn_download(ui.sync_pid, ui.metadata, ui.catalog.updated.empty() ? "--force" : nullptr);
    if (error) {
        ui.sync_pid = 0;
        ui.sync_status = "Could not start catalog sync";
        std::fprintf(stderr, "Could not start catalog sync (error %d)\n", error);
    } else {
        ui.sync_cancelled = false;
        ui.sync_status = "";
    }
    draw_sync_status();
}

void poll_catalog_sync(lv_timer_t *)
{
    if (!ui.sync_pid) return;
    int status = 0;
    const pid_t finished = waitpid(ui.sync_pid, &status, WNOHANG);
    if (finished == 0 || (finished < 0 && errno == EINTR)) return;
    ui.sync_pid = 0;
    const int result = finished > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (result == 0 && refresh() && !ui.catalog.updated.empty()) {
        const Screen screen = ui.screen;
        rebuild_groups();
        ui.screen = screen;
        ui.return_to = Screen::Groups;
        ui.sync_status = "Catalog ready offline";
    } else if (result == 3) {
        ui.sync_status = "Catalog is up to date";
    } else {
        ui.sync_status = ui.sync_cancelled ? "Sync cancelled" : "Sync failed. Check connection";
    }
    draw_sync_status();
}
}

bool studio_init(const char *profile, const char *share, const char *backups, const char *custom_artwork)
{
    std::error_code error;
    ui.profile = fs::absolute(profile, error);
    if (error) return false;
    ui.share = share;
    ui.metadata = ui.profile / "catalog.tsv";
    ui.backups = backups ? fs::absolute(backups, error).lexically_normal() : ui.profile / "amiibo_data";
    if (error) return false;
    fs::create_directories(ui.profile, error);
    if (error) return false;
    if (!backups) {
        fs::create_directories(ui.backups, error);
        if (error) return false;
    }
    ui.custom_artwork = custom_artwork ? fs::path(custom_artwork) : ui.profile / "artwork";
    ui.artwork_cache = ui.profile / ".artwork-cache";
    ui.downloaded_artwork = ui.profile / "downloaded-artwork";
    ui.downloaded_logos = ui.profile / "downloaded-logos";
    load_preferences();
    if (!refresh()) return false;
    const bool setup_required = ui.catalog.updated.empty() && !regular(ui.profile / "local-only");
    rebuild_groups();

    lv_obj_t *root = lv_screen_active();
    lv_obj_remove_style_all(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    ui.nfc_ripple = box(root, 0, 0, STUDIO_WIDTH, STUDIO_HEIGHT, background, 0);
    lv_obj_set_style_bg_opa(ui.nfc_ripple, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(ui.nfc_ripple, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.nfc_ripple, draw_nfc_ripple, LV_EVENT_DRAW_MAIN, nullptr);
    visible(ui.nfc_ripple, false);

    auto *brand = box(root, 12, 5, 27, 27, background, 5);
    auto *brand_fallback = box(brand, 0, 6, 5, 16, mint, 2);
    auto *brand_image = lv_image_create(brand);
    set_art(brand_image, brand_fallback, ui.share / "ziiro-icon.png", 27, 27);
    label(root, 44, 8, "Ziiro", &lv_font_montserrat_18, ink);
    auto *nfc_status = box(root, 94, 8, 124, 21, selection, 10);
    ui.nfc_label = label(nfc_status, 0, 0, "NFC UNAVAILABLE", &lv_font_montserrat_12, amber);
    lv_obj_center(ui.nfc_label);
    box(root, 224, 8, 84, 21, selection, 10);
    ui.view = label(root, 235, 11, "", &lv_font_montserrat_12, mint);
    box(root, 12, 34, 296, 1, line, 0);

    ui.browse = box(root, 0, 0, STUDIO_WIDTH, STUDIO_HEIGHT, background, 0);
    lv_obj_set_style_bg_opa(ui.browse, LV_OPA_TRANSP, 0);
    ui.section = short_label(ui.browse, 14, 37, section_width, &lv_font_montserrat_12, muted);
    ui.empty_list = short_label(ui.browse, 16, 58, 282, &lv_font_montserrat_12, muted);
    ui.selection_box = box(ui.browse, 11, row_start, 158, 27, selection, 6);
    for (std::size_t i = 0; i < row_count; ++i) {
        auto &row = ui.rows[i];
        const int y = row_start + row_step * static_cast<int>(i);
        row.badge = box(ui.browse, 16, y + 2, 24, 23, mint, 5);
        row.glyph = label(row.badge, 0, 0, "", &lv_font_montserrat_12, background);
        lv_obj_center(row.glyph);
        row.image = lv_image_create(row.badge);
        row.title = short_label(ui.browse, 46, y + 1, 118, &lv_font_montserrat_12, ink);
        row.note = short_label(ui.browse, 46, y + 14, 117, &lv_font_montserrat_12, muted);
    }

    ui.hero = box(ui.browse, 177, 42, 131, 98, panel, 9);
    ui.art_stage = box(ui.hero, 5, 5, 121, 53, mint, 6);
    ui.disc = box(ui.art_stage, 43, 8, 35, 35, background, 18);
    lv_obj_center(ui.disc);
    ui.art_glyph = label(ui.disc, 0, 0, "", &lv_font_montserrat_18, ink);
    lv_obj_center(ui.art_glyph);
    ui.art_image = lv_image_create(ui.art_stage);
    ui.art_status = box(ui.hero, 107, 73, 16, 16, background, 8);
    lv_obj_set_style_border_width(ui.art_status, 1, 0);
    ui.art_check = label(ui.art_status, 0, 0, LV_SYMBOL_OK, &lv_font_montserrat_12, background);
    lv_obj_center(ui.art_check);
    ui.name = short_label(ui.hero, 8, 62, 115, &lv_font_montserrat_12, ink);
    ui.detail = short_label(ui.hero, 8, 79, 115, &lv_font_montserrat_12, muted);

    ui.activity = box(root, 12, 42, 296, 98, panel, 8);
    ui.activity_caption = short_label(ui.activity, 12, 8, 272, &lv_font_montserrat_12, mint);
    ui.activity_title = short_label(ui.activity, 12, 23, 272, &lv_font_montserrat_24, ink);
    ui.activity_detail = short_label(ui.activity, 12, 55, 272, &lv_font_montserrat_12, muted);
    ui.activity_badge = short_label(ui.activity, 12, 77, 272, &lv_font_montserrat_12, amber);
    ui.activity_option = short_label(ui.activity, 12, 81, 272, &lv_font_montserrat_12, muted);
    ui.settings_artwork = short_label(ui.activity, 12, 64, 272, &lv_font_montserrat_12, muted);
    ui.settings_updated = short_label(ui.activity, 145, 8, 139, &lv_font_montserrat_12, muted);
    lv_obj_set_style_text_align(ui.settings_updated, LV_TEXT_ALIGN_RIGHT, 0);
    ui.settings_batch = short_label(ui.activity, 12, 77, 272, &lv_font_montserrat_12, muted);
    ui.settings_logos = short_label(ui.activity, 12, 90, 272, &lv_font_montserrat_12, muted);
    ui.game_effect = short_label(ui.activity, 12, 64, 272, &lv_font_montserrat_12, muted);
    lv_obj_set_height(ui.game_effect, 34);
    ui.activity_art = box(ui.activity, 173, 4, 119, 90, mint, 6);
    ui.activity_glyph = label(ui.activity_art, 0, 0, "", &lv_font_montserrat_24, background);
    lv_obj_center(ui.activity_glyph);
    ui.activity_image = lv_image_create(ui.activity_art);
    ui.artwork_hint = short_label(ui.activity_art, 6, 72, 107, &lv_font_montserrat_12, background);
    lv_obj_set_style_text_align(ui.artwork_hint, LV_TEXT_ALIGN_CENTER, 0);
    visible(ui.artwork_hint, false);
    ui.activity_status = box(ui.activity_art, 96, 5, 18, 18, background, 9);
    lv_obj_set_style_border_width(ui.activity_status, 1, 0);
    ui.activity_check = label(ui.activity_status, 0, 0, LV_SYMBOL_OK, &lv_font_montserrat_12, background);
    lv_obj_center(ui.activity_check);
    box(root, 12, 147, 296, 1, line, 0);
    ui.footer = short_label(root, 12, 152, 296, &lv_font_montserrat_12, ink);
    if (setup_required) draw_activity(Screen::Welcome);
    else draw_browser(false);
    nfc_probe();
    update_nfc(nullptr);
    lv_timer_create(update_nfc, 80, nullptr);
    lv_timer_create(poll_catalog_sync, 80, nullptr);
    lv_timer_create(poll_artwork, 80, nullptr);
    lv_timer_create(poll_artwork_batch, 150, nullptr);
    return true;
}

void studio_shutdown(void)
{
#ifdef ZIIRO_DEVICE
    if (ui.nfc_sound_pid) {
        kill(ui.nfc_sound_pid, SIGTERM);
        while (waitpid(ui.nfc_sound_pid, nullptr, 0) < 0 && errno == EINTR) {}
        ui.nfc_sound_pid = 0;
    }
#endif
    if (ui.batch_output >= 0) {
        close(ui.batch_output);
        ui.batch_output = -1;
    }
    for (pid_t *pid : {&ui.sync_pid, &ui.artwork_pid, &ui.batch_pid}) {
        if (!*pid) continue;
        kill(*pid, SIGTERM);
        while (waitpid(*pid, nullptr, 0) < 0 && errno == EINTR) {}
        *pid = 0;
    }
}

bool studio_input(StudioInput input)
{
    if (ui.batch_pid) {
        if (input == STUDIO_BACK && !ui.batch_cancelled) {
            ui.batch_cancelled = true;
            kill(ui.batch_pid, SIGTERM);
            draw_settings();
        }
        return true;
    }
    if (ui.sync_pid) {
        if (input == STUDIO_BACK) {
            ui.sync_cancelled = true;
            kill(ui.sync_pid, SIGTERM);
        }
        return true;
    }
    if (ui.screen == Screen::Welcome) {
        if (input == STUDIO_BACK) return false;
        if (input == STUDIO_SETTINGS) {
            show_activity(Screen::Settings);
            return true;
        }
        if (!ui.catalog.updated.empty()) {
            if (input == STUDIO_SELECT) {
                ui.screen = Screen::Groups;
                draw_browser(false, true);
            }
        } else if (input == STUDIO_UP || input == STUDIO_DOWN) {
            ui.welcome_item = (ui.welcome_item + (input == STUDIO_UP ? 2 : 1)) % 3;
            draw_sync_status();
        } else if (input == STUDIO_SELECT && ui.welcome_item == 1) {
            std::ofstream marker(ui.profile / "local-only");
            marker.close();
            if (!marker) {
                ui.sync_status = "Could not save setup choice";
                draw_sync_status();
                return true;
            }
            ui.screen = Screen::Groups;
            draw_browser(false, true);
        } else if (input == STUDIO_SELECT && ui.welcome_item == 2) {
            show_activity(Screen::Settings);
        } else if (input == STUDIO_SELECT) {
            start_catalog_sync();
        }
        return true;
    }
    if (ui.screen == Screen::Settings) {
        if (input == STUDIO_BACK) {
            if (ui.return_to == Screen::Welcome) draw_activity(Screen::Welcome);
            else {
                ui.screen = ui.return_to;
                draw_browser(false, true);
            }
        } else if (input == STUDIO_UP || input == STUDIO_DOWN) {
            ui.settings_item = (ui.settings_item + (input == STUDIO_UP ? settings_count - 1 : 1)) % settings_count;
            ui.sync_status = "";
            draw_sync_status();
        } else if (input == STUDIO_SELECT && ui.settings_item == 0) {
            start_catalog_sync();
        } else if (input == STUDIO_SELECT && ui.settings_item == 1) {
            if (refresh()) {
                rebuild_groups();
                ui.screen = Screen::Settings;
                if (ui.return_to != Screen::Welcome) ui.return_to = Screen::Groups;
                ui.sync_status = ui.catalog.scan_error ? "Check local backup folder" : "Local backups rescanned";
            } else {
                ui.sync_status = "Could not read the local catalog";
            }
            draw_sync_status();
        } else if (input == STUDIO_SELECT && ui.settings_item >= 4) {
            start_artwork_batch(ui.settings_item == 5);
            draw_settings();
        } else if (input == STUDIO_SELECT) {
            const bool artwork = ui.settings_item == 3;
            bool &setting = artwork ? ui.download_artwork : ui.show_miniatures;
            setting = !setting;
            if (!save_preferences()) {
                setting = !setting;
                ui.sync_status = "Could not save preferences";
            } else if (artwork) {
                ui.artwork_failures.clear();
                ui.preview_logo_failed = false;
                if (!setting && ui.artwork_pid) kill(ui.artwork_pid, SIGTERM);
                ui.sync_status = setting ? "Fetch artwork while browsing" : "Artwork downloads off";
            } else {
                ui.sync_status = "";
            }
            draw_sync_status();
        }
        return true;
    }
    if (ui.screen == Screen::Games) {
        const auto count = ui.game_usages.size();
        if (input == STUDIO_BACK) draw_activity(Screen::Figure);
        else if (count && (input == STUDIO_UP || input == STUDIO_DOWN)) {
            ui.game = (ui.game + (input == STUDIO_UP ? count - 1 : 1)) % count;
            draw_game_activity();
        }
        return true;
    }
    const bool groups = ui.screen == Screen::Groups;
    const bool platform_games = ui.screen == Screen::PlatformGames;
    const bool figures = ui.screen == Screen::Figures;
    const bool search = ui.screen == Screen::Search;
    const bool viewing_figure = ui.screen == Screen::Figure;
    if (search && input != STUDIO_BACK) {
        if (ui.editing_search) {
            if (input == STUDIO_SELECT) {
                ui.editing_search = false;
                draw_browser(false);
            }
            return true;
        }
        if (input != STUDIO_UP && input != STUDIO_DOWN && input != STUDIO_SELECT) return true;
    }
    if (!groups && !platform_games && !figures && !search) {
        if (viewing_figure && presenting(ui.nfc)) {
            if (input == STUDIO_BACK) {
                nfc_stop();
                update_nfc(nullptr);
            }
            return true;
        }
        if (viewing_figure && input == STUDIO_GAMES) {
            ui.game = 0;
            ui.game_usages.clear();
            const bool game_context = ui.grouping == Grouping::Platform && ui.return_to == Screen::Figures;
            for (const auto &usage : figure_list()[figure_selection()]->games) {
                if (!game_context || matches_platform_game(usage)) ui.game_usages.push_back(&usage);
            }
            draw_activity(Screen::Games);
            return true;
        }
        if (input == STUDIO_BACK) {
            ui.screen = ui.return_to;
            draw_browser(false, true);
            return true;
        }
        if (viewing_figure && input == STUDIO_SELECT) {
            const auto &backups = figure_list()[figure_selection()]->backups;
            if (ui.nfc == NFC_READY && !backups.empty()) nfc_present(backups.front().c_str());
            else if (nfc_needs_retry(ui.nfc)) nfc_probe();
            update_nfc(nullptr);
            return true;
        }
        if (!viewing_figure || (input != STUDIO_UP && input != STUDIO_DOWN)) return true;
    }

    switch (input) {
    case STUDIO_UP:
    case STUDIO_DOWN: {
        auto &selected = groups ? ui.group : platform_games ? ui.platform_game : figure_selection();
        const std::size_t count = groups ? ui.groups.size() : platform_games ? ui.groups[ui.group].games.size() :
            figure_list().size();
        if (!count) break;
        if (input == STUDIO_UP) selected = selected == 0 ? count - 1 : selected - 1;
        else selected = (selected + 1) % count;
        if (viewing_figure) draw_figure_activity();
        else draw_browser(true);
        break;
    }
    case STUDIO_SELECT:
        if (figures || search) {
            if (!figure_list().empty()) show_activity(Screen::Figure);
        } else if (!ui.groups.empty()) {
            if (groups && ui.grouping == Grouping::Platform) {
                ui.platform_game = 0;
                ui.screen = Screen::PlatformGames;
            } else {
                ui.screen = Screen::Figures;
            }
            ui.item = ui.first = 0;
            draw_browser(false, true);
        }
        break;
    case STUDIO_BACK:
        if (groups) return false;
        if (search) {
            ui.screen = ui.search_origin;
            ui.editing_search = false;
            ui.query.clear();
            ui.results.clear();
        } else {
            ui.screen = figures && ui.grouping == Grouping::Platform ? Screen::PlatformGames : Screen::Groups;
            ui.first = 0;
        }
        draw_browser(false, true);
        break;
    case STUDIO_VIEW:
    case STUDIO_GROUP:
    case STUDIO_REFRESH:
        if (input == STUDIO_VIEW && !ui.catalog.updated.empty()) ui.owned = !ui.owned;
        if (input == STUDIO_GROUP) {
            switch (ui.grouping) {
            case Grouping::Series: ui.grouping = Grouping::Franchise; break;
            case Grouping::Franchise: ui.grouping = Grouping::Platform; break;
            case Grouping::Platform: ui.grouping = Grouping::Series; break;
            }
        }
        if (input == STUDIO_REFRESH && !refresh()) break;
        rebuild_groups();
        draw_browser(false, true);
        break;
    case STUDIO_BACKUP:
        show_activity(Screen::Backup);
        break;
    case STUDIO_SETTINGS:
        show_activity(Screen::Settings);
        break;
    case STUDIO_SEARCH:
        ui.search_origin = ui.screen;
        ui.screen = Screen::Search;
        ui.editing_search = true;
        ui.query.clear();
        filter_search();
        draw_browser(false, true);
        break;
    case STUDIO_GAMES:
        break;
    }
    return true;
}

bool studio_search_editing(void)
{
    return ui.editing_search;
}

void studio_search_edit(char key)
{
    if (!studio_search_editing()) return;
    if (key == '\b' && !ui.query.empty()) ui.query.pop_back();
    else if (key >= ' ' && key <= '~' && ui.query.size() < 64) ui.query += key;
    else return;
    filter_search();
    draw_browser(false);
}

void studio_retry_nfc(void)
{
    if (!presenting(ui.nfc) && ui.nfc != NFC_UNAVAILABLE) {
        nfc_probe();
        update_nfc(nullptr);
    }
}
