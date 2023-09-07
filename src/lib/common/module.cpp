#include <common/module.hpp>

#include <fstream>

#include <fmt/format.h>

namespace fs = std::filesystem;

namespace crew {
void from_json(const nlohmann::json& j, ModuleConfig& v)
{
    v = {};
    v.name = j.at("name");
    if (j.contains("description")) {
        v.description = j.at("description");
    }
    v.commands = j.at("commands");
}

void from_json(const nlohmann::json& j, ModuleConfig::Command& v)
{
    v = {};
    v.description = j.at("description");
    v.args = j.at("args");
    v.vars = j.at("vars");
}

void from_json(const nlohmann::json& j, ModuleConfig::Arg& v)
{
    v = {};
    v.kind = argKindFromString(j.at("kind"));
    v.value = j.at("value");
}

ModuleConfig::ArgKind argKindFromString(const std::string& str)
{
    if (str == "Environment") {
        return ModuleConfig::ArgKind::EnvVar;
    }
    if (str == "StringLiteral") {
        return ModuleConfig::ArgKind::StringLiteral;
    }
    if (str == "BuiltIn") {
        return ModuleConfig::ArgKind::BuiltIn;
    }
    fatal("unknown arg kind: ", str);
}

ModuleInstance ModuleInstance::fromConfig(ModuleConfig description, std::filesystem::path bashFile)
{
    return {std::move(description),
            Command("bash", "--init-file", bashFile)
                    .setCurrentDir(fs::current_path())};
}

ModuleInstance ModuleLoader::load(const std::string& name)
{
    const fs::path bashPath = m_dataDir / name / (name + ".sh");
    const fs::path envPath = m_dataDir / name / (name + ".env");
    if (!std::filesystem::exists(bashPath) || !std::filesystem::exists(envPath)) {
        fatal("module or bash file doesn't exist");
    }
    std::ifstream f(envPath);
    nlohmann::json config = nlohmann::json::parse(
            std::string(std::istreambuf_iterator<char>(f), {}));
    return ModuleInstance::fromConfig(
            config,
            bashPath);
}
} // namespace crew
