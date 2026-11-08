// Runs an Agas grammar over many real source files and reports what it rejects.
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr auto processTimeout = std::chrono::seconds(60);

const std::vector<std::string> cStandards{"c90",   "c99",   "c11",   "c17",   "c23",  "gnu90",
                                          "gnu99", "gnu11", "gnu17", "gnu23"};
const std::vector<std::string> cppStandards{"c++98",   "c++11",   "c++14",   "c++17",   "c++20",   "c++23",   "c++26",
                                            "gnu++98", "gnu++11", "gnu++14", "gnu++17", "gnu++20", "gnu++23", "gnu++26"};

// --- Options -------------------------------------------------------------

struct Options {
    std::string command;
    fs::path root;
    fs::path out;
    fs::path agas;
    fs::path package;
    fs::path preprocessedOut;
    fs::path fromLists;
    std::string language{"c"};
    std::string standard{"c90"};
    bool pedantic{true};
    bool pureOnly{false};
    bool noIncludes{false};
    bool preprocessed{false};
    std::uintmax_t maximumBytes{200'000};
    std::size_t limit{0};
    std::uint64_t sample{1};
    std::uint64_t seed{0};
    unsigned jobs{std::max(1U, std::thread::hardware_concurrency())};
    std::set<fs::path> skipped{};
    std::set<std::string> excludedDirectories{};
    fs::path onlyFailing{};
};

// One path relative to ROOT in a line; empty lines and # comments are ignored.
auto readSkipList(const fs::path &path) -> std::set<fs::path> {
    std::ifstream in(path);
    if (!in)
        throw std::invalid_argument("cannot open skip list " + path.string());
    std::set<fs::path> result;
    for (std::string line; std::getline(in, line);)
        if (!line.empty() && line.front() != '#')
            result.insert(line);
    return result;
}

constexpr std::string_view usage =
        R"(usage:
  grammar-stress prepare ROOT --out DIR [--preprocessed-out DIR] [--language c]
                 [--max-bytes N] [--skip-list FILE] [--exclude-dir NAME] [--jobs N] [--limit N]
  grammar-stress prepare ROOT --from-lists DIR --preprocessed-out DIR [--language c] [--jobs N]
  grammar-stress run ROOT --agas PATH --package DIR --out DIR [--preprocessed]
                 [--language c] [--std c90] [--no-pedantic] [--no-includes] [--pure] [--max-bytes N]
                 [--skip-list FILE] [--exclude-dir NAME] [--jobs N] [--limit N]
                 [--sample N] [--seed N] [--only-failing RESULTS.TSV]

prepare  sorts the sources under ROOT by the first standard which the compiler
         accepts with -pedantic-errors: lists in DIR/<std>.txt, and also too-big,
         skipped and none. With --preprocessed-out it keeps the preprocessed text,
         line markers included, in that DIR/<std>/<source>.i. With --from-lists DIR it
         reads such lists instead, so it only preprocesses, without checking anything.
run      parses with an Agas package and writes DIR/results.tsv and DIR/summary.txt.
         With --preprocessed, ROOT holds such .i files, and the compiler is not run.
         Otherwise it preprocesses the sources which the compiler accepts in --std.

Sources are judged with their comments removed, so // does not exclude a file
from C90.

  --language   c takes *.c with gcc; c++ takes *.C, *.cc, *.cpp and *.cxx with g++
  --no-includes  take only files without includes
  --pure       take only files without includes and without __ words after
               preprocessing, as -pedantic-errors does not reject those words
  --max-bytes  skip a file as too-big when it produces more than this after
               preprocessing, not counting system headers
  --skip-list  skip the files listed there, relative to ROOT, before any work
  --exclude-dir  leave out this directory directly under ROOT; can be repeated
  --sample N   take about every Nth file, chosen by its path and --seed, for a quick
               pass; other seeds give other files
  --only-failing  take only the files which this results.tsv did not parse; give
               another --out, so that the full report stays
)";

