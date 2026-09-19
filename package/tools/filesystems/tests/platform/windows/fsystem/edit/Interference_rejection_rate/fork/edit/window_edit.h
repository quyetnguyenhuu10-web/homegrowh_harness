#pragma once
#include "fsystem/edit/edit.h"

#include "../irr_probes.h"

namespace fsystem::windows_irr
{
    EditResults edit_file(
        const EditRequests& requests,
        const IrrProbes& probes = {}
    );

    EditResult edit_file(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data,
        const IrrProbes& probes = {}
    );
}
