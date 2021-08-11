#include <iostream>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <chrono>
#include <vector>
#include <fstream>
#include <regex>
#include <magic.h>
#include <variant>
#include <unordered_set>
#include <cassert>

struct file;

struct submit {
    std::string task;
    std::time_t time;
    std::string result;
    std::string source_code;

    bool is_source_code(const std::vector<std::variant<submit, file>> &) const {
        return true;
    }
};

struct file {
    std::string task;
    std::optional<std::time_t> time;
    std::filesystem::path filename;
    std::optional<std::string> data;
    std::uint16_t group;
    std::optional<std::uint16_t> canonical;

    [[nodiscard]] bool is_pdf() const;

    [[nodiscard]] bool is_source_code(const std::vector<std::variant<submit, file>> &files) const;
};

struct cluster {
    std::vector<std::uint16_t> files;
    std::unordered_map<std::string, std::time_t> source_code_time;

    [[nodiscard]] std::optional<std::time_t>
    time_for_file(const std::filesystem::path &filename) const;
};

struct task {
    std::unordered_map<std::optional<std::uint16_t>, cluster> clusters;
    std::optional<std::time_t> first_submit;
    std::optional<std::time_t> first_ok;
};

struct file_change {
    enum class type : uint32_t {
        PDF,
        OLD_SOLUTION,
        SOLUTION,
        OTHER,
    };

    std::time_t time;
    std::variant<std::filesystem::path, std::string> file;
    std::string task;
    std::string aux;
    type commit_type;
};

struct commit {
    struct change {
        std::variant<std::filesystem::path, std::string> source;
        std::filesystem::path destination;
    };
    std::vector<change> changes;
    std::string message;
    std::time_t time;
    std::string task;

    bool has_pdf() const;

    bool has_solution_source_code() const;
};

struct PathHash {
    std::size_t operator()(std::filesystem::path const &p) const noexcept {
        return std::filesystem::hash_value(p);
    }
};

bool min_time(const std::string &lhs, const std::string &rhs) {
    std::tm t_lhs = {}, t_rhs = {};
    std::istringstream ss_lhs(lhs);
    std::istringstream ss_rhs(rhs);
    ss_lhs.imbue(std::locale("C"));
    ss_rhs.imbue(std::locale("C"));
    ss_lhs >> std::get_time(&t_lhs, "%a, %d %b %Y %H:%M:%S");
    std::time_t lhs_t = std::mktime(&t_lhs);
    ss_rhs >> std::get_time(&t_rhs, "%a, %d %b %Y %H:%M:%S");
    std::time_t rhs_t = std::mktime(&t_rhs);
    bool result = lhs_t < rhs_t;
    std::cout << lhs_t << " " << rhs_t << "\n";
    std::cout << lhs << (result ? "<" : ">=") << rhs << "\n";
    std::cout << std::put_time(&t_lhs, "%c") << " " << std::put_time(&t_rhs, "%c") << "\n";
    abort();
    return result;
}

std::string to_upper(std::string_view str) {
    std::string result;
    std::ranges::transform(str, std::back_inserter(result), ::toupper);
    return result;
}

bool file::is_pdf() const {
    return to_upper(filename.string()).ends_with(".PDF");
}

bool file::is_source_code(const std::vector<std::variant<submit, file>> &files) const {
    std::string name = to_upper(filename.filename().string());
    return name.ends_with(".CPP") || name.ends_with(".CPP~") || (canonical && std::visit(
            [&files](const auto &o) { return o.is_source_code(files); }, files[*canonical]));
}

std::optional<std::time_t> cluster::time_for_file(const std::filesystem::path &filename) const {
    for (const auto &p : source_code_time) {
        if (filename.string().starts_with(p.first)) {
            std::string suffix = filename.string().substr(p.first.size());
            if (suffix.empty() || suffix.starts_with(".o") || suffix.starts_with(".exe") ||
                suffix.starts_with(".cpp~")) {
                return p.second;
            }
        }
    }
    return std::nullopt;
}

bool commit::has_pdf() const {
    for (auto &c : changes) {
        if (const std::filesystem::path *path = std::get_if<std::filesystem::path>(&c.source)) {
            if (to_upper(path->filename().string()).ends_with(".PDF")) {
                return true;
            }
        }
    }
    return false;
}

