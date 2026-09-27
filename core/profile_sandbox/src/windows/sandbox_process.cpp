#include "../executable/process_broker.h"

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <iostream>

int main()
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    return sandbox::executable::run_process_broker(std::cin, std::cout);
}
