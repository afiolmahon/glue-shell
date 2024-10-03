#include <common/command.hpp>
#include <common/util.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>

#include <fmt/format.h>
#include <fmt/std.h>
#include <string_view>

using namespace crew;
namespace fs = std::filesystem;

/** trim from both ends of string (right then left) */
inline std::string trim(std::string&& s)
{
    constexpr const char* const ws = " \t\n\r\f\v";
    s.erase(s.find_last_not_of(ws) + 1); // trim from end of string (right)
    s.erase(0, s.find_first_not_of(ws)); // trim from beginning of string (left)
    return std::move(s);
}

/** Exposes veo-specific details about a git repository */
struct Repo {
    /** @return - true if this repo uses CMake */
    bool isCmakeProject() const { return exists(gitRoot / "CMakeLists.txt"); }

    /** @return - true if this repo is veobot */
    bool isVeobot() const { return is_directory(gitRoot / "schemas"); };
    /** @return - true if this repo is cruft */
    bool isCruft() const { return is_directory(gitRoot / "app" / "vfm-ref-remapper"); };

    /** @return - location of the "override default stage" text file */
    fs::path crewConfigPath() const { return gitRoot / ".veto-stage"; }

    /** @return - the default stage override, if one exists */
    std::optional<std::string> defaultStage() const
    {
        std::ifstream ifs(crewConfigPath());
        std::string content(std::istreambuf_iterator<char>(ifs), {});
        if (content.empty()) {
            return {};
        }
        return content;
    }

    fs::path gitRoot;
};

/** @return - Repo object for the current working directory, if it is a git repo */
static std::optional<Repo> currentRepo()
{
    std::stringstream outStr;
    std::ofstream errStr("/dev/null");
    if (int result = Command("git", "rev-parse", "--show-toplevel")
                    .setOut(outStr)
                    .setErr(errStr)
                    .onError(OnError::Return)
                    .run();
            result == 0) {
        fs::path gitRoot = trim(outStr.str());
        if (gitRoot.empty()) {
            fatal("gitRoot not found");
        }
        return Repo{.gitRoot = std::move(gitRoot)};
    }
    return {};
}

struct Stage {
    enum class LookupType {
        Default = 0,
        EnvVar,
        RepoDefault,
        CliArg,
    };

    static Stage lookup(std::optional<std::string> stageOverride,
            std::optional<std::reference_wrapper<const Repo>> repo)
    {
        // specified via command line
        if (stageOverride.has_value()) {
            return {*stageOverride, LookupType::CliArg};
        }
        // specified via environment var
        if (const char* const env = std::getenv("VETO_STAGE"); env != nullptr) {
            return {std::string(env), LookupType::EnvVar};
        }

        // if we are in a repo look for stage name specified in a .veto-stage file
        if (repo.has_value()) {
            if (std::optional<std::string> repoStage = repo->get().defaultStage()) {
                return {*repoStage, LookupType::RepoDefault};
            }
        }

        return {};
    }

    std::string name{"stage"};
    LookupType lookupType{};
};

/** Wrapper for interacting with a veo-oe installation */
class VeoOe {
public:
    VeoOe(fs::path etoRootPath) :
        etoRoot(std::move(etoRootPath)) {}

    /** @return - path to the eto executable */
    fs::path etoPath() const { return etoRoot / "bin" / "eto"; }

    /** @return - a command object targeting the eto executable */
    [[nodiscard]] Command eto() { return Command{etoPath()}; }

    /** @return - path to the stage with the specified name, dir may not exist */
    fs::path pathToStage(const Stage& stage) { return etoRoot / "tmp" / "stages" / stage.name; }

    fs::path etoRoot;
};

/** @return - a VeoOe instance for the autodetected veo-oe installation */
std::optional<VeoOe> findOe()
{
    const char* env = std::getenv("ETO_ROOT");
    if (env == nullptr) {
        return {};
    }
    fs::path etoRoot{env};
    if (!std::filesystem::is_directory(etoRoot)) {
        return {};
    }
    return {std::move(etoRoot)};
}