bool is_source_code_alike(const std::filesystem::path &path) {
    std::string name = to_upper(path.filename().string());
    if (name.ends_with(".CPP") || name.ends_with(".CPP~") || name.ends_with(".CPP~_2")) {
        return true;
    }
    return false;
}

bool commit::has_solution_source_code() const {
    for (auto &c : changes) {
        if (const std::filesystem::path *path = std::get_if<std::filesystem::path>(&c.source)) {
            if (is_source_code_alike(*path)) {
                return true;
            }
        } else {
            return true;
        }
    }
    return false;
}

template<std::ranges::range Range>
void issue_git_command(const Range &args, std::string_view env = "", bool fail_on_error = true) {
    // This is an example of how a subprocess should NOT be started.
    // Args should not be concatenated without proper escaping.
    std::string command = std::string(env) + " git ";
    for (std::string_view arg : args) {
        command += "\"";
        command += arg;
        command += "\"";
        command += " ";
    }
    if (system(command.c_str())) {
        std::clog << command << ": Subprocess failed.\n";
        if (fail_on_error) {
            abort();
        }
    }
}

void git_init(const std::filesystem::path &directory) {
    std::array<std::string_view, 2> args = {"init", directory.native()};
    issue_git_command(args);
}

void git_add() {
    std::array<std::string_view, 2> args = {"add", "-A"};
    issue_git_command(args);
}

void git_commit(std::string_view message, std::string_view time) {
    std::array<std::string_view, 6> args = {"commit", "-m", message, "--date", time,
                                            "--allow-empty"};
    std::string env = "GIT_COMMITTER_DATE=\"";
    env += time;
    env += "\"";
    issue_git_command(args, env, false);
}

std::string read_file(const std::filesystem::path &filename) {
    std::ifstream index(filename);
    index.seekg(0, std::ios_base::end);
    auto size = index.tellg();
    index.seekg(0, std::ios_base::beg);
    std::string index_content(size, '\0');
    index.read(index_content.data(), size);
    if (index.fail()) {
        std::clog << "Index read failed.\n";
        abort();
    }
    return index_content;
}

std::string read_index(const std::filesystem::path &athina_directory) {
    return read_file(athina_directory / "index.html");
}

std::string extract_body(const std::string &index) {
    constexpr std::string_view body_marker = "<!-- [BODY] -->";
    constexpr std::string_view body_end_marker = "<!-- [/BODY] -->";
    auto body_start = index.find(body_marker);
    if (body_start == std::string::npos) {
        std::clog << "Didn't find body inside index.\n";
        abort();
    }
    auto body_end = index.find(body_end_marker);
    if (body_end == std::string::npos) {
        std::clog << "Didn't find end of body inside index.\n";
        abort();
    }
    return index.substr(body_start, body_end - body_start);
}

std::string html_to_text(std::string_view html) {
    constexpr std::string_view header = "<html><body><pre style=\"word-wrap: break-word; white-space: pre-wrap;\">";
    constexpr std::string_view footer = "</pre></body></html>";
    if (!html.starts_with(header)) {
        std::clog << "HTML doesn't have header.\n";
        abort();
    }
    if (!html.ends_with(footer)) {
        std::clog << "HTML doesn't have footer.\n";
        abort();
    }
    html.remove_prefix(header.size());
    html.remove_suffix(footer.size());
    static const std::array replacements = {std::pair{std::regex{"&amp;"}, "&"},
                                            std::pair{std::regex{"&lt;"}, "<"},
                                            std::pair{std::regex{"&gt;"}, ">"},
                                            std::pair{std::regex{"&quot;"}, "\""},
                                            std::pair{std::regex{"&apos;"}, "'"}};
    std::string text(html);
    for (const auto &replacement : replacements) {
        text = std::regex_replace(text, replacement.first, replacement.second);
    }
    return text;
}

