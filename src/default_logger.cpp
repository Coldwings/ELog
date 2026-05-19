#include "elog/logger.hpp"

namespace elog {
namespace detail {

void register_logger(const std::string& name, Logger* l);

Logger* default_logger_init() {
    Logger* l = new Logger("default");
    l->set_level(Level::INFO);
    l->add_sink(make_stderr_sink(true));
    register_logger("default", l);
    return l;
}

}  // namespace detail
}  // namespace elog