std::string toString(const Stage::LookupType& type)
{
    switch (type) {
    case Stage::LookupType::Default:
        return "Default";
    case Stage::LookupType::EnvVar:
        return "Environment Variable";
    case Stage::LookupType::RepoDefault:
        return "Repo Default";
    case Stage::LookupType::CliArg:
        return "CliArg";
    }
    return fmt::format("Stage::LookupType({})", fmt::underlying(type));
}
std::string toString(const Stage& stage)
{
    return fmt::format("{} ({})", stage.name, toString(stage.lookupType));
}

/** Represents a build configuration */
class Build {
public:
    Build(VeoOe _oe, Repo _repo, Stage _stage) :
        oe(std::move(_oe)),
        repo(std::move(_repo)),
        stage(std::move(_stage)),
        dir(this->repo.gitRoot)
    {
        if (repo.isCmakeProject()) {
            dir = repo.gitRoot / "stage-build" / stage.name;
        }
    }

    // TODO: support error handling so that BuildError exception will be caught/logged if verbose/dry after normal output?
    /**
     * Helper for dry-run functionality which encapsulates
     * an action that potentially changes the state of the project
     */
    template <typename ActionCallable>
    void transaction(ActionCallable&& action, std::string_view description)
    {
        if (!dryRun) {
            action();
        }

        if (!dryRun && !verbose) {
            return;
        }

        std::cerr << fmt::format("{:s}: {}\n", dryRun ? "DRY" : "LOG", description);
    }

    template <typename Begin, typename End>
    [[noreturn]] void make(Begin begin, End end)
    {
        if (!is_directory(dir)) {
            fatal("build dir doesn't exist");
        }
        oe.eto()
                .args("xc", "make", "-l28", "-j" + std::to_string(numThreads))
                .args(begin, end)
                .setCurrentDir(dir)
                .setDry(dryRun)
                .setVerbose(dryRun)
                .run(RunMode::ExecPty);
        fatal("unreachable");
    }

    VeoOe oe;
    Repo repo;
    Stage stage;
    fs::path dir;
    bool verbose = false;
    bool dryRun = false;
    int numThreads = 30;
};

template <typename Begin, typename End>
void cmake(Build& build, Begin begin, End end)
{
    if (!is_directory(build.dir)) {
        fatal("build dir doesn't exist");
    }

    build.oe.eto()
            .args("xc", "cmake", "-S", build.repo.gitRoot.string(), "-B", build.dir.string())
            .setCurrentDir(build.dir)
            .setVerbose(build.verbose)
            .setDry(build.dryRun)
            .args(begin, end)
            .run(RunMode::BlockPty);

    const fs::path link = build.repo.gitRoot / "compile_commands.json";
    const fs::path target = build.dir / "compile_commands.json";

    if (is_symlink(link)) {
        build.transaction(
                [&link]() { remove(link); },
                fmt::format("removing {}", link));
    };

    build.transaction(
            [&target, &link]() {
                if (!exists(link)) {
                    create_symlink(target, link);
                } else {
                    std::cerr << "failed to update compile_commands symlink" << std::endl;
                } },
            fmt::format("symlinking compile_commands to {} from {}", link, target));
}