std::time_t parse_athina_time(const std::string &str) {
    static const std::regex time_regex(
            "[^,]*, +([[:digit:]]+) ([[:alpha:]]+) ([[:digit:]]+) ([[:digit:]]+):([[:digit:]]+):([[:digit:]]+) ([[:alpha:]]+)");
    constexpr std::array months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
                                   "Oct", "Nov", "Dec"};
    std::smatch match;
    if (std::regex_match(str, match, time_regex)) {
        std::tm tmb{};
        tmb.tm_mday = std::stoi(match.str(1));
        tmb.tm_mon = std::ranges::find(months, match.str(2)) - months.begin();
        tmb.tm_year = std::stoi(match.str(3)) - 1900;
        tmb.tm_hour = std::stoi(match.str(4));
        tmb.tm_min = std::stoi(match.str(5));
        tmb.tm_sec = std::stoi(match.str(6));
        tmb.tm_isdst = (match.str(7) == "CEST");
        return std::mktime(&tmb);
    } else {
        std::clog << "Parsing Athina time failed: " << str << "\n";
        abort();
    }
}

std::vector<submit> extract_submits(const std::filesystem::path &athina_directory) {
    std::vector<submit> result;
    std::string index_body = extract_body(read_index(athina_directory));
    std::regex submit_regex(
            R"regexp(href="/(\d+)".*"solved">([^<]+)<.*"time">([^<]+)<.*">(\w+)</td></tr>)regexp");
    std::sregex_iterator begin(index_body.begin(), index_body.end(), submit_regex);
    std::sregex_iterator end;
    for (; begin != end; ++begin) {
        std::smatch match = *begin;
        std::string file = read_file(athina_directory / (match.str(1) + ".html"));
        std::string code = html_to_text(file);
        result.push_back({match.str(2), parse_athina_time(match.str(3)), match.str(4), code});
    }
    return result;
}

file extract_file(magic_t magic, const std::string &task,
                  const std::filesystem::directory_entry &entry, std::uint16_t group) {
    if (auto *msg = magic_file(magic, entry.path().c_str())) {
        const time_t time = std::chrono::system_clock::to_time_t(
                std::chrono::file_clock::to_sys(entry.last_write_time()));
        tm *tmb = std::localtime(&time);
        std::optional<std::time_t> maybe_time;
        if (!(tmb->tm_year == 2010 - 1900 && tmb->tm_mon == 4 && tmb->tm_mday == 11 &&
              tmb->tm_hour == 22 &&
              tmb->tm_min >= 54 && tmb->tm_min <= 56)) {
            maybe_time.emplace(time);
        }
        if (std::string_view(msg).starts_with("text")) {
            return {task, maybe_time, entry.path(), read_file(entry.path()), group, std::nullopt};
        } else {
            return {task, maybe_time, entry.path(), std::nullopt, group, std::nullopt};
        }
    } else {
        std::clog << magic_error(magic) << "\n";
        abort();
    }
}

std::unordered_set<std::filesystem::path, PathHash>
read_ignored_files(const std::filesystem::path &ignored_files) {
    std::ifstream input(ignored_files);
    if (input.fail()) {
        std::clog << "Failed to open ignored_files file.\n";
        abort();
    }
    std::unordered_set<std::filesystem::path, PathHash> result;
    while (!input.eof()) {
        std::filesystem::path path;
        input >> path;
        result.insert(path);
    }
    return result;
}

std::unordered_map<std::filesystem::path, std::time_t, PathHash>
read_custom_dates(const std::filesystem::path &custom_dates) {
    std::ifstream input(custom_dates);
    if (input.fail()) {
        std::clog << "Failed to open custom_dates file.\n";
        abort();
    }
    std::unordered_map<std::filesystem::path, std::time_t, PathHash> result;
    while (!input.eof()) {
        std::filesystem::path path;
        input >> path;
        std::tm tmb{};
        input >> std::get_time(&tmb, "%d.%m.%Y");
        tmb.tm_hour = 8;
        tmb.tm_isdst = -1;
        std::time_t time = std::mktime(&tmb);
        result[path] = time;
    }
    return result;
}

