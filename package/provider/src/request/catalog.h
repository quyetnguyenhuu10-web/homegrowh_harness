#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace provider
{
    namespace
    {
        std::filesystem::path catalog_path()
        {
            if (const char* explicit_path = std::getenv("PROVIDER_CATALOG"))
            {
                if (*explicit_path == '\0')
                {
                    throw std::runtime_error("PROVIDER_CATALOG is empty");
                }

                return explicit_path;
            }

            std::filesystem::path directory = std::filesystem::current_path();

            while (true)
            {
                const std::filesystem::path candidate = directory / "catalog.json";

                if (std::filesystem::is_regular_file(candidate))
                {
                    return candidate;
                }

                const std::filesystem::path parent = directory.parent_path();

                if (parent == directory || parent.empty())
                {
                    break;
                }

                directory = parent;
            }

            throw std::runtime_error("catalog.json not found");
        }

    }
}
