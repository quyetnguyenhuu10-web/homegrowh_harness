#include <sandbox_process.h>
#include <registry.h>

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    std::filesystem::path current_executable()
    {
#ifdef _WIN32
        std::wstring buffer(32768, L'\0');
        const DWORD written = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (written == 0 || written >= buffer.size())
        {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "GetModuleFileNameW");
        }
        buffer.resize(written);
        return std::filesystem::path(buffer);
#else
        std::string buffer(4096, '\0');
        const ssize_t written = readlink(
            "/proc/self/exe",
            buffer.data(),
            buffer.size());
        if (written < 0)
        {
            throw std::system_error(
                errno,
                std::generic_category(),
                "readlink(/proc/self/exe)");
        }
        buffer.resize(static_cast<std::size_t>(written));
        return std::filesystem::path(buffer);
#endif
    }
}

int main(int argc, char* argv[])
{
    const char* child = std::getenv("HOMEGROWPH_SANDBOX_PROCESS_TEST_CHILD");
    if (child != nullptr && *child != '\0')
    {
        const std::string raw{
            std::istreambuf_iterator<char>(std::cin),
            std::istreambuf_iterator<char>()};
        const nlohmann::json argument =
            nlohmann::json::parse(raw, nullptr, false);
        if (argument.is_object())
        {
            const std::string mode = argument.value("mode", std::string{});
            if (mode == "echo")
            {
                std::cout << "sandbox-process-ok";
                return 0;
            }
            if (mode == "sleep")
            {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                return 0;
            }
        }
    }

    const std::filesystem::path executable = current_executable();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / "homegrowph-harness-sandbox-process-test";
    const std::filesystem::path state_path =
        std::filesystem::temp_directory_path()
        / "homegrowph-harness-sandbox-process-test.state";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::filesystem::remove(state_path, cleanup_error);
#ifdef _WIN32
    if (_wputenv_s(
            L"HOMEGROWPH_SANDBOX_REGISTRY_STATE",
            state_path.c_str()) != 0)
    {
        throw std::runtime_error("could not set process test registry state path");
    }
    if (_wputenv_s(L"HOMEGROWPH_SANDBOX_PROCESS_TEST_CHILD", L"1") != 0)
        throw std::runtime_error("could not set process test child marker");
#else
    if (setenv(
            "HOMEGROWPH_SANDBOX_REGISTRY_STATE",
            state_path.c_str(),
            1) != 0)
    {
        throw std::system_error(
            errno,
            std::generic_category(),
            "setenv(process test registry state)");
    }
    if (setenv("HOMEGROWPH_SANDBOX_PROCESS_TEST_CHILD", "1", 1) != 0)
    {
        throw std::system_error(
            errno,
            std::generic_category(),
            "setenv(process test child marker)");
    }
#endif
    std::filesystem::create_directories(root);

    const auto registration = sandbox::registry({
        {executable, sandbox::permission::read_only},
        {root, sandbox::permission::read_write},
    }, true);
    require(!registration.final_error, "process test registry final error");
    require(
        registration.permissions.size() == 2,
        "process test registry permission count mismatch");

    sandbox::process_request normal;
    normal.executable = executable;
    normal.stdin_data = nlohmann::json{{"mode", "echo"}}.dump();
    normal.working_directory = root;
    normal.config = sandbox::config{
        sandbox::read_only(executable),
        sandbox::read_write(root),
        sandbox::network(sandbox::network_access::none),
    };
    normal.timeout = std::chrono::seconds(5);

    sandbox::process(normal);
    const auto& normal_result = normal.results;
    require(!normal_result.state.final_error, "normal sandbox process final error");
    require(!normal_result.state.config.final_error, "normal sandbox config error");
    require(normal_result.state.started, "normal sandbox process did not start");
    require(!normal_result.state.timed_out, "normal sandbox process timed out");
    require(normal_result.state.exit_code == 0, "normal sandbox process exit mismatch");
    require(
        normal_result.stdout_text == "sandbox-process-ok",
        "normal sandbox process stdout mismatch");

    sandbox::process_request timeout = normal;
    timeout.stdin_data = nlohmann::json{{"mode", "sleep"}}.dump();
    timeout.timeout = std::chrono::milliseconds(100);
    sandbox::process(timeout);
    const auto& timeout_result = timeout.results;
    require(timeout_result.state.started, "timeout sandbox process did not start");
    require(timeout_result.state.timed_out, "sandbox timeout was not reported");
    require(timeout_result.state.terminated, "sandbox timeout did not terminate process");
    require(
        !timeout_result.state.os_error_before_termination.message().empty(),
        "sandbox timeout did not capture OS error state before termination");

    std::filesystem::remove_all(root, cleanup_error);
    std::filesystem::remove(state_path, cleanup_error);
#ifdef _WIN32
    _wputenv_s(L"HOMEGROWPH_SANDBOX_REGISTRY_STATE", L"");
    _wputenv_s(L"HOMEGROWPH_SANDBOX_PROCESS_TEST_CHILD", L"");
#else
    unsetenv("HOMEGROWPH_SANDBOX_REGISTRY_STATE");
    unsetenv("HOMEGROWPH_SANDBOX_PROCESS_TEST_CHILD");
#endif
    std::cout << "sandbox process tests passed\n";
    return 0;
}
