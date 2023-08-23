#include "../lib/include/command.hpp"
#include "../lib/include/util.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>

using namespace crew;
namespace fs = std::filesystem;

struct Repo {
    static std::optional<Repo> current()
    {
        std::stringstream outStr;
        std::ofstream errStr("/dev/null");
        if (int result = Command("git")
                                 .args("rev-parse", "--show-toplevel")
                                 .run(outStr, errStr);
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

    void updateOe()
    {
        Command("git").arg("fetch").setCurrentDir(etoRoot).run();
        Command("git").arg("pull").setCurrentDir(etoRoot).run();
        if (int e = eto().args("oe", "update-layers").setCurrentDir(etoRoot).run(); e != 0) {
            fatal("oe update-layers failed with status ", e);
        }
        // TODO(antonio): configure PTY so output isn't scrunched up
        if (Command(etoRoot / "bin" / "eto")
                        .args("oe", "bitbake", "veo-sysroots", "root-image")
                        .setCurrentDir(etoRoot)
                        .runPty()
                != 0) {
            fatal("bitbake failed");
        }
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
    return "Stage::Type("
            + std::to_string(static_cast<std::underlying_type_t<Stage::Type>>(type))
            + ")";
}
std::string toString(const Stage& stage)
{
    return stage.name + " (" + toString(stage.type) + ")";
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
        Command("bash").args("-c", std::move(cmd)).setCurrentDir(dir).run();
    }

    void install()
    {
        if (!is_directory(dir)) {
            fatal("build dir doesn't exist");
        }

        auto c = oe.eto();
        c.args("stage", "-n", stage.name);

        if (repo.isCmakeProject()) {
            c.args("-b", dir.string());
        };

        c.args("install", "-l28", "-j" + std::to_string(numThreads));
        c.runPty();
    }

    void test()
    {
        // make all && make test ARGS="-j30"
    }

    void cmake(std::vector<std::string> extraArgs)
    {
        if (!is_directory(dir)) {
            fatal("build dir doesn't exist");
        }
        auto c = oe.eto().args("xc", "cmake", "-S", repo.gitRoot.string(), "-B", dir.string());
        for (auto& a : extraArgs) {
            c.arg(std::move(a));
        }
        if (int e = c.setCurrentDir(dir)
                            .setVerbose(verbose)
                            .runPty();
                e != 0) {
            fatal("command failed with non-zero exit status: ", e);
        }

        // update compile commands symlink
        const fs::path link = repo.gitRoot / "compile_commands.json";
        const fs::path target = dir / "compile_commands.json";

        if (is_symlink(link)) {
            remove(link);
        }

        if (!exists(link)) {
            create_symlink(target, link);
        } else {
            std::cerr << "failed to update compile_commands symlink" << std::endl;
        }
    }

    void cmakeInit(const std::vector<std::string>& extraArgs)
    {
        if (!repo.isCmakeProject()) {
            fatal("not a cmake project");
        }

        if (exists(dir)) {
            fatal("build dir ", dir, " already exists");
        }

        fs::create_directories(dir);

        std::vector<std::string> args{
                "-DUSE_CLANG_TIDY=NO",
                "-DCMAKE_BUILD_TYPE=RelWithDebugInfo",
        };

        if (repo.isVeobot() || repo.isCruft()) {
            args.push_back("-DETO_STAGEDIR=" + oe.pathToStage(stage).string());
        }

        for (auto& a : extraArgs) {
            args.push_back(a);
        }
        cmake(std::move(args));
    }

    void make(std::vector<std::string> extraArgs)
    {
        if (!is_directory(dir)) {
            fatal("build dir doesn't exist");
        }
        auto c = oe.eto().args("xc", "make", "-l28", "-j" + std::to_string(numThreads))
                         .setCurrentDir(dir);
        for (auto& arg : extraArgs) {
            c.arg(arg);
        }
        c.runPty();
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
    int numThreads = 30;
};

constexpr const char* const helpText = R"(A DWIM wrapper for the eto utility
    cmake <ARGS...> - invoke cmake from the current stage build dir
    cmake-init <ARGS...> - initialize a cmake dir for the current stage
    set-stage <stage-name> - set the stage name associated with the current repo
    install - eto stage install the current build configuration
    mk <ARGS...> - invoke make with the current stage build configuration
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

    const auto currentBuildConfig = [&stageName, &verbose]() {
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
        return result;
    };

    for (auto it = args.begin(); it != args.end(); ++it) {
        const auto& arg = *it;

        if (arg == "--help" || arg == "-h") {
            std::cout << helpText << std::endl;
            return 0;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "-n" || arg == "--name") {
            if (++it == args.end()) {
                fatal("missing stage name after ", arg);
            }
            stageName = *it;
        } else if (arg == "cmake") {
            // TODO: dry run support
            currentBuildConfig().cmake(std::vector(++it, args.end()));
            return 0;
        } else if (arg == "cmake-init") {
            // TODO: dry run support
            currentBuildConfig().cmakeInit(std::vector(++it, args.end()));
            return 0;
        } else if (arg == "install") {
            // TODO: dry run support
            currentBuildConfig().install();
            return 0;
        } else if (arg == "mk") {
            // TODO: dry run support
            // forward remaining arguments to make, if any are supplied
            currentBuildConfig().make(std::vector(++it, args.end()));
            return 0;
        } else if (arg == "targets") {
            currentBuildConfig().targets();
            return 0;
        } else if (arg == "status") {
            currentBuildConfig().printStatus();
            return 0;
        } else if (arg == "set-stage") {
            // TODO: dry run support
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
            // TODO: dry run support
            auto oe = VeoOe::find();
            if (!oe.has_value()) {
                fatal("veo oe not found");
            }
            oe->updateOe();
            return 0;
        } else {
            fatal("unknown argument ", quoted(arg));
        }
    }
    return 0;
}
