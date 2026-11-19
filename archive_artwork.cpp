#include "archive_artwork.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
bool ends_with(const std::string &value, const std::string &suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string figure_key(const std::string &name)
{
    auto key = artwork_slug(name);
    if (ends_with(key, "-outfit")) key.resize(key.size() - 7);
    key.erase(std::remove(key.begin(), key.end(), '-'), key.end());
    return key;
}

std::string group_key(const std::string &name)
{
    const auto key = artwork_slug(name);
    return key.compare(0, 4, "the-") == 0 ? key.substr(4) : key;
}

std::string family_key(const std::string &name)
{
    auto key = group_key(name);
    if (ends_with(key, "-amiibo")) key.resize(key.size() - 7);
    return key == "super-mario-bros" ? "super-mario" : key;
}

std::string artwork_id(const std::string &key)
{
    // AmiiboAPI publishes image names in the form icon_<head>-<tail>.png.
    if (key.size() != 22 || key.compare(0, 5, "icon-") != 0 || key[13] != '-') return {};
    const auto id = key.substr(5, 8) + key.substr(14, 8);
    return id.find_first_not_of("0123456789abcdef") == std::string::npos ? id : "";
}

int scope_depth(const fs::path &ancestor, const fs::path &path)
{
    if (ancestor == ".") return 0;
    auto child = path.begin();
    int depth = 0;
    for (const auto &part : ancestor) {
        if (child == path.end() || *child != part) return -1;
        ++child;
        ++depth;
    }
    return depth;
}
}

void ArchiveArtwork::load(const fs::path &collection, const fs::path &cache)
{
    images_.clear();
    collection_ = collection;
    if (collection.empty()) return;

    std::ifstream index(cache / "index.tsv");
    std::string root;
    if (!std::getline(index, root)) return;
    if (root.compare(0, 9, "portable\t") == 0) {
        const auto folder = collection.filename().empty() ? collection.parent_path() : collection;
        if (folder.filename() != root.substr(9)) return;
    } else {
        std::error_code error;
        if (!fs::equivalent(collection, fs::path(root), error) || error) return;
    }

    std::string line;
    while (std::getline(index, line)) {
        std::istringstream row(line);
        std::string scope, name, digest;
        if (!std::getline(row, scope, '\t') || !std::getline(row, name, '\t') ||
            !std::getline(row, digest) || digest.size() != 64 ||
            digest.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
        const auto key = artwork_slug(name);
        const bool logo = ends_with(key, "-logo") || ends_with(key, "-icon");
        images_.push_back({scope, cache / "thumbs" / (digest + ".png"), name, artwork_id(key), logo});
    }
}

fs::path ArchiveArtwork::group_image(const std::string &name) const
{
    const auto key = group_key(name);
    const auto alternative = key == "super-mario-bros" ? std::string("super-mario") : key;
    fs::path fallback;
    for (const auto &image : images_) {
        if (!image.logo) continue;
        auto title = group_key(image.name);
        title.resize(title.size() - 5);
        if (title == key) return image.thumbnail;
        if (title == alternative) fallback = image.thumbnail;
    }
    return fallback;
}

fs::path ArchiveArtwork::figure_image(const Figure &figure) const
{
    if (images_.empty()) return {};
    for (const auto &image : images_) {
        if (!image.figure_id.empty() && image.figure_id == figure.id) return image.thumbnail;
    }
    const auto model_name = figure_key(figure.name);
    fs::path best;
    int best_score = -1;
    for (const auto &backup : figure.backups) {
        const auto filename = figure_key(backup.stem().string());
        const auto folder = backup.parent_path().lexically_relative(collection_);
        for (const auto &image : images_) {
            if (image.logo) continue;
            const auto depth = scope_depth(image.scope, folder);
            if (depth < 0) continue;
            const auto stem = figure_key(image.name);
            const bool matches_filename = !filename.empty() && stem == filename;
            const bool matches_name = !model_name.empty() && stem == model_name;
            if (!matches_filename && !matches_name) continue;
            const int score = 2 * depth + matches_filename;
            if (score > best_score) {
                best = image.thumbnail;
                best_score = score;
            }
        }
    }
    if (!best.empty() || model_name.empty()) return best;

    // A flat import can still use the catalog's series or franchise to find art
    // in a separate Images folder. Leave ambiguous names to the ID-based pack.
    const auto series = family_key(figure.series);
    const auto franchise = family_key(figure.franchise);
    for (const auto &image : images_) {
        if (image.logo || image.scope.empty() || figure_key(image.name) != model_name) continue;
        const auto family = family_key(image.scope.begin()->string());
        if (family != series && family != franchise) continue;
        if (!best.empty() && best != image.thumbnail) return {};
        best = image.thumbnail;
    }
    return best;
}
