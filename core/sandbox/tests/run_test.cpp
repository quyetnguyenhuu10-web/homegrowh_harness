#include <sandbox>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
    const std::filesystem::path marker =
        std::filesystem::temp_directory_path() / "homegrowh_sandbox_run_test.txt";
    std::filesystem::remove(marker);

#ifdef _WIN32
    std::string path = marker.string();
    std::size_t position = 0;
    while ((position = path.find('\'', position)) != std::string::npos)
    {
        path.insert(position, 1, '\'');
        position += 2;
    }
    const std::string body =
        "[System.IO.File]::WriteAllText('" + path + "', 'worker'); exit 37";
#else
    std::string path = marker.string();
    std::size_t position = 0;
    while ((position = path.find('\'', position)) != std::string::npos)
    {
        path.replace(position, 1, "'\\''");
        position += 4;
    }
    const std::string body = "printf worker > '" + path + "'; exit 37";
#endif

    const int result = sandbox::run(body);

    assert(result == 37);

    std::ifstream input(marker, std::ios::binary);
    std::string content;
    input >> content;
    assert(content == "worker");

    std::filesystem::remove(marker);
    return 0;
}
