#include "../lib/include/command.hpp"
#include "../lib/include/util.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>

#include <fmt/format.h>
#include <fmt/std.h>

using namespace crew;
namespace fs = std::filesystem;

struct Repo {
    static std::optional<Repo> current()
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

    bool isVeobot() const { return is_directory(gitRoot / "schemas"); };
    bool isCruft() const { return is_directory(gitRoot / "app" / "vfm-ref-remapper"); };
    fs::path stageFilePath() const { return gitRoot / ".veto-stage"; }

    bool isCmakeProject() const
    {
        return exists(gitRoot / "CMakeLists.txt");
    }

    std::optional<std::string> defaultStage() const
    {
        std::ifstream ifs(stageFilePath());
        std::string content(std::istreambuf_iterator<char>(ifs), {});
        if (content.empty()) {
            return {};
        }
        return content;
    }

    /**
     * Update the repo's default stage
     * @param stageName - stage to use as repo default
     */
    void setDefaultStage(const std::string& stageName)
    {
        fs::remove(stageFilePath());
        if (!stageName.empty()) {
            std::ofstream(stageFilePath()) << stageName;
        }
    }

    fs::path gitRoot;
};

struct Stage {
    enum class Type {
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
            return {*stageOverride, Type::CliArg};
        }
        // specified via environment var
        if (const char* const env = std::getenv("VETO_STAGE"); env != nullptr) {
            return {std::string(env), Type::EnvVar};
        }

        // if we are in a repo look for stage name specified in a .veto-stage file
        if (repo.has_value()) {
            if (std::optional<std::string> repoStage = repo->get().defaultStage()) {
                return {*repoStage, Type::RepoDefault};
            }
        }

        return {};
    }

    std::string name{"stage"};
    Type type{};
};

class VeoOe {
public:
    VeoOe(fs::path etoRootPath) :
        etoRoot(std::move(etoRootPath)) {}

    static std::optional<VeoOe> find()
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

    [[nodiscard]] Command eto()
    {
        return Command{etoRoot / "bin" / "eto"};
    }

    fs::path pathToStage(const Stage& stage)
    {
        return etoRoot / "tmp" / "stages" / stage.name;
    }

    [[noreturn]] void updateOe()
    {
        Command("git", "fetch").setCurrentDir(etoRoot).run();
        Command("git", "pull").setCurrentDir(etoRoot).run();
        eto().args("oe", "update-layers")
                .setCurrentDir(etoRoot)
                .run();
        Command(etoRoot / "bin" / "eto", "oe", "bitbake", "veo-sysroots", "root-image")
                .setCurrentDir(etoRoot)
                .run(RunMode::ExecPty);
        fatal("unreachable");
    }

    fs::path etoRoot;
};