auto parseOptions(int argc, char **argv) -> Options {
    if (argc < 3)
        throw std::invalid_argument("missing command or root");
    Options options;
    options.command = argv[1];
    options.root = argv[2];
    for (int index = 3; index < argc; ++index) {
        const std::string_view name = argv[index];
        const auto value = [&]() -> std::string {
            if (++index >= argc)
                throw std::invalid_argument("missing value of " + std::string{name});
            return argv[index];
        };
        if (name == "--out")
            options.out = value();
        else if (name == "--agas")
            options.agas = value();
        else if (name == "--package")
            options.package = value();
        else if (name == "--preprocessed-out")
            options.preprocessedOut = value();
        else if (name == "--from-lists")
            options.fromLists = value();
        else if (name == "--preprocessed")
            options.preprocessed = true;
        else if (name == "--language")
            options.language = value();
        else if (name == "--std")
            options.standard = value();
        else if (name == "--no-pedantic")
            options.pedantic = false;
        else if (name == "--pure")
            options.pureOnly = true;
        else if (name == "--no-includes")
            options.noIncludes = true;
        else if (name == "--max-bytes")
            options.maximumBytes = std::stoull(value());
        else if (name == "--skip-list")
            options.skipped = readSkipList(value());
        else if (name == "--exclude-dir")
            options.excludedDirectories.insert(value());
        else if (name == "--limit")
            options.limit = std::stoull(value());
        else if (name == "--only-failing")
            options.onlyFailing = value();
        else if (name == "--sample")
            options.sample = std::max<std::uint64_t>(1, std::stoull(value()));
        else if (name == "--seed")
            options.seed = std::stoull(value());
        else if (name == "--jobs")
            options.jobs = std::max(1, std::stoi(value()));
        else
            throw std::invalid_argument("unknown option " + std::string{name});
    }
    if (!options.fromLists.empty()) {
        if (options.preprocessedOut.empty())
            throw std::invalid_argument("--from-lists requires --preprocessed-out");
    } else if (options.out.empty()) {
        throw std::invalid_argument("--out is required");
    }
    if (options.command == "run" && (options.agas.empty() || options.package.empty()))
        throw std::invalid_argument("run requires --agas and --package");
    if (options.command != "run" && options.command != "prepare")
        throw std::invalid_argument("unknown command " + options.command);
    if (options.language != "c" && options.language != "c++")
        throw std::invalid_argument("unknown language " + options.language);
    return options;
}

// --- Processes -----------------------------------------------------------

struct ProcessResult {
    int exitCode{};
    bool timedOut{};
    std::string errors;
};

auto findProgram(const std::string &name) -> fs::path {
    const char *variable = std::getenv("PATH");
    std::istringstream directories{variable != nullptr ? variable : ""};
    for (std::string directory; std::getline(directories, directory, ':');) {
        const fs::path candidate = fs::path{directory} / name;
        if (::access(candidate.c_str(), X_OK) == 0)
            return candidate;
    }
    throw std::runtime_error("cannot find " + name + " in PATH");
}