// TODO: document command line flags, use a more conventional --help page organization
constexpr const char* const helpText = R"(A DWIM wrapper for the eto utility
    cmake <ARGS...> - invoke cmake from the current stage build dir
    cmake-init <ARGS...> - initialize a cmake dir for the current stage
    set-stage <stage-name> - set the stage name associated with the current repo
    stage shell <ARGS...> - call eto stage shell in a more robust way
      - executed from ETO_ROOT to ensure bind-dir is found more reliably regardless of current file system position
      - stage is inferred via the same semantics as `crew install`
    install - eto stage install the current build configuration
    mk <ARGS...> - invoke make with the current stage build configuration
    test - build and run all tests
    lint - (studio only) run yarn lint
    serve - (studio only) run yarn serve
    targets - list make targets for the current stages build configuration
    status - print information about the current stage build configuration
    stage-prompt - print current stage name for use in a PS1 prompt. no output if stage is default
    update-oe - bitbake latest
)";

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    std::optional<std::string> stageName;
    bool verbose = false;
    bool dryRun = false;

    const auto currentBuildConfig = [&stageName, &verbose, &dryRun]() -> Build {
        std::optional repo = currentRepo();
        if (!repo.has_value()) {
            fatal("No project found; not in a git repo");
        }

        const auto stage = Stage::lookup(stageName, *repo);
        auto result = Build(
                [] {
                    auto result = findOe();
                    if (!result.has_value()) {
                        fatal("unable to locate veo-oe");
                    }
                    return std::move(*result);
                }(),
                *repo,
                stage);
        result.verbose = verbose;
        result.dryRun = dryRun;
        return result;
    };

    for (auto it = args.begin(); it != args.end(); ++it) {
        if (*it == "--help" || *it == "-h") {
            std::cout << helpText << std::endl;
            return 0;
        } else if (*it == "--verbose" || *it == "-v") {
            verbose = true;
        } else if (*it == "--dry-run") {
            dryRun = true;
        } else if (*it == "-n" || *it == "--name") {
            if (++it == args.end()) {
                fmt::print(std::cerr, "expected stage name following {:s}", *it);
                return 1;
            }
            stageName = *it;
        } else if (*it == "cmake") {
            Build build = currentBuildConfig();
            cmake(build, ++it, args.end());
            return 0;
        } else if (*it == "stage") {
            ++it;
            const auto subcmd = *it;
            if (subcmd == "shell") {
                std::optional oe = findOe();
                if (!oe.has_value()) {
                    fmt::print(std::cerr, "unable to locate veo-oe");
                    return 0;
                }

                const auto stage = Stage::lookup(stageName, currentRepo());
                oe->eto()
                        .args("stage", "-n", stage.name, "shell")
                        // Foward remaining args to eto stage shell
                        .args(++it, args.end())
                        .setCurrentDir(oe->etoRoot)
                        .setDry(dryRun)
                        .setVerbose(dryRun)
                        .run(RunMode::ExecPty);
                return 0;
            }
            fmt::print(std::cerr, "unknown command: stage {}", subcmd);
            return 1;

        } else if (*it == "cmake-init") {
            Build build = currentBuildConfig();

            if (!build.repo.isCmakeProject()) {
                fmt::print(std::cerr, "not a cmake project");
                return 1;
            }

            if (exists(build.dir)) {
                fmt::print(std::cerr, "build dir {:?} already exists", build.dir);
                return 1;
            }

            build.transaction(
                    [&build]() { create_directories(build.dir); },
                    fmt::format("creating directory {}", build.dir));

            std::vector<std::string> cmakeArgs{
                    "-DUSE_CLANG_TIDY=NO",
                    "-DCMAKE_BUILD_TYPE=RelWithDebugInfo",
            };
            if (build.repo.isVeobot() || build.repo.isCruft()) {
                cmakeArgs.push_back("-DETO_STAGEDIR=" + build.oe.pathToStage(build.stage).string());
            }
            cmakeArgs.insert(cmakeArgs.end(), ++it, args.end()); // fwd remaining command line args
            cmake(build, cmakeArgs.begin(), cmakeArgs.end());
            return 0;
        } else if (*it == "install") {
            Build build = currentBuildConfig();
            if (!is_directory(build.dir)) {
                fmt::print(std::cerr, "build dir not found: {:?}", build.dir);
                return 1;
            }

            auto c = build.oe.eto()
                             .setDry(dryRun)
                             .setVerbose(verbose)
                             .args("stage", "-n", build.stage.name);

            if (build.repo.isCmakeProject()) {
                c.args("-b", build.dir.string());
            };

            c.args("install", "-l28", "-j" + std::to_string(build.numThreads)).run(RunMode::ExecPty);
            return 0;
        } else if (*it == "mk") {
            Build build = currentBuildConfig();
            build.make(++it, args.end());
            return 0;
        } else if (*it == "test") {
            Build build = currentBuildConfig();
            std::vector<std::string> makeArgs{
                    "all", "test", "ARGS=\"-j" + std::to_string(build.numThreads) + "\""};
            build.make(makeArgs.begin(), makeArgs.end());
            return 0;
        } else if (*it == "targets") {
            Build build = currentBuildConfig();
            if (!is_directory(build.dir)) {
                fmt::print(std::cerr, "build directory not found: {:?}", build.dir);
                return 1;
            }
            std::string cmd = "make -qp"
                              " | awk -F':' '/^[a-zA-Z0-9][^$#\\/\\t=]*:([^=]|$)/ "
                              "{split($1,A,/ /);for(i in A)print A[i]}'"
                              " | sed '/Makefile/d' | sort -u";
            Command("bash", "-c", std::move(cmd)).setCurrentDir(build.dir).run();
            return 0;
        } else if (*it == "lint") {
            Build build = currentBuildConfig();
            if (!is_directory(build.dir)) {
                fmt::print("build directory not found: {:?}", build.dir);
                return 1;
            }
            build.oe.eto()
                    .args("js", "yarn", "lint")
                    .setCurrentDir(build.dir)
                    .setDry(dryRun)
                    .setVerbose(dryRun)
                    .run(RunMode::ExecPty);
            return 0;
        } else if (*it == "serve") {
            Build build = currentBuildConfig();
            if (!is_directory(build.dir)) {
                fmt::print("build directory not found: {:?}", build.dir);
                return 1;
            }
            build.oe.eto()
                    .args("js", "yarn", "serve")
                    .setCurrentDir(build.dir)
                    .setDry(dryRun)
                    .setVerbose(dryRun)
                    .run(RunMode::ExecPty);
            return 0;
        } else if (*it == "status") {
            Build build = currentBuildConfig();
            auto& str = std::cout;
            str << "Stage:      " << toString(build.stage) << "\n"
                << "Repository: " << build.repo.gitRoot << "\n"
                << "Build Dir:  " << build.dir
                << (is_directory(build.dir) ? "\n" : " (missing)\n");
            str << "CMake:      " << (build.repo.isCmakeProject() ? "true" : "false") << std::endl;
            return 0;
        } else if (*it == "set-stage") {
            if (dryRun) { // TODO: dry run support
                fmt::print(std::cerr, "{:s} doesn't support dry run", *it);
                return 1;
            }
            // TODO: uniformly respect -n argument
            if (stageName.has_value()) {
                fmt::print(std::cerr, "{:s} doesn't support -n argument", *it);
                return 1;
            }
            std::optional<Repo> repo = currentRepo();
            if (!repo.has_value()) {
                std::cerr << "Can't update stage; not in a git repo\n";
                return 1;
            }

            if (++it == args.end()) {
                // clear default stage if no arg supplied
                remove(repo->crewConfigPath());
            } else {
                // update default stage, discarding any prior selection
                std::ofstream(repo->crewConfigPath(), std::ios::trunc) << *it;
            }
            return 0;
        } else if (*it == "stage-prompt") {
            auto stage = Stage::lookup(stageName, currentRepo());
            if (stage.lookupType != Stage::LookupType::Default) {
                std::cout << stage.name << std::endl;
            }
            return 0;
        } else if (*it == "update-oe") {
            if (dryRun) { // TODO: dry run support
                fmt::print(std::cerr, "{:s} doesn't support dry run", *it);
                return 1;
            }
            auto oe = findOe();
            if (!oe.has_value()) {
                fmt::print(std::cerr, "veo oe not found");
                return 1;
            }

            Command("git", "fetch").setCurrentDir(oe->etoRoot).run();
            Command("git", "pull").setCurrentDir(oe->etoRoot).run();
            oe->eto().args("oe", "update-layers").setCurrentDir(oe->etoRoot).run();
            Command(oe->etoPath(), "oe", "bitbake", "veo-sysroots", "root-image")
                    .setCurrentDir(oe->etoRoot)
                    .run(RunMode::ExecPty);
            return 1; // unreachable
        } else {
            fmt::print(std::cerr, "unknown argument: {:?}", *it);
            return 1;
        }
    }
    return 0;
}