std::string toString(const Stage::Type& type)
{
    switch (type) {
    case Stage::Type::Default:
        return "Default";
    case Stage::Type::EnvVar:
        return "Environment Variable";
    case Stage::Type::RepoDefault:
        return "Repo Default";
    case Stage::Type::CliArg:
        return "CliArg";
    }
    return fmt::format("Stage::Type({})", fmt::underlying(type));
}
std::string toString(const Stage& stage)
{
    return fmt::format("{} ({})", stage.name, toString(stage.type));
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
    template <typename ActionCallable, typename Description>
    void transaction(ActionCallable&& action, Description&& description)
    {
        if (!dryRun) {
            action();
        }

        if (!dryRun && !verbose) {
            return;
        }

        auto& str = std::cerr;
        str << fmt::format("{:s}: {}\n",
                dryRun ? "DRY" : "LOG",
                std::forward<Description>(description));
    }

    // print to stdout
    void targets() const
    {
        if (!is_directory(dir)) {
            fatal("build dir doesn't exist");
        }
        std::string cmd = "make -qp"
                          " | awk -F':' '/^[a-zA-Z0-9][^$#\\/\\t=]*:([^=]|$)/ "
                          "{split($1,A,/ /);for(i in A)print A[i]}'"
                          " | sed '/Makefile/d' | sort -u";
        Command("bash", "-c", std::move(cmd)).setCurrentDir(dir).run();
    }

    [[noreturn]] void install()
    {
        if (!is_directory(dir)) {
            fatal("build dir doesn't exist");
        }

        auto c = oe.eto()
                         .setDry(dryRun)
                         .setVerbose(verbose)
                         .args("stage", "-n", stage.name);

        if (repo.isCmakeProject()) {
            c.args("-b", dir.string());
        };

        c.args("install", "-l28", "-j" + std::to_string(numThreads)).run(RunMode::ExecPty);
        fatal("unreachable");
    }

    template <typename Begin, typename End>
    void cmake(Begin begin, End end)
    {
        if (!is_directory(dir)) {
            fatal("build dir doesn't exist");
        }
        oe.eto()
                .args("xc", "cmake", "-S", repo.gitRoot.string(), "-B", dir.string())
                .setCurrentDir(dir)
                .setVerbose(verbose)
                .setDry(dryRun)
                .args(begin, end)
                .run(RunMode::BlockPty);

        const fs::path link = repo.gitRoot / "compile_commands.json";
        const fs::path target = dir / "compile_commands.json";

        if (is_symlink(link)) {
            transaction(
                    [&link]() { remove(link); },
                    fmt::format("removing {}", link));
        };

        transaction(
                [&target, &link]() {
                    if (!exists(link)) {
                        create_symlink(target, link);
                    } else {
                        std::cerr << "failed to update compile_commands symlink" << std::endl;
                    } },
                fmt::format("symlinking compile_commands to {} from {}", link, target));
    }

    template <typename Begin, typename End>
    void cmakeInit(Begin begin, End end)
    {
        if (!repo.isCmakeProject()) {
            fatal("not a cmake project");
        }

        if (exists(dir)) {
            fatal("build dir {} already exists", dir);
        }

        transaction(
                [this]() { create_directories(dir); },
                fmt::format("creating directory {}", dir));

        std::vector<std::string> args{"-DUSE_CLANG_TIDY=NO", "-DCMAKE_BUILD_TYPE=RelWithDebugInfo"};
        if (repo.isVeobot() || repo.isCruft()) {
            args.push_back("-DETO_STAGEDIR=" + oe.pathToStage(stage).string());
        }

        cmake(args.begin(), args.end());
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

    [[noreturn]] void test(bool build = true)
    {
        std::vector<std::string> args;
        if (build) {
            args.push_back("all");
        }
        args.push_back("test");
        args.push_back("ARGS=\"-j" + std::to_string(numThreads) + "\"");
        make(args.begin(), args.end());
    }

    void printStatus(std::ostream& str = std::cout) const
    {
        str << "Stage:      " << toString(stage) << "\n"
            << "Repository: " << repo.gitRoot << "\n"
            << "Build Dir:  " << dir
            << (is_directory(dir) ? "\n" : " (missing)\n");
        str << "CMake:      " << (repo.isCmakeProject() ? "true" : "false") << std::endl;
    }

    VeoOe oe;
    Repo repo;
    Stage stage;
    fs::path dir;
    bool verbose = false;
    bool dryRun = false;
    int numThreads = 30;
};

// TODO: document command line flags, use a more conventional --help page organization
constexpr const char* const helpText = R"(A DWIM wrapper for the eto utility
    cmake <ARGS...> - invoke cmake from the current stage build dir
    cmake-init <ARGS...> - initialize a cmake dir for the current stage
    set-stage <stage-name> - set the stage name associated with the current repo
    install - eto stage install the current build configuration
    mk <ARGS...> - invoke make with the current stage build configuration
    test - build and run all tests
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

    const auto currentBuildConfig = [&stageName, &verbose, &dryRun]() {
        std::optional repo = Repo::current();
        if (!repo.has_value()) {
            fatal("No project found; not in a git repo");
        }

        const auto stage = Stage::lookup(stageName, *repo);
        auto result = Build(
                [] {
                    auto result = VeoOe::find();
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
        const auto& arg = *it;

        if (arg == "--help" || arg == "-h") {
            std::cout << helpText << std::endl;
            return 0;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--dry-run") {
            dryRun = true;
        } else if (arg == "-n" || arg == "--name") {
            if (++it == args.end()) {
                fatal("missing stage name after ", arg);
            }
            stageName = *it;
        } else if (arg == "cmake") {
            auto build = currentBuildConfig();
            build.cmake(++it, args.end());
            return 0;
        } else if (arg == "cmake-init") {
            auto build = currentBuildConfig();
            build.cmakeInit(++it, args.end());
            return 0;
        } else if (arg == "install") {
            currentBuildConfig().install();
            return 0;
        } else if (arg == "mk") {
            currentBuildConfig().make(++it, args.end());
            return 0;
        } else if (arg == "test") {
            currentBuildConfig().test();
            return 0;
        } else if (arg == "targets") {
            currentBuildConfig().targets();
            return 0;
        } else if (arg == "status") {
            currentBuildConfig().printStatus();
            return 0;
        } else if (arg == "set-stage") {
            if (dryRun) { // TODO: dry run support
                fatal("{:s} doesn't support dry run", arg);
            }
            std::optional<Repo> repo = Repo::current();
            if (!repo.has_value()) {
                std::cerr << "Can't update stage; not in a git repo\n";
                return 1;
            }

            if (++it == args.end()) {
                repo->setDefaultStage({}); // if no argument supplied, clear default stage
            } else {
                repo->setDefaultStage(*it);
            }
            return 0;
        } else if (arg == "stage-prompt") {
            auto stage = Stage::lookup(stageName, Repo::current());
            if (stage.type != Stage::Type::Default) {
                std::cout << stage.name << std::endl;
            }
            return 0;
        } else if (arg == "update-oe") {
            if (dryRun) { // TODO: dry run support
                fatal("{:s} doesn't support dry run", arg);
            }
            auto oe = VeoOe::find();
            if (!oe.has_value()) {
                fatal("veo oe not found");
            }
            oe->updateOe();
            return 0;
        } else {
            fatal("unknown argument \"{:s}\"", arg);
        }
    }
    return 0;
}
