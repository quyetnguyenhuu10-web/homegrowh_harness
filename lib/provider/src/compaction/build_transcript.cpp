#include "buld_transcript.h"
#include "error/capture.h"

namespace provider
{

    Result<nlohmann::json> build_transcript(const nlohmann::json& message)
    {
        try
        {
            std::string transcript;

            const auto append_content = [&](const nlohmann::json& content)
            {
                if (content.is_null())
                {
                    return;
                }

                if (content.is_string())
                {
                    transcript += content.get<std::string>();
                    return;
                }

                transcript += content.dump();
            };

            const auto append_message = [&](const nlohmann::json& item)
            {
                const std::string role = item.at("role").get<std::string>();

                if (role == "assistant")
                {
                    transcript += "<assistant>\n";

                    const auto reasoning = item.find("reasoning_content");

                    if (
                        reasoning != item.end() &&
                        !reasoning->is_null() &&
                        (!reasoning->is_string() || !reasoning->get_ref<const std::string&>().empty()))
                    {
                        transcript += "<thinking>\n";
                        append_content(*reasoning);
                        transcript += "\n</thinking>\n";
                    }

                    const auto content = item.find("content");

                    if (content != item.end() && !content->is_null())
                    {
                        append_content(*content);
                        transcript += '\n';
                    }

                    const auto tool_calls = item.find("tool_calls");

                    if (
                        tool_calls != item.end() &&
                        tool_calls->is_array() &&
                        !tool_calls->empty())
                    {
                        transcript += "<tool_call>\n";
                        transcript += tool_calls->dump();
                        transcript += "\n</tool_call>\n";
                    }

                    transcript += "</assistant>\n";
                    return;
                }

                if (role == "user")
                {
                    transcript += "<user>\n";

                    const auto content = item.find("content");

                    if (content != item.end() && !content->is_null())
                    {
                        append_content(*content);
                        transcript += '\n';
                    }

                    transcript += "</user>\n";
                    return;
                }

                if (role == "tool")
                {
                    transcript += "<user>\n<tool_results>\n";
                    transcript += item.dump();
                    transcript += "\n</tool_results>\n</user>\n";
                }
            };

            if (message.is_array())
            {
                for (const nlohmann::json& item : message)
                {
                    append_message(item);
                }
            }
            else
            {
                append_message(message);
            }

            return Result<nlohmann::json>::success(nlohmann::json{
                {"role", "user"},
                {"content", std::move(transcript)}
            });
        }
        catch (...)
        {
            return Result<nlohmann::json>::failure(error_detail::capture_exception(
                std::current_exception(), "build_transcript", {{{"messages", message}}}));
        }
    }
}
