#include "core/TestConfig.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <utility>

namespace snx::core::testconfig {

namespace {

constexpr std::size_t kMaxServerUrlLength = 2048;  // a server address, not a document (S6)

bool cleanPath(const std::string& path) {
    return !path.empty() && path.front() == '/' && path.size() <= kMaxServerUrlLength &&
           path.find_first_of(" \t\r\n") == std::string::npos;
}

// "imageBench": {"paths": [...], "repeat": n}. All or nothing: anything odd leaves the benchmark off.
void parseBench(const nlohmann::json& bench, TestConfig& config) {
    const auto paths = bench.is_object() ? bench.find("paths") : bench.end();
    const auto repeat = bench.is_object() ? bench.find("repeat") : bench.end();
    if (paths == bench.end() || repeat == bench.end() || !paths->is_array() || !repeat->is_number_integer()) {
        config.problems.push_back("test config: \"imageBench\" needs \"paths\" (a list) and \"repeat\" (a number)");
        return;
    }
    const auto times = repeat->get<std::int64_t>();
    if (paths->empty() || paths->size() > kMaxBenchPaths || times < 1 || times > kMaxBenchRepeat) {
        config.problems.push_back("test config: \"imageBench\" takes 1 to 8 paths and a repeat of 1 to 20");
        return;
    }
    std::vector<std::string> clean;
    for (const auto& path : *paths) {
        if (!path.is_string() || !cleanPath(path.get<std::string>())) {
            config.problems.push_back("test config: every \"imageBench.paths\" entry must be a path starting with /");
            return;
        }
        clean.push_back(path.get<std::string>());
    }
    config.benchPaths = std::move(clean);
    config.benchRepeat = static_cast<int>(times);
}

}  // namespace

const std::vector<std::pair<Drill, std::string_view>>& drills() {
    static const std::vector<std::pair<Drill, std::string_view>> kDrills = {
        {Drill::NullWrite, "null-write"},
        {Drill::Misaligned, "misaligned"},
        {Drill::WorkerNullWrite, "worker-null-write"},
        {Drill::WorkerStackOverflow, "worker-stack-overflow"},
        {Drill::Terminate, "terminate"},
        {Drill::GpuAbort, "gpu-abort"},
        {Drill::Oom, "oom"},
        {Drill::MainLoopException, "main-loop-exception"},
        {Drill::Watchdog, "watchdog"},
        {Drill::CacheWriteExit, "cache-write-exit"},
    };
    return kDrills;
}

std::string_view drillName(Drill drill) {
    for (const auto& [d, name] : drills()) {
        if (d == drill) {
            return name;
        }
    }
    return "none";
}

Drill drillFromName(std::string_view name) {
    for (const auto& [d, n] : drills()) {
        if (n == name) {
            return d;
        }
    }
    return Drill::None;
}

TestConfig parse(std::string_view json) {
    TestConfig config;
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        config.problems.push_back("test config: not a JSON object");
        return config;
    }
    if (const auto it = root.find("drill"); it != root.end()) {
        if (!it->is_string()) {
            config.problems.push_back("test config: \"drill\" must be a string");
        } else {
            const std::string name = it->get<std::string>();
            config.drill = drillFromName(name);
            if (config.drill == Drill::None) {
                config.problems.push_back("test config: unknown drill \"" + name + "\"");
            }
        }
    }
    if (const auto server = root.find("server"); server != root.end()) {
        const auto url = server->is_object() ? server->find("url") : server->end();
        if (!server->is_object()) {
            config.problems.push_back("test config: \"server\" must be an object");
        } else if (url != server->end()) {
            std::string text = url->is_string() ? url->get<std::string>() : std::string();
            const bool http = text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0;
            const bool clean = text.size() <= kMaxServerUrlLength &&
                               text.find_first_of(" \t\r\n") == std::string::npos;
            if (!http || !clean) {
                config.problems.push_back("test config: \"server.url\" must be an http:// or https:// URL");
            } else {
                while (!text.empty() && text.back() == '/') {
                    text.pop_back();
                }
                config.serverUrl = text;
            }
        }
    }
    if (const auto bench = root.find("imageBench"); bench != root.end()) {
        parseBench(*bench, config);
    }
    return config;
}

}  // namespace snx::core::testconfig
