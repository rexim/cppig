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

template <typename T, size_t Capacity>
struct Fixed_Stack
{
    size_t size = 0;
    T elements[Capacity];
};

template <typename T, size_t Capacity>
void push(Fixed_Stack<T, Capacity> *stack, T element)
{
    assert(stack->size < Capacity);
    stack->elements[stack->size++] = element;
}

struct Edge
{
    String a;
    String b;
};

void print1(FILE *stream, Edge edge)
{
    print(stream, edge.a, " -> ", edge.b);
}

Fixed_Stack<String, 1024> visited;
Fixed_Stack<Edge, 1024> edges;

static Region<20_Mb> file_memory;
static Region<20_Mb> graph_memory;

int main(int argc, char *argv[])
{
    if (argc < 2) {
        println(stderr, "Usage: ", argv[0], " <files...>");
        exit(1);
    }

    for (int i = 1; i < argc; ++i) {
        file_memory.size = 0;
        String current_file = copy(string_of_cstr(argv[i]), &graph_memory);

        auto result = read_whole_file(argv[i], &file_memory);
        if (result.is_error) {
            println(stderr, "Could not open file `", argv[i], "`: ", result.error);
            continue;
        }
        auto content = result.unwrap;

        auto unparsed = content;
        while (unparsed.size > 0) {
            auto line = chop_by_delim(&unparsed, '\n');
            auto include_path = parse_include_path(line);
            if (!include_path.is_error) {
                Edge edge {
                    .a = current_file,
                    .b = copy(include_path.unwrap, &graph_memory)
                };
                push(&edges, edge);
            }
        }

        push(&visited, current_file);
    }

    for (size_t i = 0; i < edges.size; ++i) {
        println(stdout, edges.elements[i]);
    }

    return 0;
}
