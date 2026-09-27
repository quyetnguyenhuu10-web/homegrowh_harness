#include <events>

#include <stdexcept>
#include <system_error>

namespace events
{
    Store create(
        const std::string& id,
        const std::filesystem::path& path)
    {
        if (id.empty())
        {
            throw std::invalid_argument("events: id must not be empty");
        }

        const std::filesystem::path id_path(id);
        if (
            id_path.has_parent_path() ||
            id_path == "." ||
            id_path == "..")
        {
            throw std::invalid_argument(
                "events: id must be a file name, not a path");
        }

        if (path.empty())
        {
            throw std::invalid_argument("events: path must not be empty");
        }

        std::filesystem::create_directories(path);
        const std::filesystem::path database_path = path / (id + ".db");

        if (std::filesystem::exists(database_path))
        {
            throw std::filesystem::filesystem_error(
                "events: database already exists",
                database_path,
                std::make_error_code(std::errc::file_exists));
        }

        return Store(database_path);
    }
}
