#include <file/reader.h>

#include <iostream>

int main()
{
    auto result = file::read("test.txt");

    if (result.error != 0)
    {
        std::cerr << "Read failed. Error: "
                  << result.error
                  << '\n';

        return 1;
    }

    std::cout << result.content;

    return 0;
}