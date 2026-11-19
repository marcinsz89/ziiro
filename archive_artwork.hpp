#ifndef AMIIBO_STUDIO_ARCHIVE_ARTWORK_HPP
#define AMIIBO_STUDIO_ARCHIVE_ARTWORK_HPP

#include "catalog.hpp"

class ArchiveArtwork {
public:
    void load(const std::filesystem::path &collection, const std::filesystem::path &cache);
    std::filesystem::path group_image(const std::string &name) const;
    std::filesystem::path figure_image(const Figure &figure) const;

private:
    struct Image {
        std::filesystem::path scope;
        std::filesystem::path thumbnail;
        std::string name;
        std::string figure_id;
        bool logo;
    };

    std::filesystem::path collection_;
    std::vector<Image> images_;
};

#endif