auto readFile(const fs::path &path) -> std::string {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// Standard output goes to a file, as preprocessed sources can be large. posix_spawn
// matters here: fork from many threads queues them on copying the page tables, which
// leaves most processor cores idle when running thousands of short compilers.
auto runProcess(const fs::path &program, const std::vector<std::string> &arguments, const fs::path &outputFile,
                const fs::path &errorFile) -> ProcessResult {
    const std::string programName = program.string();
    std::vector<char *> argv{const_cast<char *>(programName.c_str())};
    for (const auto &argument: arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    constexpr int writeFlags = O_WRONLY | O_CREAT | O_TRUNC;
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, outputFile.c_str(), writeFlags, 0600);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, errorFile.c_str(), writeFlags, 0600);
    pid_t process = 0;
    const int spawned = posix_spawn(&process, programName.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0)
        throw std::runtime_error("cannot start " + programName + ": " + std::strerror(spawned));

    ProcessResult result;
    const auto deadline = std::chrono::steady_clock::now() + processTimeout;
    int status = 0;
    while (waitpid(process, &status, WNOHANG) == 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(process, SIGKILL);
            waitpid(process, &status, 0);
            result.timedOut = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if (!result.timedOut)
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.errors = readFile(errorFile);
    return result;
}

class WorkFiles {
public:
    explicit WorkFiles(unsigned worker) {
        const fs::path shared{"/dev/shm"};
        const fs::path base = fs::is_directory(shared) ? shared : fs::temp_directory_path();
        const std::string prefix = "grammar_stress_" + std::to_string(::getpid()) + "_" + std::to_string(worker);
        output = base / (prefix + ".i");
        errors = base / (prefix + ".err");
        stripped = base / (prefix + ".c");
    }
    ~WorkFiles() {
        std::error_code ignored;
        fs::remove(output, ignored);
        fs::remove(errors, ignored);
        fs::remove(stripped, ignored);
    }
    WorkFiles(const WorkFiles &) = delete;
    auto operator=(const WorkFiles &) -> WorkFiles & = delete;

    fs::path output;
    fs::path errors;
    fs::path stripped;
};

// Comments do not matter to a grammar, but gcc -std=c90 reads // as two divisions.
// So every standard judges the source without comments, with macros still unexpanded.
auto stripComments(const fs::path &compiler, const Options &options, const fs::path &source, const WorkFiles &files)
        -> bool {
    const std::string standard = options.language == "c" ? "-std=gnu99" : "-std=gnu++26";
    const auto result =
            runProcess(compiler, {"-x", options.language, standard, "-fpreprocessed", "-dD", "-E", "-P", source.string()},
                       files.stripped, files.errors);
    return !result.timedOut && result.exitCode == 0;
}

// The stripped copy lives elsewhere, so its quoted includes need the source directory.
auto compilerOn(const Options &options, const std::string &standard, bool pedantic, const std::string &mode,
                const fs::path &source, const WorkFiles &files) -> std::vector<std::string> {
    std::vector<std::string> arguments{"-x", options.language, "-std=" + standard};
    if (pedantic)
        arguments.emplace_back("-pedantic-errors");
    arguments.push_back(mode);
    arguments.emplace_back("-iquote");
    arguments.push_back(source.parent_path().string());
    arguments.push_back(files.stripped.string());
    return arguments;
}

auto compilerName(const Options &options) -> std::string {
    return options.language == "c" ? "gcc" : "g++";
}

// --- Source files --------------------------------------------------------

// FNV-1a: the same files for the same seed, on every machine.
auto sampleHash(const std::string &path, std::uint64_t seed) -> std::uint64_t {
    std::uint64_t hash = 14695981039346656037ULL ^ seed;
    for (const char value: path) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// The files of an earlier report which a grammar has not parsed yet.
auto readFailing(const fs::path &report) -> std::vector<fs::path> {
    std::ifstream in(report);
    if (!in)
        throw std::invalid_argument("cannot open report " + report.string());
    std::vector<fs::path> sources;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        const auto first = line.find('\t');
        const auto second = line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            continue;
        const auto category = std::string_view{line}.substr(first + 1, second - first - 1);
        if (category == "rejected" || category == "lexer-error" || category == "agas-error" || category == "timeout")
            sources.emplace_back(line.substr(0, first));
    }
    return sources;
}

auto collectSources(const Options &options) -> std::vector<fs::path> {
    if (!options.onlyFailing.empty())
        return readFailing(options.onlyFailing);
    static const std::set<std::string> cExtensions{".c"};
    static const std::set<std::string> cppExtensions{".C", ".cc", ".cpp", ".cxx"};
    static const std::set<std::string> preprocessedExtensions{".i"};
    const auto &extensions = options.preprocessed ? preprocessedExtensions
                             : options.language == "c" ? cExtensions : cppExtensions;
    std::vector<fs::path> sources;
    for (const auto &entry:
         fs::recursive_directory_iterator(options.root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file() || !extensions.contains(entry.path().extension().string()))
            continue;
        fs::path source = fs::relative(entry.path(), options.root);
        if (!options.excludedDirectories.contains(source.begin()->string()) &&
            sampleHash(source.generic_string(), options.seed) % options.sample == 0)
            sources.push_back(std::move(source));
    }
    std::ranges::sort(sources);
    if (options.limit != 0 && sources.size() > options.limit)
        sources.resize(options.limit);
    return sources;
}

auto isWordCharacter(char value) -> bool {
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

auto hasDoubleUnderscoreWord(const std::string &text) -> bool {
    for (std::size_t found = text.find("__"); found != std::string::npos; found = text.find("__", found + 1))
        if ((found == 0 || !isWordCharacter(text[found - 1])) && found + 2 < text.size() &&
            isWordCharacter(text[found + 2]))
            return true;
    return false;
}

struct Preprocessed {
    std::string code;
    std::uintmax_t ownBytes{};
    bool includes{};
};

// Reads preprocessed text, which grammars take as it is: they ignore lines starting
// with #, while here line markers tell what came from system headers. code is the
// text without such lines. Only a marker with flags 1 and 3 enters a system file, and
// only flag 2 leaves one: other markers with flag 3 mark macro expansions inside the
// source itself. So ownBytes counts what the file itself produces, however large its
// headers are. The compiler enters stdc-predef.h by itself, which is no include.
auto inspectPreprocessed(const std::string &text) -> Preprocessed {
    Preprocessed result;
    result.code.reserve(text.size());
    std::size_t systemDepth = 0;
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t end = std::min(text.find('\n', position), text.size());
        const std::string_view line{text.data() + position, end - position};
        position = end + 1;
        if (line.starts_with("# ") && line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[2])) != 0) {
            const auto close = line.rfind('"');
            const auto flags = close == std::string_view::npos ? std::string_view{} : line.substr(close + 1);
            const auto hasFlag = [&](char flag) {
                for (std::size_t index = 0; index + 1 < flags.size(); ++index)
                    if (flags[index] == ' ' && flags[index + 1] == flag)
                        return true;
                return false;
            };
            if (hasFlag('1') && line.find("stdc-predef.h") == std::string_view::npos &&
                line.find("\"<") == std::string_view::npos)
                result.includes = true;
            if (hasFlag('1') && hasFlag('3'))
                ++systemDepth;
            else if (hasFlag('2') && systemDepth > 0)
                --systemDepth;
            continue;
        }
        // #pragma and #ident survive preprocessing, but they are no part of the language.
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos && line[first] == '#')
            continue;
        result.code.append(line);
        result.code += '\n';
        if (systemDepth == 0)
            result.ownBytes += line.size() + 1;
    }
    return result;
}

