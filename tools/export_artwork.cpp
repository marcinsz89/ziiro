#include "archive_artwork.hpp"
#include "catalog.hpp"

#include <exception>
#include <iostream>
#include <map>

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    if (argc != 5) {
        std::cerr << "Usage: ziiro-export-artwork CATALOG.tsv COLLECTION CACHE NEW_PACK\n";
        return 2;
    }

    try {
        const auto collection = fs::canonical(argv[2]);
        const fs::path output = argv[4];
        if (!fs::is_directory(collection) || fs::exists(output)) {
            std::cerr << "Use an existing collection and a new output directory.\n";
            return 1;
        }

        Catalog catalog;
        if (!catalog.load(argv[1])) {
            std::cerr << "Cannot load the catalog.\n";
            return 1;
        }
        const auto known_count = catalog.figures.size();
        catalog.scan(collection);
        if (catalog.scan_error) {
            std::cerr << "Cannot finish scanning the collection.\n";
            return 1;
        }

        ArchiveArtwork artwork;
        artwork.load(collection, argv[3]);
        std::map<fs::path, fs::path> assets;
        const auto add_image = [&assets](const fs::path &target, const fs::path &source) {
            if (!source.empty() && fs::is_regular_file(fs::symlink_status(source))) {
                assets.try_emplace(target, source);
            }
        };
        for (std::size_t i = 0; i < known_count; ++i) {
            const auto &figure = catalog.figures[i];
            if (figure.id.find_first_not_of("0123456789abcdef") != std::string::npos) {
                std::cerr << "Catalog contains an invalid figure ID.\n";
                return 1;
            }
            add_image(fs::path("figures") / (figure.id + ".png"), artwork.figure_image(figure));
            add_image(fs::path("series") / (artwork_slug(figure.series) + ".png"),
                      artwork.group_image(figure.series));
            add_image(fs::path("games") / (artwork_slug(figure.franchise) + ".png"),
                      artwork.group_image(figure.franchise));
        }
        if (assets.empty()) {
            std::cerr << "No matching thumbnails found. Check the collection and cache paths.\n";
            return 1;
        }
        for (const auto &[target, source] : assets) {
            const auto destination = output / target;
            fs::create_directories(destination.parent_path());
            fs::copy_file(source, destination);
        }
        std::cout << "Exported " << assets.size() << " thumbnails indexed by figure ID or group.\n";
    } catch (const std::exception &error) {
        std::cerr << "Artwork export failed: " << error.what() << '\n';
        return 1;
    }
}
