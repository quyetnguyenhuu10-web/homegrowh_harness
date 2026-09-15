#include <fsystem>
#include <iostream>
#include <string>
#include <filesystem>
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

const char* to_string(fsystem::EditNote note)
{
    switch (note)
    {
        case fsystem::EditNote::none:
            return "none";

        case fsystem::EditNote::old_data_not_found:
            return "old_data_not_found";

        case fsystem::EditNote::old_data_appears_more_than_once:
            return "old_data_appears_more_than_once";

        case fsystem::EditNote::file_changed:
            return "file_changed";
    }

    return "unknown";
}

int main(int argc, char* argv[])
{
    CLI::App edit{};
    
    std::filesystem::path path_toolcall;
    edit.add_option("--toolcall", path_toolcall, "Nhận đường dẫn json chứa hướng dẫn sửa file");
    CLI11_PARSE(edit, argc, argv);
    auto read_result = fsystem::read(path_toolcall);

    json workflow = nlohmann::json::parse(read_result.content);

    std::string old_content = workflow["old_content"];
    std::string new_content = workflow["new_content"];
    std::filesystem::path edit_path = workflow["path"];

    auto edit_result = fsystem::edit(edit_path, old_content, new_content);

    std::cout << "=================OLD CONTENT====================\n";
    std::cout << edit_result.old_content << '\n';
    std::cout << "================================================\n";

    std::cout << "=================NEW CONTENT====================\n";
    std::cout << edit_result.new_content << '\n';
    std::cout << "================================================\n";

    std::cout << "Mã lỗi: " << edit_result.error << '\n';
    std::cout << "Ghi chú: " << to_string(edit_result.note) << '\n';
    return 0;
}