// --- Work distribution ---------------------------------------------------

template<class Work>
void forEachSource(const std::vector<fs::path> &sources, unsigned jobs, Work work) {
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> finished{0};
    std::vector<std::jthread> workers;
    for (unsigned worker = 0; worker < jobs; ++worker)
        workers.emplace_back([&, worker] {
            WorkFiles files{worker};
            for (std::size_t index = next++; index < sources.size(); index = next++) {
                work(sources[index], files);
                const std::size_t done = ++finished;
                if (done % 1000 == 0)
                    std::cerr << done << " / " << sources.size() << " files\n";
            }
        });
}

// --- prepare -------------------------------------------------------------

// Gives the list which a source belongs to, and keeps its preprocessed text if asked.
auto prepareSource(const Options &options, const fs::path &compiler, const fs::path &source, const WorkFiles &files)
        -> std::string {
    if (options.skipped.contains(source))
        return "skipped";
    const fs::path path = options.root / source;
    if (!stripComments(compiler, options, path, files))
        return "none";
    const auto &standards = options.language == "c" ? cStandards : cppStandards;
    for (const auto &standard: standards) {
        const auto check = runProcess(compiler, compilerOn(options, standard, true, "-fsyntax-only", path, files),
                                      files.output, files.errors);
        if (check.timedOut || check.exitCode != 0)
            continue;
        const auto expanded = runProcess(compiler, compilerOn(options, standard, true, "-E", path, files),
                                         files.output, files.errors);
        if (expanded.timedOut || expanded.exitCode != 0)
            return "none";
        if (inspectPreprocessed(readFile(files.output)).ownBytes > options.maximumBytes)
            return "too-big";
        if (!options.preprocessedOut.empty()) {
            const fs::path kept = options.preprocessedOut / standard / (source.string() + ".i");
            fs::create_directories(kept.parent_path());
            fs::copy_file(files.output, kept, fs::copy_options::overwrite_existing);
        }
        return standard;
    }
    return "none";
}

// Restores the preprocessed files after /tmp was cleared. The lists already say which
// standard each source belongs to, so nothing has to be tried or checked again.
void prepareFromLists(const Options &options) {
    const fs::path compiler = findProgram(compilerName(options));
    std::map<fs::path, std::string> standardOf;
    for (const auto &standard: options.language == "c" ? cStandards : cppStandards) {
        std::ifstream list(options.fromLists / (standard + ".txt"));
        for (std::string line; std::getline(list, line);)
            if (!line.empty())
                standardOf.emplace(line, standard);
    }
    if (standardOf.empty())
        throw std::runtime_error("no lists in " + options.fromLists.string());
    std::vector<fs::path> sources;
    sources.reserve(standardOf.size());
    for (const auto &[source, standard]: standardOf)
        sources.push_back(source);
    std::atomic<std::size_t> failed{0};
    forEachSource(sources, options.jobs, [&](const fs::path &source, const WorkFiles &files) {
        const fs::path path = options.root / source;
        const std::string &standard = standardOf.at(source);
        const auto expanded = stripComments(compiler, options, path, files)
                                      ? runProcess(compiler, compilerOn(options, standard, true, "-E", path, files),
                                                   files.output, files.errors)
                                      : ProcessResult{1, false, ""};
        if (expanded.timedOut || expanded.exitCode != 0) {
            ++failed;
            return;
        }
        const fs::path kept = options.preprocessedOut / standard / (source.string() + ".i");
        fs::create_directories(kept.parent_path());
        fs::copy_file(files.output, kept, fs::copy_options::overwrite_existing);
    });
    std::cout << "files\t" << sources.size() << "\nfailed\t" << failed.load() << '\n';
}

