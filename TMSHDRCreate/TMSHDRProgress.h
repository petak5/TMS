#pragma once
#include <functional>
#include <string>

using ProgressFn = std::function<void(int, const std::string&)>;
