#include "process_broker.h"

#include "process_protocol.h"

#include <sandbox/process.h>

#include <exception>
#include <istream>
#include <ostream>
#include <string>
#include <system_error>

namespace sandbox::executable
{
    int run_process_broker(std::istream& input, std::ostream& output)
    {
        process_request request;
        std::string parse_error;
        if (!process_protocol::read_request(input, request, parse_error))
        {
            process_result result;
            result.state.final_error =
                std::make_error_code(std::errc::invalid_argument);
            result.stderr_text = parse_error;
            return process_protocol::write_result(output, result) ? 0 : 2;
        }

        process_result result;
        try
        {
            result = process(request);
        }
        catch (const std::system_error& exception)
        {
            result.state.final_error = exception.code();
            result.stderr_text = exception.what();
        }
        catch (const std::exception& exception)
        {
            result.state.final_error =
                std::make_error_code(std::errc::invalid_argument);
            result.stderr_text = exception.what();
        }

        return process_protocol::write_result(output, result) ? 0 : 2;
    }
}
