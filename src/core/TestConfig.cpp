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

// "server": {"url", "auth", "username", "password"}. A bad entry is left out and named in `problems`.
void parseServer(const nlohmann::json& server, TestConfig& config) {
    if (!server.is_object()) {
        config.problems.push_back("test config: \"server\" must be an object");
        return;
    }
    if (const auto url = server.find("url"); url != server.end()) {
        std::string text = url->is_string() ? url->get<std::string>() : std::string();
        const bool http = text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0;
        const bool clean = text.size() <= kMaxServerUrlLength && text.find_first_of(" \t\r\n") == std::string::npos;
        if (!http || !clean) {
            config.problems.push_back("test config: \"server.url\" must be an http:// or https:// URL");
        } else {
            while (!text.empty() && text.back() == '/') {
                text.pop_back();
            }
            config.serverUrl = text;
        }
    }
    if (const auto auth = server.find("auth"); auth != server.end()) {
        const auto mode = auth->is_string() ? authModeFromName(auth->get<std::string>()) : std::nullopt;
        if (!mode) {
            config.problems.push_back("test config: \"server.auth\" must be none, basic or ui_login");
        }
        config.serverAuth = mode;
    }
    const auto credential = [&](const char* key, std::string& out) {
        const auto it = server.find(key);
        if (it == server.end()) {
            return;
        }
        if (!it->is_string() || it->get<std::string>().size() > kMaxCredentialLength) {
            config.problems.push_back(std::string("test config: \"server.") + key + "\" must be a short string");
            return;
        }
        out = it->get<std::string>();
    };
    credential("username", config.serverUsername);
    credential("password", config.serverPassword);
}

// "diagnostics": true, or {"burst": n}.
void parseDiagnostics(const nlohmann::json& diagnostics, TestConfig& config) {
    if (diagnostics.is_boolean()) {
        config.diagnostics = diagnostics.get<bool>();
        return;
    }
    const auto burst = diagnostics.is_object() ? diagnostics.find("burst") : diagnostics.end();
    if (!diagnostics.is_object() || (burst != diagnostics.end() && !burst->is_number_integer())) {
        config.problems.push_back("test config: \"diagnostics\" must be true or {\"burst\": n}");
        return;
    }
    config.diagnostics = true;
    if (burst != diagnostics.end()) {
        const auto n = burst->get<std::int64_t>();
        if (n < 0 || n > kMaxBurst) {
            config.problems.push_back("test config: \"diagnostics.burst\" must be 0 to 100");
            return;
        }
        config.diagnosticsBurst = static_cast<int>(n);
    }
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
        parseServer(*server, config);
    }
    if (const auto setup = root.find("showSetup"); setup != root.end()) {
        if (setup->is_boolean()) {
            config.showSetup = setup->get<bool>();
        } else {
            config.problems.push_back("test config: \"showSetup\" must be true or false");
        }
    }
    if (const auto diagnostics = root.find("diagnostics"); diagnostics != root.end()) {
        parseDiagnostics(*diagnostics, config);
    }
    if (const auto bench = root.find("imageBench"); bench != root.end()) {
        parseBench(*bench, config);
        config.diagnostics = config.diagnostics || config.benchRepeat > 0;
    }
    return config;
}

}  // namespace snx::core::testconfig
