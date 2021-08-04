#include <iostream>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <chrono>
#include <vector>
#include <fstream>
#include <regex>

struct submit {
    std::string task;
    std::string time;
    std::string result;
    std::string filename;
};

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

void git_add(const std::filesystem::path &filename) {
    std::array<std::string_view, 2> args = {"add", filename.native()};
    issue_git_command(args);
}

void git_commit(std::string_view message, std::string_view time) {
    std::array<std::string_view, 6> args = {"commit", "-m", message, "--date", time, "--allow-empty"};
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

std::vector<submit> extract_submits(const std::filesystem::path &athina_directory) {
    std::vector<submit> result;
    std::string index_body = extract_body(read_index(athina_directory));
    std::regex submit_regex(R"regexp(href="/(\d+)".*"solved">([^<]+)<.*"time">([^<]+)<.*">(\w+)</td></tr>)regexp");
    std::sregex_iterator begin(index_body.begin(), index_body.end(), submit_regex);
    std::sregex_iterator end;
    for (; begin != end; ++begin) {
        std::smatch match = *begin;
        result.push_back({match.str(2), match.str(3), match.str(4), match.str(1)});
    }
    return result;
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

void process_submit(const std::filesystem::path &athina_directory, const submit &sub,
                    const std::filesystem::path &git_directory) {
    std::string file = read_file(athina_directory / (sub.filename + ".html"));
    std::string code = html_to_text(file);
    std::filesystem::path output_file = git_directory / (sub.task + ".cpp");
    {
        std::ofstream output(output_file);
        output << code;
    }
    git_add(output_file);
    std::string message = sub.task + " - " + sub.result;
    git_commit(message, sub.time);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::clog << argv[0] << " [athina snapshot directory] [target git repository]\n";
        return 1;
    }

    std::filesystem::path athina_directory{argv[1]};
    std::filesystem::path git_directory{argv[2]};

    std::vector<submit> submits = extract_submits(athina_directory);

    if (std::filesystem::is_directory(git_directory)) {
        std::filesystem::remove_all(git_directory);
    }
    if (std::filesystem::exists(git_directory)) {
        std::clog << argv[2] << " exists and is not a directory.\n";
        return 2;
    }
    git_init(git_directory);
    std::filesystem::current_path(git_directory);

    for (const submit &submit : std::ranges::reverse_view(submits)) {
        process_submit(athina_directory, submit, git_directory);
    }

    return 0;
}
