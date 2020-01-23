#include "aids.hpp"

#include <cassert>
#include <cstdint>

using namespace aids;

static
String chop_alpha(String *s)
{
    if (s == nullptr || s->size == 0) {
        return {0};
    }

    size_t i = 0;

    while (i < s->size && isalpha(s->data[i])) {
        i += 1;
    }

    String result = {
        .size = i,
        .data = s->data
    };

    s->size -= i;
    s->data += i;

    return result;
}

static
Result<String, String> parse_include_path(String line)
{
    line = trim_begin(line);
    if (line.size == 0 || *line.data != '#') {
        return {.is_error = true, .error = "#"_s};
    }
    line.size -= 1;
    line.data += 1;

    line = trim_begin(line);
    auto include = chop_alpha(&line);
    if (include != "include"_s) {
        return {.is_error = true, .error = "include"_s};
    }

    line = trim_begin(line);
    if (line.size == 0 || (*line.data != '<' && *line.data != '\"')) {
        return {.is_error = true, .error = "opening quote"_s};
    }
    line.size -= 1;
    line.data += 1;

    line = trim_end(line);
    if (line.size == 0 || (*(line.data + line.size - 1) != '>' && *(line.data + line.size - 1) != '\"')) {
        return {.is_error = true, .error = "closing quote"_s};
    }
    line.size -= 1;

    return {
        .is_error = false,
        .unwrap = line
    };
}

Fixed_Stack<String, 1024> visited;
Fixed_Queue<String, 1024> wave;

static Region<20_MiB> file_memory;
static Region<20_MiB> graph_memory;

// FIXME: is_visited check is O(N)
bool is_visited(String file_path)
{
    for (size_t i = 0; i < visited.size; ++i) {
        if (visited.elements[i] == file_path) {
            return true;
        }
    }

    return false;
}

void usage(FILE *stream)
{
    println(stream, "Usage: cppig [options] [--] <files...>");
    println(stream, "  -n, --name <name>  name of the graphviz graph");
    println(stream, "  -s, --silent       silent mode, suppress all the warnings");
    println(stream, "  -h, --help         show this help and exit");
}

int main(int argc, char *argv[])
{
    int options_end = 1;
    bool silent = false;
    String name = "include_graph"_s;

    while (options_end < argc) {
        auto option = string_of_cstr(argv[options_end]);
        if (option == "-s"_s || option == "--silent"_s) {
            silent = true;
            options_end += 1;
        } else if (option == "-n"_s || option == "--name"_s) {
            if (options_end + 1 >= argc) {
                println(stderr, "No argument is provided for ", option);
                usage(stderr);
                exit(1);
            }
            name = string_of_cstr(argv[options_end + 1]);
            options_end += 2;
        } else if (option == "-h"_s || option == "--help"_s) {
            usage(stdout);
            exit(0);
        } else if (option == "--"_s) {
            options_end += 1;
            break;
        } else {
            break;
        }
    }

    for (int i = options_end; i < argc; ++i) {
        enqueue(&wave, string_of_cstr(argv[i]));
    }

    if (wave.size == 0) {
        println(stderr, "No files are provided");
        usage(stderr);
        exit(1);
    }

    println(stdout, "digraph ", name, " {");
    while (wave.size != 0) {
        auto current_file = dequeue(&wave);
        if (is_visited(current_file)) continue;

        file_memory.size = 0;
        auto current_file_cstr = cstr_of_string(current_file, &file_memory);
        auto result = read_whole_file(current_file_cstr, &file_memory);
        if (result.is_error) {
            if (!silent) {
                println(stderr, "Could not open file `", current_file, "`: ", result.error);
            }
            continue;
        }

        auto content = result.unwrap;
        while (content.size > 0) {
            auto line = chop_by_delim(&content, '\n');
            auto include_path = parse_include_path(line);
            if (!include_path.is_error) {
                println(stdout, "    \"", current_file, "\" -> \"", include_path.unwrap, "\";");
                enqueue(&wave, copy(include_path.unwrap, &graph_memory));
            }
        }

        push(&visited, current_file);
    }

    println(stdout, "}");

    return 0;
}