std::vector<file>
extract_files(magic_t magic, const std::vector<std::filesystem::path> &source_directories,
              std::unordered_set<std::filesystem::path, PathHash> ignored_files,
              std::unordered_map<std::filesystem::path, std::time_t, PathHash> custom_dates) {
    static const std::regex task_regex("(q|Q|[[:alpha:]][[:digit:]]+).*");
    std::vector<file> result;
    uint16_t current_id, next_id = 0;
    for (const auto &directory : source_directories) {
        current_id = next_id++;
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(
                directory)) {
            std::smatch match;
            std::string filename = entry.path().filename().string();
            if (!std::regex_match(filename, match, task_regex)) {
                continue;
            }
            std::string task = to_upper(match.str(1));
            if (entry.is_directory()) {
                uint16_t group_id = next_id++;
                for (const std::filesystem::directory_entry &subentry : std::filesystem::recursive_directory_iterator(
                        entry.path())) {
                    if (subentry.is_directory()) {
                        continue;
                    }
                    result.push_back(extract_file(magic, task, subentry, group_id));
                }
            } else {
                result.push_back(extract_file(magic, task, entry, current_id));
            }
        }
    }
    std::erase_if(result,
                  [&ignored_files](const file &f) { return ignored_files.contains(f.filename); });
    for (auto &r : result) {
        if (!r.time.has_value() && custom_dates.contains(r.filename)) {
            r.time = custom_dates[r.filename];
        }
    }
    return result;
}

std::unordered_map<std::string_view, std::vector<std::uint16_t>>
compute_equivalence(const std::vector<std::variant<submit, file>> &files) {
    std::unordered_map<std::string_view, std::vector<std::uint16_t>> result;
    for (std::uint16_t i = 0; i < files.size(); ++i) {
        auto &variant = files[i];
        if (std::holds_alternative<submit>(variant)) {
            result[std::string_view(std::get<submit>(variant).source_code)].push_back(i);
        } else {
            auto &f = std::get<file>(variant);
            if (f.data) {
                result[std::string_view(*f.data)].push_back(i);
            }
        }
    }
    return result;
}

void mark_canonical_files(std::vector<std::variant<submit, file>> &files,
                          const std::unordered_map<std::string_view, std::vector<std::uint16_t>> &equivalence) {
    for (uint16_t i = 0; i < files.size(); ++i) {
        auto &variant = files[i];
        if (auto *f = std::get_if<file>(&variant)) {
            if (!f->data) {
                continue;
            }
            std::uint16_t canonical = *std::ranges::min_element(
                    equivalence.find(std::string_view(*f->data))->second);
            if (canonical != i) {
                f->canonical = canonical;
            }
        }
    }
}

std::unordered_map<std::string, task>
compute_clusters(const std::vector<std::variant<submit, file>> &files) {
    std::unordered_map<std::string, task> results;
    for (std::uint16_t i = 0; i < files.size(); ++i) {
        auto &variant = files[i];
        if (const submit *sub = std::get_if<submit>(&variant)) {
            results[sub->task].clusters[std::nullopt].files.push_back(i);
        } else {
            const file &f = std::get<file>(variant);
            results[f.task].clusters[f.group].files.push_back(i);
        }
    }
    return results;
}

void extract_interesting_timestamps(std::unordered_map<std::string, task> &tasks,
                                    const std::vector<std::variant<submit, file>> &files) {
    for (auto &t : tasks) {
        std::optional<std::time_t> first_submit, first_ok;
        for (auto &c : t.second.clusters) {
            if (c.first) {
                continue;
            }
            for (std::uint16_t i : c.second.files) {
                const auto &s = std::get<submit>(files[i]);
                if (first_submit) {
                    first_submit = std::min(*first_submit, s.time);
                } else {
                    first_submit = s.time;
                }
                if (s.result == "OK") {
                    if (first_ok) {
                        first_ok = std::min(*first_ok, s.time);
                    } else {
                        first_ok = s.time;
                    }
                }
            }
        }
        t.second.first_submit = first_submit;
        t.second.first_ok = first_ok;
    }
}

