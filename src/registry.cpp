#include "elog/registry.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace elog {

namespace {

struct Registry {
    std::mutex mu;
    std::unordered_map<std::string, std::unique_ptr<Logger>> owned;
    std::unordered_map<std::string, Logger*> all;
};

Registry& reg() {
    static Registry* r = new Registry();
    return *r;
}

}  // namespace

namespace detail {

void register_logger(const std::string& name, Logger* l) {
    auto& r = reg();
    std::lock_guard<std::mutex> lk(r.mu);
    r.all[name] = l;
}

}  // namespace detail

Logger& get_logger(const std::string& name) {
    auto& r = reg();
    std::lock_guard<std::mutex> lk(r.mu);
    auto it = r.all.find(name);
    if (it != r.all.end()) return *it->second;
    auto up = std::unique_ptr<Logger>(new Logger(name));
    Logger* p = up.get();
    r.owned.emplace(name, std::move(up));
    r.all[name] = p;
    return *p;
}

Logger* find_logger(const std::string& name) {
    auto& r = reg();
    std::lock_guard<std::mutex> lk(r.mu);
    auto it = r.all.find(name);
    return it == r.all.end() ? nullptr : it->second;
}

bool remove_logger(const std::string& name) {
    auto& r = reg();
    std::lock_guard<std::mutex> lk(r.mu);
    bool erased_owned = r.owned.erase(name) > 0;
    bool erased_alias = r.all.erase(name) > 0;
    return erased_owned || erased_alias;
}

std::vector<std::string> logger_names() {
    auto& r = reg();
    std::lock_guard<std::mutex> lk(r.mu);
    std::vector<std::string> names;
    names.reserve(r.all.size());
    for (auto& kv : r.all) names.push_back(kv.first);
    return names;
}

}  // namespace elog
