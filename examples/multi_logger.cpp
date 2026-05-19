#include "elog/elog.hpp"
#include "elog/registry.hpp"
#include "elog/rotating_sink.hpp"

int main() {
    auto& app = elog::get_logger("app");
    app.add_sink(elog::make_stderr_sink(true));
    app.set_level(elog::Level::INFO);

    auto& audit = elog::get_logger("audit");
    audit.add_sink(elog::make_rotating_file_sink("/tmp/elog_audit.log", 64 * 1024, 4));
    audit.set_level(elog::Level::INFO);

    app.tie(audit);

    LOGGER_F(app, elog::Level::INFO, "request {} processed", 42);
    LOGGER_F(audit, elog::Level::INFO, "user {} performed action", "alice");

    app.flush();
    audit.flush();
    return 0;
}
