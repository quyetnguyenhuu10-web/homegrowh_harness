#pragma once

#include <filesystem>
#include <string>

namespace sandbox::executable::detail
{
    std::filesystem::path path_from_utf8(const std::string& value);

    std::string path_to_utf8(const std::filesystem::path& path);
}