void reconcile_timestamps(std::unordered_map<std::string, task> &tasks,
                          std::vector<std::variant<submit, file>> &files) {
    for (auto &t : tasks) {
        std::string_view task_name = t.first;
        std::optional<std::time_t> first_submit = t.second.first_submit;
        std::optional<std::time_t> first_ok = t.second.first_ok;
        for (auto &c : t.second.clusters) {
            // First attempt - the latest time within cluster
            std::optional<std::time_t> time;
            for (std::uint16_t i : c.second.files) {
                auto &variant = files[i];
                if (file *f = std::get_if<file>(&variant)) {
                    if (f->is_pdf()) {
                        continue;
                    }
                    // Also assign timestamps from canonical files.
                    if (!f->time && f->canonical) {
                        auto &variant = files[*f->canonical];
                        if (const submit *sub = std::get_if<submit>(&variant)) {
                            f->time = sub->time;
                        } else {
                            const file &f2 = std::get<file>(variant);
                            f->time = f2.time;
                        }
                    }
                    if (time) {
                        if (f->time) {
                            time = std::max(*time, *f->time);
                        }
                    } else {
                        time = f->time;
                    }
                }
            }
            // Second attempt - the latest time of canonical submits
            if (!time) {
                for (std::uint16_t i : c.second.files) {
                    auto &variant = files[i];
                    if (const file *f = std::get_if<file>(&variant)) {
                        if (f->is_pdf()) {
                            continue;
                        }
                        if (f->canonical) {
                            std::optional<std::time_t> candidate_time;
                            auto &file_variant = files[*f->canonical];
                            if (const submit *s = std::get_if<submit>(&file_variant)) {
                                candidate_time = s->time;
                            } else {
                                const file &ff = std::get<file>(file_variant);
                                candidate_time = ff.time;
                            }
                            if (time && candidate_time) {
                                time = std::max(*time, *candidate_time);
                            } else if (candidate_time) {
                                time = candidate_time;
                            }
                        }
                    }
                }
            }
            if (first_ok) {
                time = first_ok;
            }
            for (std::uint16_t i : c.second.files) {
                auto &variant = files[i];
                if (file *f = std::get_if<file>(&variant)) {
                    if (!f->time) {
                        if (f->is_pdf()) {
                            f->time = first_submit;
                        } else if (first_ok && !f->is_source_code(files)) {
                            f->time = *first_ok;
                        } else {
                            f->time = time;
                        }
                    }
                }
            }
            // Store timestamps of sources in cluster
            for (std::uint16_t i : c.second.files) {
                auto &variant = files[i];
                if (file *f = std::get_if<file>(&variant)) {
                    if (f->time && f->is_source_code(files)) {
                        std::string name = f->filename.string();
//                        if (name.ends_with("~")) {
//                            assert(f->is_source_code(files));
//                            name.resize(name.size() - 1);
//                        }
                        if (to_upper(name).ends_with(".CPP")) {
                            assert(f->is_source_code(files));
                            name.resize(name.size() - 4);
                        }
                        c.second.source_code_time[name] = *f->time;
                    }
                }
            }
        }
    }
}

std::string time_to_string(std::time_t time) {
    std::ostringstream ss;
    ss.imbue(std::locale("C"));
    ss << std::put_time(std::localtime(&time), "%c %z");
    return ss.str();
}

void reconcile_timestamps_from_sources(std::unordered_map<std::string, task> &tasks,
                                       std::vector<std::variant<submit, file>> &files) {
    for (const auto &t : tasks) {
        for (const auto &c : t.second.clusters) {
            for (const auto &i : c.second.files) {
                auto &v = files[i];
                if (file *f = std::get_if<file>(&v)) {
                    if (std::optional<std::time_t> maybe_time = c.second.time_for_file(
                                f->filename); !f->is_pdf() && maybe_time && f->time != maybe_time) {
//                        std::cout << "Overriding " << f->filename << " from "
//                                  << time_to_string(*f->time) << " to "
//                                  << time_to_string(*maybe_time) << "\n";
                        f->time = *maybe_time;
                    }
                }
            }
        }
    }
}

bool is_pdf(const std::variant<submit, file> &variant) {
    if (std::holds_alternative<submit>(variant)) {
        return false;
    } else {
        const file &f = std::get<file>(variant);
        return f.is_pdf();
    }
}

bool is_task_old_source_code(const std::variant<submit, file> &variant) {
    if (std::holds_alternative<submit>(variant)) {
        return false;
    } else {
        const file &f = std::get<file>(variant);
        const std::string filename = to_upper(f.filename.filename().string());
        const std::string &task = f.task;
        return filename == (task + ".CPP~") || filename == (task + ".CPP~_2");
    }
}

