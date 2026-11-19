#ifndef AMIIBO_STUDIO_CATALOG_HPP
#define AMIIBO_STUDIO_CATALOG_HPP

#include <filesystem>
#include <string>
#include <vector>

struct GameUsage {
    std::string platform;
    std::string name;
    std::string access;
    std::string effect;
};

struct Figure {
    std::string id;
    std::string name;
    std::string series;
    std::string franchise;
    std::string type;
    std::vector<std::filesystem::path> backups;
    bool legacy;
    std::vector<GameUsage> games;
};

struct Catalog {
    std::vector<Figure> figures;
    std::string updated;
    std::size_t matched = 0;
    std::size_t unknown = 0;
    std::size_t skipped = 0;
    bool scan_error = false;

    bool load(const std::filesystem::path &metadata);
    void scan(const std::filesystem::path &root);
};

std::string artwork_slug(const std::string &name);

#endif
