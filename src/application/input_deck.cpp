#include "application/input_deck.hpp"
#include "graphics/smoke.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

// A cursor for this deck's JSON schema, rather than a general JSON object tree.
struct Cursor { std::string_view text; size_t offset{}; };

[[noreturn]] void error(const Cursor& c, std::string_view path, std::string_view message) {
    throw std::invalid_argument(std::string(path) + ": " + std::string(message) +
        " (byte " + std::to_string(c.offset + 1) + ")");
}

void whitespace(Cursor& c) {
    while (c.offset < c.text.size() &&
        (c.text[c.offset] == ' ' || c.text[c.offset] == '\t' ||
         c.text[c.offset] == '\r' || c.text[c.offset] == '\n')) ++c.offset;
}

bool take(Cursor& c, char token) {
    whitespace(c);
    if (c.offset == c.text.size() || c.text[c.offset] != token) return false;
    ++c.offset;
    return true;
}

void expect(Cursor& c, char token, std::string_view path) {
    if (!take(c, token)) error(c, path, std::string("expected '") + token + "'");
}

unsigned hex_quad(Cursor& c, std::string_view path) {
    unsigned value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (c.offset == c.text.size()) error(c, path, "incomplete Unicode escape");
        const char ch = c.text[c.offset++];
        unsigned digit;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else error(c, path, "invalid Unicode escape");
        value = value * 16 + digit;
    }
    return value;
}

void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) {
        out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63));
        out += char(0x80 | (cp & 63));
    } else {
        out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 63));
        out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63));
    }
}

std::string key(Cursor& c, std::string_view path) {
    expect(c, '"', path);
    std::string result;
    while (c.offset < c.text.size()) {
        const unsigned char ch = c.text[c.offset++];
        if (ch == '"') return result;
        if (ch < 0x20) error(c, path, "unescaped control character in key");
        if (ch != '\\') { result += char(ch); continue; }
        if (c.offset == c.text.size()) error(c, path, "incomplete string escape");
        switch (c.text[c.offset++]) {
        case '"': result += '"'; break;
        case '\\': result += '\\'; break;
        case '/': result += '/'; break;
        case 'b': result += '\b'; break;
        case 'f': result += '\f'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'u': {
            unsigned cp = hex_quad(c, path);
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (c.text.substr(c.offset, 2) != "\\u") error(c, path, "missing low surrogate");
                c.offset += 2;
                const unsigned low = hex_quad(c, path);
                if (low < 0xdc00 || low > 0xdfff) error(c, path, "invalid low surrogate");
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            } else if (cp >= 0xdc00 && cp <= 0xdfff) error(c, path, "unpaired low surrogate");
            append_utf8(result, cp);
            break;
        }
        default: error(c, path, "invalid string escape");
        }
    }
    error(c, path, "unterminated key");
}

bool digit(const Cursor& c) {
    return c.offset < c.text.size() && c.text[c.offset] >= '0' && c.text[c.offset] <= '9';
}

double number(Cursor& c, std::string_view path) {
    whitespace(c);
    const size_t start = c.offset;
    if (c.offset < c.text.size() && c.text[c.offset] == '-') ++c.offset;
    if (!digit(c)) error(c, path, "expected a number");
    if (c.text[c.offset++] != '0') while (digit(c)) ++c.offset;
    else if (digit(c)) error(c, path, "leading zero in number");
    if (c.offset < c.text.size() && c.text[c.offset] == '.') {
        ++c.offset;
        if (!digit(c)) error(c, path, "expected digits after decimal point");
        while (digit(c)) ++c.offset;
    }
    if (c.offset < c.text.size() && (c.text[c.offset] == 'e' || c.text[c.offset] == 'E')) {
        ++c.offset;
        if (c.offset < c.text.size() && (c.text[c.offset] == '+' || c.text[c.offset] == '-')) ++c.offset;
        if (!digit(c)) error(c, path, "expected exponent digits");
        while (digit(c)) ++c.offset;
    }
    double value{};
    const auto parsed = std::from_chars(c.text.data() + start, c.text.data() + c.offset, value);
    if (parsed.ec != std::errc{} || !std::isfinite(value)) error(c, path, "number is outside the finite numeric range");
    return value;
}

