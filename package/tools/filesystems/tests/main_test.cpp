#include <file/reader.h>
#include <file/edit.h>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <filesystem>
#include <string>

using namespace ftxui;

int main()
{
    const std::filesystem::path file_path =
        R"(D:\homegrowh_harness\.gitignore)";

    const auto result = file::read(file_path);

    auto status =
        result.error == 0
            ? text("SUCCESS") | color(Color::Green) | bold
            : text("FAILED") | color(Color::Red) | bold;

    auto content =
        result.error == 0
            ? paragraph(result.content)
            : paragraph(
                  "Unable to read file.\n"
                  "Error code: " + std::to_string(result.error));

    auto document =
        window(
            text(" File Reader Test "),
            vbox({
                hbox({
                    text("Status : "),
                    status,
                }),

                separator(),

                hbox({
                    text("File   : "),
                    text(file_path.string()),
                }),

                separator(),

                text("Content") | bold,

                content
                    | border
                    | flex,
            }))
        | size(WIDTH, GREATER_THAN, 80)
        | size(HEIGHT, GREATER_THAN, 20);

    auto screen = Screen::Create(
        Dimension::Full(),
        Dimension::Fit(document)
    );

    Render(screen, document);

    screen.Print();

    return result.error == 0 ? 0 : 1;
}