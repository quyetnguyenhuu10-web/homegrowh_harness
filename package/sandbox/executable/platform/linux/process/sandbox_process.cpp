#include "../../../process/process_broker.h"

#include <iostream>

int main()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    return sandbox::executable::run_process_broker(std::cin, std::cout);
}