void mark(Cursor& c, unsigned& seen, unsigned bit, std::string_view path) {
    if (seen & bit) error(c, path, "duplicate key");
    seen |= bit;
}

std::array<float, 3> source(Cursor& c, const std::string& path) {
    expect(c, '{', path);
    std::array<float, 3> result{};
    unsigned seen{};
    if (!take(c, '}')) {
        do {
            const auto name = key(c, path);
            const auto field = path + "." + name;
            const auto axis = std::string_view("xyz").find(name);
            if (name.size() != 1 || axis == std::string_view::npos) error(c, field, "unknown field");
            mark(c, seen, 1u << axis, field);
            expect(c, ':', field);
            const double value = number(c, field);
            if (std::abs(value) > std::numeric_limits<float>::max()) error(c, field, "coordinate exceeds simulator numeric range");
            result[axis] = static_cast<float>(value);
        } while (take(c, ','));
        expect(c, '}', path);
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!(seen & (1u << axis))) error(c, path + "." + "xyz"[axis], "required field is missing");
    return result;
}

} // namespace

application::InputDeck application::parse_input_deck(std::string_view text) {
    Cursor c{text};
    InputDeck result;
    unsigned seen{};
    expect(c, '{', "input");
    if (!take(c, '}')) {
        do {
            const auto name = key(c, "input");
            if (name == "domain_box_opacity") {
                mark(c, seen, 1, name);
                expect(c, ':', name);
                const double value = number(c, name);
                if (value < 0 || value > 1) error(c, name, "must be between 0 and 1");
                result.domain_box_opacity = static_cast<float>(value);
            } else if (name == "sources") {
                mark(c, seen, 2, name);
                expect(c, ':', name);
                expect(c, '[', name);
                if (!take(c, ']')) {
                    do {
                        result.sources.push_back(source(c, "sources[" + std::to_string(result.sources.size()) + "]"));
                    } while (take(c, ','));
                    expect(c, ']', name);
                }
            } else error(c, name, "unknown field");
        } while (take(c, ','));
        expect(c, '}', "input");
    }
    if (!(seen & 1)) error(c, "domain_box_opacity", "required field is missing");
    if (!(seen & 2)) error(c, "sources", "required field is missing");
    whitespace(c);
    if (c.offset != text.size()) error(c, "input", "unexpected content after deck");
    return result;
}

application::InputDeck application::load_input_deck(std::string_view filename) {
    std::ifstream file(std::string(filename), std::ios::binary);
    if (!file) throw std::runtime_error(std::string(filename) + ": cannot open input deck");
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) throw std::runtime_error(std::string(filename) + ": cannot read input deck");
    try { return parse_input_deck(text); }
    catch (const std::invalid_argument& e) { throw std::invalid_argument(std::string(filename) + ": " + e.what()); }
}

application::InputDeck application::default_input_deck(const graphics::FieldDomain& domain) {
    return {0.65f, {simulation::smoke_parameters(domain).center}};
}

void application::validate_input_deck(const InputDeck& deck, const graphics::FieldDomain& domain) {
    if (!std::isfinite(deck.domain_box_opacity) || deck.domain_box_opacity < 0 || deck.domain_box_opacity > 1)
        throw std::invalid_argument("domain_box_opacity: must be a finite number between 0 and 1");
    const double radius = simulation::smoke_parameters(domain).radius;
    for (size_t i = 0; i < deck.sources.size(); ++i) {
        for (size_t axis = 0; axis < 3; ++axis) {
            const double coordinate = deck.sources[i][axis];
            const double minimum = domain.origin[axis], maximum = minimum + double(domain.dimensions[axis]) * domain.spacing;
            const std::string path = "sources[" + std::to_string(i) + "]." + "xyz"[axis];
            if (!std::isfinite(coordinate)) throw std::invalid_argument(path + ": coordinate must be finite");
            if (coordinate - radius < minimum || coordinate + radius > maximum)
                throw std::invalid_argument(path + ": emitter extends beyond the domain");
        }
    }
}
