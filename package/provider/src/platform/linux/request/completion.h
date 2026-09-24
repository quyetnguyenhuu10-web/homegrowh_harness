#pragma once

#include "request/completion_detail.h"

#include <cstddef>
#include <cstdint>

namespace provider::linux
{
    void completion_create(std::uintptr_t (&handles)[2]);
    void completion_destroy(std::uintptr_t (&handles)[2]) noexcept;

    std::uintptr_t completion_producer_handle(
        const std::uintptr_t (&handles)[2]) noexcept;

    void completion_post_data(
        std::uintptr_t producer_handle,
        RawResponse* response,
        std::size_t bytes);

    void completion_post_finished(
        std::uintptr_t producer_handle,
        RawResponse* response);

    void completion_post_failed(
        std::uintptr_t producer_handle,
        RawResponse* response,
        std::uint32_t error);

    bool completion_wait(
        const std::uintptr_t (&handles)[2],
        detail::NativeCompletion* completion,
        std::uint32_t timeout_ms);
}
