#pragma once

#include "logger.hpp"

#include <string>
#include <vector>

namespace elog {

Logger& get_logger(const std::string& name);

Logger* find_logger(const std::string& name);

bool remove_logger(const std::string& name);

std::vector<std::string> logger_names();

}  // namespace elog