bool is_task_source_code(const std::variant<submit, file> &variant) {
    if (std::holds_alternative<submit>(variant)) {
        return true;
    } else {
        const file &f = std::get<file>(variant);
        const std::string filename = to_upper(f.filename.filename().string());
        const std::string &task = f.task;
        return filename == task || filename == (task + ".O") || filename == (task + ".EXE") ||
               filename == (task + ".EXE.STACKDUMP") ||
               filename == (task + ".CPP");
    }
}

bool is_other(const std::variant<submit, file> &variant) {
    return !is_pdf(variant) && !is_task_source_code(variant);
}

file_change::type get_commit_type(const std::variant<submit, file> &variant) {
    if (is_pdf(variant)) {
        return file_change::type::PDF;
    } else if (is_task_old_source_code(variant)) {
        return file_change::type::OLD_SOLUTION;
    } else if (is_task_source_code(variant)) {
        return file_change::type::SOLUTION;
    } else {
        assert(is_other(variant));
        return file_change::type::OTHER;
    }
}

bool is_canonical(const std::variant<submit, file> &variant) {
    if (std::holds_alternative<submit>(variant)) {
        return true;
    } else {
        const file &f = std::get<file>(variant);
        return !f.canonical.has_value();
    }
}

std::map<std::time_t, std::vector<file_change>>
generate_changes(const std::vector<std::variant<submit, file>> &files) {
    std::map<std::time_t, std::vector<file_change>> result;
    for (auto &v : files) {
        if (!is_canonical(v)) {
            continue;
        }
        if (const submit *sub = std::get_if<submit>(&v)) {
            file_change c;
            c.time = sub->time;
            c.task = sub->task;
            c.aux = sub->result;
            c.commit_type = file_change::type::SOLUTION;
            c.file = sub->source_code;
            result[c.time].push_back(c);
        } else {
            const file &f = std::get<file>(v);
            file_change c;
            c.time = *f.time;
            c.task = f.task;
            c.file = f.filename;
            c.commit_type = get_commit_type(v);
            switch (c.commit_type) {
                case file_change::type::PDF:
                    c.aux = "task statement";
                    break;
                case file_change::type::OLD_SOLUTION:
                    c.aux = "solution";
                    break;
                case file_change::type::SOLUTION:
                    c.aux = "solution";
                    break;
                case file_change::type::OTHER:
                    c.aux = "tests";
                    break;
            }
            result[c.time].push_back(c);
        }
    }
    return result;
}

std::vector<commit>
generate_commits(const std::map<std::time_t, std::vector<file_change>> &file_changes) {
    std::vector<commit> results;
    for (auto &p: file_changes) {
        commit c;
        c.time = p.first;
        for (std::size_t i = 0;;) {
            goto begin;
            push_and_reset:
            results.push_back(c);
            c = {};
            c.time = p.first;
            begin:
            if (i >= p.second.size()) {
                break;
            }
            const file_change &change = p.second[i];
            if (c.task.empty()) {
                c.task = change.task;
            } else if (c.task != change.task) {
                goto push_and_reset;
            }
            switch (change.commit_type) {
                case file_change::type::PDF: {
                    assert(c.changes.empty());
                    std::filesystem::path path = std::get<std::filesystem::path>(change.file);
                    c.changes.push_back(
                            {.source =path, .destination=change.task / path.filename()});
                    c.message = change.task + " - task";
                    i += 1;
                    goto push_and_reset;
                    break;
                }
                case file_change::type::OLD_SOLUTION:
                case file_change::type::SOLUTION:
                    if (const std::string *source_code = std::get_if<std::string>(&change.file)) {
                        if (c.has_solution_source_code()) {
                            goto push_and_reset;
                        }
                        c.changes.push_back({.source = *source_code, .destination=
                        std::filesystem::path{change.task} / (change.task + ".cpp")});
                        c.message = change.task + " - " + change.aux;
                    } else {
                        const std::filesystem::path &file_location = std::get<std::filesystem::path>(
                                change.file);
                        std::filesystem::path destination;
                        if (is_source_code_alike(file_location)) {
                            if (c.has_solution_source_code()) {
                                goto push_and_reset;
                            }
                            destination =
                                    std::filesystem::path{change.task} / (change.task + ".cpp");
                        } else {
                            destination =
                                    std::filesystem::path{change.task} / file_location.filename();
                        }
                        c.changes.push_back({.source = file_location, .destination = destination});
                        if (c.message.empty()) {
                            c.message = change.task + " - implementation";
                        }
                    }
                    i += 1;
                    break;
                case file_change::type::OTHER: {
                    assert(!c.has_pdf());
                    const std::filesystem::path &file_location = std::get<std::filesystem::path>(
                            change.file);
                    const std::string file_location_s = file_location.string();
                    std::filesystem::path destination;
                    const std::regex location_regex(".*/" + change.task + "/(.+)");
                    std::smatch match;
                    if (std::regex_match(file_location_s, match, location_regex)) {
                        destination = std::filesystem::path{change.task} / match.str(1);
                    } else {
                        destination = std::filesystem::path{change.task} / file_location.filename();
                    }
                    c.changes.push_back({.source = file_location, .destination=destination});
                    if (c.message.empty()) {
                        c.message = change.task + " - tests";
                    }
                    i += 1;
                    break;
                }
            }
        }
        if (c.changes.empty()) {
            assert(c.task.empty());
        } else {
            results.push_back(c);
        }
    }

    return results;
}

