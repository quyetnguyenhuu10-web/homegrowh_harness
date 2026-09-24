#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace provider
{
    namespace
    {
        enum class UsageProvider
        {
            openai,
            deepseek,
            bonsai
        };

        std::filesystem::path catalog_path()
        {
            if (const char* explicit_path = std::getenv("PROVIDER_CATALOG"))
            {
                if (*explicit_path == '\0')
                {
                    throw std::runtime_error("PROVIDER_CATALOG is empty");
                }

                return explicit_path;
            }

            std::filesystem::path directory = std::filesystem::current_path();

            while (true)
            {
                const std::filesystem::path candidate = directory / "catalog.json";

                if (std::filesystem::is_regular_file(candidate))
                {
                    return candidate;
                }

                const std::filesystem::path parent = directory.parent_path();

                if (parent == directory || parent.empty())
                {
                    break;
                }

                directory = parent;
            }

            throw std::runtime_error("catalog.json not found");
        }

        UsageProvider provider_for_model(const std::string& model_id)
        {
            std::ifstream file(catalog_path());

            if (!file)
            {
                throw std::runtime_error("cannot open catalog.json");
            }

            nlohmann::json catalog;
            file >> catalog;

            std::optional<UsageProvider> selected;

            for (const auto& [provider_name, provider_entry] : catalog.items())
            {
                if (!provider_entry.is_object())
                {
                    throw std::runtime_error("catalog provider entry is not an object");
                }

                const nlohmann::json& models = provider_entry.at("models");

                if (!models.is_array())
                {
                    throw std::runtime_error("catalog provider models is not an array");
                }

                for (const nlohmann::json& model : models)
                {
                    if (model.at("id").get<std::string>() != model_id)
                    {
                        continue;
                    }

                    UsageProvider current;

                    if (provider_name == "openai")
                    {
                        current = UsageProvider::openai;
                    }
                    else if (provider_name == "deepseek")
                    {
                        current = UsageProvider::deepseek;
                    }
                    else if (provider_name == "bonsai")
                    {
                        current = UsageProvider::bonsai;
                    }
                    else
                    {
                        throw std::runtime_error(
                            "unsupported provider in catalog: " + provider_name);
                    }

                    if (selected.has_value())
                    {
                        throw std::runtime_error(
                            "duplicate model id in catalog: " + model_id);
                    }

                    selected = current;
                }
            }

            if (!selected.has_value())
            {
                throw std::runtime_error(
                    "model id not found in catalog: " + model_id);
            }

            return *selected;
        }
    }
}