void prepare(const Options &options) {
    if (!options.fromLists.empty()) {
        prepareFromLists(options);
        return;
    }
    const fs::path compiler = findProgram(compilerName(options));
    const auto sources = collectSources(options);
    fs::create_directories(options.out);
    std::mutex lock;
    std::map<std::string, std::vector<fs::path>> lists;
    forEachSource(sources, options.jobs, [&](const fs::path &source, const WorkFiles &files) {
        const std::string found = prepareSource(options, compiler, source, files);
        const std::scoped_lock guard{lock};
        lists[found].push_back(source);
    });
    for (auto &[standard, list]: lists) {
        std::ranges::sort(list);
        std::ofstream out(options.out / (standard + ".txt"));
        for (const auto &source: list)
            out << source.generic_string() << '\n';
        std::cout << standard << '\t' << list.size() << '\n';
    }
}

// --- run -----------------------------------------------------------------

struct Finding {
    fs::path source;
    std::string category;
    std::optional<std::size_t> line;
    std::optional<std::size_t> column;
    std::string token;
    std::string text;
};

auto cleanForReport(std::string_view text) -> std::string {
    std::string result;
    for (const char value: text)
        result += value == '\t' || value == '\n' || value == '\r' ? ' ' : value;
    return result.substr(0, 150);
}

// Fills in where the syntax error is, from file:line:column in the agas message.
void describeSyntaxError(Finding &finding, const std::string &errors, const std::string &preprocessed) {
    static const std::regex position{R"(:(\d+):(\d+): source has a syntax error)"};
    std::smatch match;
    if (!std::regex_search(errors, match, position))
        return;
    finding.line = std::stoull(match[1].str());
    finding.column = std::stoull(match[2].str());
    std::size_t start = 0;
    for (std::size_t line = 1; line < *finding.line && start != std::string::npos; ++line) {
        start = preprocessed.find('\n', start);
        if (start != std::string::npos)
            ++start;
    }
    if (start == std::string::npos)
        return;
    const std::size_t end = std::min(preprocessed.find('\n', start), preprocessed.size());
    const std::string_view line{preprocessed.data() + start, end - start};
    finding.text = cleanForReport(line);
    if (*finding.column == 0 || *finding.column > line.size()) {
        finding.token = "<end of input>";
        return;
    }
    const auto rest = line.substr(*finding.column - 1);
    std::size_t length = 0;
    while (length < rest.size() && isWordCharacter(rest[length]))
        ++length;
    finding.token = std::string{rest.substr(0, std::max<std::size_t>(length, 1))};
}

auto categoryOfAgasFailure(const std::string &errors) -> std::string {
    if (errors.find("source has a syntax error") != std::string::npos)
        return "rejected";
    if (errors.find("cannot form a token") != std::string::npos ||
        errors.find("invalid UTF-8") != std::string::npos)
        return "lexer-error";
    return "agas-error";
}

// Runs the compiler on a source, leaving the preprocessed text in files.output, or
// returns the category of a source which does not get that far.
auto preprocessSource(const Options &options, const fs::path &compiler, const fs::path &source, const WorkFiles &files)
        -> std::optional<std::string> {
    const fs::path path = options.root / source;
    if (!stripComments(compiler, options, path, files))
        return "preprocessor-failed";
    const auto check = runProcess(compiler,
                                  compilerOn(options, options.standard, options.pedantic, "-fsyntax-only", path, files),
                                  files.output, files.errors);
    if (check.timedOut || check.exitCode != 0)
        return "not-accepted-by-compiler";
    const auto expanded = runProcess(compiler, compilerOn(options, options.standard, options.pedantic, "-E", path, files),
                                     files.output, files.errors);
    if (expanded.timedOut || expanded.exitCode != 0)
        return "preprocessor-failed";
    return std::nullopt;
}

