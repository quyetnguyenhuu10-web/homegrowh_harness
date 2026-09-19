#include <fsystem>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
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

        case fsystem::EditNote::old_data_occurrences_overlap:
            return "old_data_occurrences_overlap";

        case fsystem::EditNote::timeout:
            return "timeout";
    }

    return "unknown";
}

// Một item phải có đủ 3 trường chuỗi: path / old_content / new_content.
bool parse_item(
    const json& item,
    fsystem::EditRequest& out,
    std::string& error
)
{
    for (const char* key : {"path", "old_content", "new_content"})
    {
        if (!item.contains(key) || !item.at(key).is_string())
        {
            error = std::string("Thiếu trường chuỗi: ") + key;
            return false;
        }
    }

    out.path = item.at("path").get<std::string>();
    out.old_content = item.at("old_content").get<std::string>();
    out.new_content = item.at("new_content").get<std::string>();

    return true;
}

int main(int argc, char* argv[])
{
    CLI::App edit{};

    std::filesystem::path path_toolcall;
    edit.add_option(
        "--toolcall",
        path_toolcall,
        "Nhận đường dẫn json chứa hướng dẫn sửa file (1 object hoặc 1 mảng)"
    );
    CLI11_PARSE(edit, argc, argv);

    if (path_toolcall.empty())
    {
        std::cerr << "Thiếu --toolcall <đường dẫn json>\n";
        return 1;
    }

    // 1. Đọc toàn bộ file json hướng dẫn.
    const fsystem::ReadResult read_result = fsystem::read(path_toolcall);

    if (read_result.error != 0)
    {
        std::cerr
            << "Không đọc được file json, mã lỗi: "
            << read_result.error
            << '\n';

        return 1;
    }

    // 2. Parse toàn bộ json (chấp nhận 1 object hoặc 1 mảng object).
    json workflow;

    try
    {
        workflow = json::parse(read_result.content);
    }
    catch (const std::exception& e)
    {
        std::cerr << "JSON lỗi: " << e.what() << '\n';
        return 1;
    }

    // 3. Dựng TOÀN BỘ requests trước, chưa gọi edit.
    fsystem::EditRequests requests;

    if (workflow.is_array())
        requests.reserve(workflow.size());

    const auto append_item = [&](const json& item) -> bool
    {
        fsystem::EditRequest request;
        std::string error;

        if (!parse_item(item, request, error))
        {
            std::cerr << "Item lỗi: " << error << '\n';
            return false;
        }

        requests.push_back(std::move(request));
        return true;
    };

    if (workflow.is_array())
    {
        for (const json& item : workflow)
        {
            if (!item.is_object() || !append_item(item))
                return 1;
        }
    }
    else if (workflow.is_object())
    {
        if (!append_item(workflow))
            return 1;
    }
    else
    {
        std::cerr << "JSON phải là object hoặc mảng object\n";
        return 1;
    }

    if (requests.empty())
    {
        std::cerr << "Không có request nào để sửa\n";
        return 1;
    }

    // 4. Gọi edit ĐÚNG 1 LẦN với tất cả phần tử.
    const fsystem::EditResults edit_results = fsystem::edit(requests);

    // 5. In kết quả JSON (1 mảng, cùng thứ tự requests).
    json output = json::array();

    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        if (i < edit_results.size())
        {
            const fsystem::EditResult& r = edit_results[i];

            output.push_back({
                {"path", r.path.string()},
                {"error", r.error},
                {"note", to_string(r.note)},
                {"replace_attempted", r.replace_attempted}
            });
        }
        else
        {
            output.push_back({
                {"path", requests[i].path.string()},
                {"error", 0},
                {"note", "missing_result"},
                {"replace_attempted", false}
            });
        }
    }

    std::cout << output.dump(4) << '\n';
    return 0;
}
