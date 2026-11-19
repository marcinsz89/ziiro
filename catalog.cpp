#include "catalog.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace fs = std::filesystem;

namespace {
constexpr std::size_t tag_size = 540;
constexpr std::size_t id_offset = 0x54;
constexpr std::size_t id_size = 8;

std::string ascii_lower(std::string text)
{
    for (auto &ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    }
    return text;
}

bool read_id(const fs::path &path, std::string &id)
{
    // O_NONBLOCK avoids hanging if a regular file is replaced with a pipe.
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat info {};
    std::array<uint8_t, id_offset + id_size> header{};
    std::size_t consumed = 0;
    if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == tag_size) {
        while (consumed < header.size()) {
            const auto count = read(fd, header.data() + consumed, header.size() - consumed);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            consumed += static_cast<std::size_t>(count);
        }
    }
    close(fd);
    if (consumed != header.size() ||
        header[3] != (0x88 ^ header[0] ^ header[1] ^ header[2]) ||
        header[8] != (header[4] ^ header[5] ^ header[6] ^ header[7])) {
        return false;
    }

    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = id_offset; i < header.size(); ++i) {
        id += hex[header[i] >> 4];
        id += hex[header[i] & 0x0F];
    }
    return true;
}
}

bool Catalog::load(const fs::path &metadata)
{
    std::ifstream input(metadata);
    std::vector<Figure> loaded;
    std::string line;
    std::string timestamp;
    while (std::getline(input, line)) {
        if (line.compare(0, 11, "#amiiboapi\t") == 0) {
            if (!loaded.empty() || !timestamp.empty()) return false;
            timestamp = line.substr(11);
            if (timestamp.size() < 19 || timestamp.size() > 26) return false;
            continue;
        }
        std::istringstream row(line);
        Figure figure{};
        if (!std::getline(row, figure.id, '\t')) return false;
        if (figure.id == "game") {
            GameUsage game;
            if (timestamp.empty() || loaded.empty() || !std::getline(row, game.platform, '\t') ||
                !std::getline(row, game.name, '\t') || !std::getline(row, game.access, '\t') ||
                !std::getline(row, game.effect, '\t')) return false;
            loaded.back().games.push_back(std::move(game));
            continue;
        }
        if (!std::getline(row, figure.name, '\t') ||
            !std::getline(row, figure.series, '\t') || !std::getline(row, figure.franchise, '\t') ||
            !std::getline(row, figure.type) || figure.id.size() != 16 || figure.type.empty()) {
            return false;
        }
        figure.legacy = figure.id.compare(14, 2, "02") == 0;
        loaded.push_back(std::move(figure));
    }
    if (loaded.empty() || !input.eof()) return false;
    figures = std::move(loaded);
    updated = std::move(timestamp);
    return true;
}

void Catalog::scan(const fs::path &root)
{
    if (root.empty()) return;
    std::unordered_map<std::string, std::size_t> known;
    const auto catalog_count = figures.size();
    for (std::size_t i = 0; i < catalog_count; ++i) {
        known.emplace(figures[i].id, i);
    }

    std::error_code error;
    const auto root_status = fs::symlink_status(root, error);
    if (error || !fs::is_directory(root_status)) {
        scan_error = true;
        return;
    }

    fs::recursive_directory_iterator entries(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    std::vector<fs::path> candidates;
    for (; entries != end && !error; entries.increment(error)) {
        const auto &path = entries->path();
        std::error_code status_error;
        const auto status = entries->symlink_status(status_error);
        const auto name = ascii_lower(path.filename().string());
        if (fs::is_directory(status) && (name.front() == '.' || name == "!essential files")) {
            entries.disable_recursion_pending();
        }
        if (fs::is_regular_file(status) && name.front() != '.' &&
            ascii_lower(path.extension().string()) == ".bin") {
            candidates.push_back(path);
        }
    }
    scan_error = static_cast<bool>(error);
    std::sort(candidates.begin(), candidates.end());

    for (const auto &path : candidates) {
        std::string id;
        if (!read_id(path, id)) {
            ++skipped;
            continue;
        }
        const auto known_entry = known.find(id);
        if (known_entry != known.end()) {
            auto &figure = figures[known_entry->second];
            figure.backups.push_back(path);
            if (known_entry->second < catalog_count) ++matched;
            else ++unknown;
        } else {
            ++unknown;
            known.emplace(id, figures.size());
            figures.push_back({std::move(id), path.stem().string(), "Unidentified", "Unidentified",
                               "Raw NFC tag", {path}, true, {}});
        }
    }
}

std::string artwork_slug(const std::string &name)
{
    std::string slug;
    bool separator = false;
    for (const char ch : ascii_lower(name)) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            if (separator && !slug.empty()) slug += '-';
            slug += ch;
            separator = false;
        } else {
            separator = true;
        }
    }
    return slug;
}
