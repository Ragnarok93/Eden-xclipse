// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace AndroidPgo {
// Returns a result only after checking the workload output; throws on failure.
std::string RunWorkload(int stage);
}