void execute_commits(const std::vector<commit> &commits) {
    for (auto &c : commits) {
        for (auto &change : c.changes) {
            std::filesystem::create_directory(change.destination.parent_path());
            if (const std::filesystem::path *file_location = std::get_if<std::filesystem::path>(
                    &change.source)) {
                if (std::filesystem::file_size(*file_location) > 100 * 1024 * 1024) {
                    std::clog << "Ignoring big file: " << *file_location << "\n";
                    continue;
                }
                assert(std::filesystem::copy_file(*file_location, change.destination,
                                                  std::filesystem::copy_options::overwrite_existing));
            } else {
                std::ofstream output(change.destination, std::ios_base::trunc);
                output << std::get<std::string>(change.source);
            }
        }
        git_add();
        git_commit(c.message, time_to_string(c.time));
    }
}

int main(int argc, char **argv) {
    if (argc < 6) {
        std::clog << argv[0]
                  << " [target git repository] [athina snapshot directory] [custom dates file] [ignored files] [source directories...]\n";
        return 1;
    }

    const magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (magic_load(magic, nullptr) != 0) {
        std::clog << magic_error(magic) << "\n";
        return 3;
    }

    std::filesystem::path athina_directory{argv[2]};
    std::filesystem::path git_directory{argv[1]};
    std::vector<std::filesystem::path> source_directories;
    source_directories.reserve(argc - 5);
    for (int i = 5; i < argc; ++i) {
        source_directories.emplace_back(argv[i]);
    }

    std::vector<std::variant<submit, file>> files;
    std::ranges::copy(extract_submits(athina_directory), std::back_inserter(files));
    std::ranges::copy(extract_files(magic, source_directories, read_ignored_files(argv[4]),
                                    read_custom_dates(argv[3])),
                      std::back_inserter(files));

    magic_close(magic);

    std::unordered_map<std::string_view, std::vector<std::uint16_t >> equivalence = compute_equivalence(
            files);

    mark_canonical_files(files, equivalence);

    std::unordered_map<std::string, task> clusters = compute_clusters(files);

    extract_interesting_timestamps(clusters, files);

    reconcile_timestamps(clusters, files);
    reconcile_timestamps_from_sources(clusters, files);

    std::map<std::time_t, std::vector<file_change>> file_changes = generate_changes(files);
    for (auto &p : file_changes) {
        std::ranges::sort(p.second, [](const file_change &lhs, const file_change &rhs) {
            return lhs.commit_type < rhs.commit_type;
        });
    }

    std::vector<commit> commits = generate_commits(file_changes);

    if (std::filesystem::is_directory(git_directory)) {
        std::filesystem::remove_all(git_directory);
    }
    if (std::filesystem::exists(git_directory)) {
        std::clog << argv[1] << " exists and is not a directory.\n";
        return 2;
    }
    git_init(git_directory);
    std::filesystem::current_path(git_directory);

    execute_commits(commits);

    return 0;
}
