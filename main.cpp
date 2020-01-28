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

struct File_Path
{
    const char *unwrap;
};

File_Path file_path(const char *cstr)
{
    return { .unwrap = cstr };
}

void print1(FILE *stream, File_Path file_path)
{
    print1(stream, file_path.unwrap);
}

bool operator==(File_Path a, File_Path b)
{
    return strcmp(a.unwrap, b.unwrap) == 0;
}

static Fixed_Stack<File_Path, 1024> visited;
static Fixed_Queue<File_Path, 1024> wave;
static Fixed_Stack<File_Path, 1024> include_paths;

static Region<20_MiB> temp_memory;            // Temporary dynamic memory the lifetime of which is a single file processing. After the file has processed, everything in this memory is cleaned up.
static Region<20_MiB> perm_memory;            // Permanent dynamic memory the lifetime of which is the whole application. 

// FIXME: is_visited check is O(N)
bool is_visited(File_Path file_path)
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

struct Mebibytes
{
    unsigned long long unwrap;
};

Mebibytes mebibytes(unsigned long long x)
{
    return { .unwrap = x };
}

void print1(FILE *stream, Mebibytes mebibytes)
{
    print(stream, mebibytes.unwrap / 1024 / 1024, " MiB");
}

template <typename T, typename Error, typename... Prints>
T unwrap_or_exit(Result<T, Error> result, Prints... prints)
{
    if (result.is_error) {
        println(stderr, prints..., ": ", result.error);
        exit(1);
    }

    return result.unwrap;
}

#define FILE_PATH_DELIM "/"
#define FILE_PATH_DELIM_SIZE (sizeof(FILE_PATH_DELIM) - 1)

// TODO: join supports only Unix paths
// TODO: join is not variadic for multicomponent paths

template <typename Ator = Mator>
Result<File_Path, Errno> join(File_Path base, File_Path file, Ator *ator = &mator)
{
    size_t base_size = strlen(base.unwrap);
    size_t file_size = strlen(file.unwrap);
    Result<char*, Errno> result = alloc<char*>(ator, base_size + file_size + FILE_PATH_DELIM_SIZE + 1);
    if (result.is_error) return { .is_error = true, .error = result.error };

    memcpy(result.unwrap, base.unwrap, base_size);
    memcpy(result.unwrap + base_size, FILE_PATH_DELIM, FILE_PATH_DELIM_SIZE);
    memcpy(result.unwrap + base_size + FILE_PATH_DELIM_SIZE, file.unwrap, file_size);
    result.unwrap[base_size + file_size + FILE_PATH_DELIM_SIZE] = '\0';
    return {
        .is_error = false,
        .unwrap = { .unwrap = result.unwrap }
    };
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
        } else if (take(option, 2) == "-I"_s) {
            auto include_path = unwrap_or_exit(
                cstr_of_string(drop(option, 2), &perm_memory),
                "Not enough graph memory");
            push(&include_paths, file_path(include_path));
            options_end += 1;
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
        enqueue(&wave, file_path(argv[i]));
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

        temp_memory.size = 0;

        auto result = read_whole_file(current_file.unwrap, &temp_memory);
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
            if (include_path.is_error) {
                continue;
            }

            println(stdout, "    \"", current_file, "\" -> \"", include_path.unwrap, "\";");

            auto include_path_cstr = cstr_of_string(include_path.unwrap, &perm_memory);
            if (include_path_cstr.is_error) {
                println(stderr, "Traversing too many files, the memory limit has exceed");
                exit(1);
            }
            enqueue(&wave, file_path(include_path_cstr.unwrap));
        }

        push(&visited, current_file);
    }

    println(stdout, "}");

    return 0;
}