auto examine(const Options &options, const fs::path &compiler, const fs::path &source, const WorkFiles &files)
        -> Finding {
    Finding finding{source, "", {}, {}, "", ""};
    if (options.skipped.contains(source)) {
        finding.category = "skipped";
        return finding;
    }
    if (!options.preprocessed) {
        if (const auto stopped = preprocessSource(options, compiler, source, files)) {
            finding.category = *stopped;
            return finding;
        }
    }
    const fs::path input = options.preprocessed ? options.root / source : files.output;
    const std::string preprocessed = readFile(input);
    const auto [code, ownBytes, includes] = inspectPreprocessed(preprocessed);
    if (ownBytes > options.maximumBytes) {
        finding.category = "too-big";
        return finding;
    }
    if ((options.pureOnly || options.noIncludes) && includes) {
        finding.category = "has-include";
        return finding;
    }
    if (options.pureOnly && hasDoubleUnderscoreWord(code)) {
        finding.category = "has-gnu-words";
        return finding;
    }
    // --ast-stats stops before the AST wire, whose limits say nothing about the grammar.
    const fs::path discard = fs::path{files.errors}.replace_extension(".stats");
    const auto parsed =
            runProcess(options.agas, {"--ast-stats", options.package.string(), input.string()}, discard, files.errors);
    std::error_code ignored;
    fs::remove(discard, ignored);
    if (parsed.timedOut) {
        finding.category = "timeout";
    } else if (parsed.exitCode == 0) {
        finding.category = "parsed";
    } else {
        finding.category = categoryOfAgasFailure(parsed.errors);
        if (finding.category == "rejected")
            describeSyntaxError(finding, parsed.errors, preprocessed);
        else
            finding.text = cleanForReport(parsed.errors);
    }
    return finding;
}

void writeReports(const Options &options, std::vector<Finding> findings) {
    std::ranges::sort(findings, {}, &Finding::source);
    std::ofstream results(options.out / "results.tsv");
    results << "file\tcategory\tline\tcolumn\ttoken\ttext\n";
    std::map<std::string, std::size_t> categories;
    std::map<std::string, std::size_t> tokens;
    for (const auto &finding: findings) {
        results << finding.source.generic_string() << '\t' << finding.category << '\t'
                << (finding.line ? std::to_string(*finding.line) : "") << '\t'
                << (finding.column ? std::to_string(*finding.column) : "") << '\t' << finding.token << '\t'
                << finding.text << '\n';
        ++categories[finding.category];
        if (finding.category == "rejected")
            ++tokens[finding.token.empty() ? "<unknown>" : finding.token];
    }
    std::ostringstream summary;
    summary << "files\t" << findings.size() << '\n';
    for (const auto &[category, count]: categories)
        summary << category << '\t' << count << '\n';
    std::vector<std::pair<std::size_t, std::string>> ranked;
    for (const auto &[token, count]: tokens)
        ranked.emplace_back(count, token);
    std::ranges::sort(ranked, std::greater{});
    if (!ranked.empty())
        summary << "\ntokens at rejections\n";
    for (std::size_t index = 0; index < std::min<std::size_t>(ranked.size(), 40); ++index)
        summary << ranked[index].second << '\t' << ranked[index].first << '\n';
    std::ofstream(options.out / "summary.txt") << summary.str();
    std::cout << summary.str();
}

void run(const Options &options) {
    const fs::path compiler = options.preprocessed ? fs::path{} : findProgram(compilerName(options));
    const auto sources = collectSources(options);
    fs::create_directories(options.out);
    std::mutex lock;
    std::vector<Finding> findings;
    findings.reserve(sources.size());
    forEachSource(sources, options.jobs, [&](const fs::path &source, const WorkFiles &files) {
        Finding finding = examine(options, compiler, source, files);
        const std::scoped_lock guard{lock};
        findings.push_back(std::move(finding));
    });
    writeReports(options, std::move(findings));
}

} // namespace

auto main(int argc, char **argv) -> int {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.command == "prepare")
            prepare(options);
        else
            run(options);
    } catch (const std::invalid_argument &error) {
        std::cerr << error.what() << "\n\n" << usage;
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
