#ifndef CREW_MODULE_HPP
#define CREW_MODULE_HPP

#include "command.hpp"

#include <filesystem>

#include <nlohmann/json.hpp>

namespace crew {

struct ModuleConfig {
    ModuleConfig() = default;
    enum class ArgKind {
        StringLiteral,
        EnvVar,
        BuiltIn,
    };
    struct Arg {
        ArgKind kind{};
        std::string value{};
    };
    struct Command {
        std::string description{};
        std::map<std::string, Arg> args{};
        std::map<std::string, Arg> vars{};
    };
    std::string name{};
    std::string description{};
    std::map<std::string, Command> commands{};
};

void from_json(const nlohmann::json& j, ModuleConfig& v);
void from_json(const nlohmann::json& j, ModuleConfig::Command& v);
void from_json(const nlohmann::json& j, ModuleConfig::Arg& v);

ModuleConfig::ArgKind argKindFromString(const std::string& str);

class ModuleInstance {
public:
    static ModuleInstance fromConfig(ModuleConfig description, std::filesystem::path bashFile);

    Command& command() { return m_command; }

private:
    ModuleInstance(ModuleConfig config, Command command) :
        m_config(std::move(config)),
        m_command(command)
    {
    }

    ModuleConfig m_config;
    Command m_command;
};

class ModuleLoader {
public:
    ModuleLoader(std::filesystem::path dataDir) :
        m_dataDir(std::move(dataDir)) {}

    ModuleInstance load(const std::string& name);

private:
    std::filesystem::path m_dataDir;
};

} // namespace crew
#endif
