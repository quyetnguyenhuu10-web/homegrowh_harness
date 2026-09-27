#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace events
{
    struct EventInput
    {
        std::string events;
        std::optional<std::string> create_at;
        std::string session_id;
        std::string provider;
        std::string model;
    };

    struct Event
    {
        std::int64_t row_position = 0;
        std::string events;
        std::string create_at;
        std::string session_id;
        std::string provider;
        std::string model;
    };

    class Store
    {
    public:
        explicit Store(const std::filesystem::path& database_path);
        ~Store();

        Store(const Store&) = delete;
        Store& operator=(const Store&) = delete;
        Store(Store&&) noexcept;
        Store& operator=(Store&&) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        friend std::int64_t append(Store&, const EventInput&);
        friend bool erase(Store&, std::int64_t);
        friend std::vector<Event> query(const Store&);
        friend std::vector<Event> query(const Store&, const std::string&);
        friend std::int64_t insert_after(Store&, std::int64_t, const EventInput&);
    };

    Store create(
        const std::string& id,
        const std::filesystem::path& path);

    std::int64_t append(Store& store, const EventInput& event);

    bool erase(Store& store, std::int64_t row_position);

    std::vector<Event> query(const Store& store);

    std::vector<Event> query(
        const Store& store,
        const std::string& session_id);

    std::int64_t insert_after(
        Store& store,
        std::int64_t row_position,
        const EventInput& event);
}
