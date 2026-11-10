// Stand-in for src/Eikon/pch.h when compiling the Core sources into the test runner: Win32 and the
// standard library, but no WinRT / Windows App SDK. This directory comes first in
// AdditionalIncludeDirectories so that `#include "pch.h"` in src/Eikon/Core/*.cpp resolves here.
#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
