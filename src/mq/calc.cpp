#include "mq/calc.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/float_chars.h"

namespace hailstone::mq {

namespace {

// MQ's eCalcOp. Number and the parentheses take no part in precedence.
enum class CalcOp : std::uint8_t {
    Number, Open, Close, Add, Subtract, Multiply, Divide, IntDivide, LogicalAnd, And, LogicalOr, Or, Xor, Equal,
    NotEqual, Greater, NotGreater, Less, NotLess, Modulus, Power, LogicalNot, Not, ShiftLeft, ShiftRight, Negate,
};

int precedence(CalcOp op) {
    switch (op) {
    case CalcOp::Number: case CalcOp::Open: case CalcOp::Close: return 0;
    case CalcOp::LogicalOr: return 1;
    case CalcOp::LogicalAnd: return 2;
    case CalcOp::Or: return 3;
    case CalcOp::Xor: return 4;
    case CalcOp::And: return 5;
    case CalcOp::Equal: case CalcOp::NotEqual: return 6;
    case CalcOp::Greater: case CalcOp::NotGreater: case CalcOp::Less: case CalcOp::NotLess: return 7;
    case CalcOp::ShiftLeft: case CalcOp::ShiftRight: return 8;
    case CalcOp::Add: case CalcOp::Subtract: return 9;
    case CalcOp::Multiply: case CalcOp::Divide: case CalcOp::IntDivide: case CalcOp::Modulus: return 10;
    case CalcOp::Power: return 11;
    case CalcOp::LogicalNot: case CalcOp::Not: case CalcOp::Negate: return 12;
    }
    return 0;
}

struct Item {
    CalcOp op = CalcOp::Number;
    double value = 0;
};

// A double as a 32-bit int the way x86's truncating conversion gives it: NaN and values outside int's range are
// 0x80000000 (MQ's (int) casts compile to it; in C++ they are undefined).
std::int32_t to_int(double d) {
    if (!(d > -2147483649.0 && d < 2147483648.0)) return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(d);
}

// Both stacks keep slot 0 as MQ's do: never pushed to, read as the top of an empty stack (0, or Number for the
// operator stack), and written by an operator that finds only one operand.
bool evaluate(const std::vector<Item>& list, double& result) {
    if (list.empty()) return false;
    std::vector<double> st(1, 0.0);
    std::size_t n = 0;
    auto top = [&]() -> double& { return st[n]; };
    auto push = [&](double v) {
        ++n;
        if (st.size() <= n) st.push_back(v);
        else st[n] = v;
    };
    auto pop = [&]() {
        if (n == 0) return false; // MQ: "Illegal arithmetic in calculation"
        --n;
        return true;
    };
    for (const Item& it : list) {
        if (it.op == CalcOp::Number) {
            push(it.value);
            continue;
        }
        if (it.op == CalcOp::Open || it.op == CalcOp::Close) continue; // an unclosed '(' left on the stack
        if (it.op == CalcOp::Negate) { top() = -top(); continue; }
        if (it.op == CalcOp::LogicalNot) { top() = to_int(top()) == 0 ? 1.0 : 0.0; continue; }
        if (it.op == CalcOp::Not) { top() = static_cast<double>(~to_int(top())); continue; }
        if (it.op == CalcOp::Divide && top() == 0.0) return false; // MQ checks before taking the operand
        if (it.op == CalcOp::IntDivide || it.op == CalcOp::Modulus) {
            const std::int32_t right = to_int(top());
            if (right == 0) return false;
            if (!pop()) return false;
            const std::int32_t left = to_int(top());
            if (left == std::numeric_limits<std::int32_t>::min() && right == -1) // a divide fault in MQ
                top() = it.op == CalcOp::IntDivide ? static_cast<double>(left) : 0.0;
            else
                top() = static_cast<double>(it.op == CalcOp::IntDivide ? left / right : left % right);
            continue;
        }
        const double right = top();
        if (!pop()) return false;
        double& left = top();
        const std::int32_t li = to_int(left), ri = to_int(right);
        const auto u = [](std::int32_t v) { return static_cast<std::uint32_t>(v); };
        switch (it.op) {
        case CalcOp::Add: left += right; break;
        case CalcOp::Subtract: left -= right; break;
        case CalcOp::Multiply: left *= right; break;
        case CalcOp::Divide: left /= right; break;
        case CalcOp::LogicalAnd: left = (left != 0.0 && right != 0.0) ? 1.0 : 0.0; break;
        case CalcOp::LogicalOr: left = (left != 0.0 || right != 0.0) ? 1.0 : 0.0; break;
        case CalcOp::Equal: left = left == right ? 1.0 : 0.0; break;
        case CalcOp::NotEqual: left = left != right ? 1.0 : 0.0; break;
        case CalcOp::Greater: left = left > right ? 1.0 : 0.0; break;
        case CalcOp::NotGreater: left = left <= right ? 1.0 : 0.0; break;
        case CalcOp::Less: left = left < right ? 1.0 : 0.0; break;
        case CalcOp::NotLess: left = left >= right ? 1.0 : 0.0; break;
        // x86 takes a shift count modulo 32; a left shift wraps.
        case CalcOp::ShiftLeft: left = static_cast<double>(static_cast<std::int32_t>(u(li) << (u(ri) & 31u))); break;
        case CalcOp::ShiftRight: left = static_cast<double>(li >> (u(ri) & 31u)); break;
        case CalcOp::And: left = static_cast<double>(li & ri); break;
        case CalcOp::Or: left = static_cast<double>(li | ri); break;
        case CalcOp::Xor: left = static_cast<double>(li ^ ri); break;
        case CalcOp::Power: left = std::pow(left, right); break;
        default: break;
        }
    }
    result = top();
    return true;
}

} // namespace

std::optional<double> calculate(std::string_view formula) {
    if (const auto nul = formula.find('\0'); nul != std::string_view::npos) formula = formula.substr(0, nul);
    if (formula.size() > 2047) return std::nullopt;
    // NULL, TRUE and FALSE become numbers of the same length ("0.00", "1.00", "0.000"), as MQ rewrites them in
    // place; the rest is upper case for the search.
    std::string text(formula);
    for (char& c : text)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const auto replace_all = [&](std::string_view word, std::string_view with) {
        for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at))
            text.replace(at, word.size(), with);
    };
    replace_all("NULL", "0.00");
    replace_all("TRUE", "1.00");
    replace_all("FALSE", "0.000");

    std::vector<Item> out;
    std::vector<CalcOp> st(1, CalcOp::Number); // slot 0: the top of an empty stack (MQ's zeroed array)
    std::size_t n = 0;
    std::string token;
    const auto finish = [&]() {
        if (token.empty()) return;
        double v = 0;
        float_from_chars(token.data(), token.data() + token.size(), v); // a failed read leaves 0, as MQ's does
        out.push_back({CalcOp::Number, v});
        token.clear();
    };
    const auto push = [&](CalcOp op) {
        ++n;
        if (st.size() <= n) st.push_back(op);
        else st[n] = op;
    };
    const auto new_op = [&](CalcOp op) {
        finish();
        while (n > 0 && st[n] != CalcOp::Open && precedence(st[n]) >= precedence(op)) out.push_back({st[n--], 0});
        push(op);
    };
    const auto at = [&](std::size_t i) { return i < text.size() ? text[i] : '\0'; };

    bool was_paren = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char next = at(i + 1);
        switch (c) {
        case ' ': continue; // (keeps was_paren)
        case '(':
            finish();
            push(CalcOp::Open);
            break;
        case ')':
            finish();
            while (st[n] != CalcOp::Open) {
                if (n == 0) return std::nullopt; // no '(' to close
                out.push_back({st[n--], 0});
            }
            --n;
            was_paren = true;
            continue;
        case '+':
            if (next != '+') new_op(CalcOp::Add);
            break;
        case '-':
            if (next == '-') {
                ++i;
                new_op(CalcOp::Add);
            } else {
                new_op(!token.empty() || was_paren ? CalcOp::Subtract : CalcOp::Negate);
            }
            break;
        case '*': new_op(CalcOp::Multiply); break;
        case '\\': new_op(CalcOp::IntDivide); break;
        case '/': new_op(CalcOp::Divide); break;
        case '%': new_op(CalcOp::Modulus); break;
        case '~': new_op(CalcOp::Not); break;
        case '|':
            if (next == '|') ++i;
            new_op(next == '|' ? CalcOp::LogicalOr : CalcOp::Or);
            break;
        case '&':
            if (next == '&') ++i;
            new_op(next == '&' ? CalcOp::LogicalAnd : CalcOp::And);
            break;
        case '^':
            if (next == '^') ++i;
            new_op(next == '^' ? CalcOp::Xor : CalcOp::Power);
            break;
        case '!':
            if (next == '=') ++i;
            new_op(next == '=' ? CalcOp::NotEqual : CalcOp::LogicalNot);
            break;
        case '=':
            if (next != '=') return std::nullopt;
            ++i;
            new_op(CalcOp::Equal);
            break;
        case '<':
            if (next == '=' || next == '<') ++i;
            new_op(next == '=' ? CalcOp::NotGreater : next == '<' ? CalcOp::ShiftLeft : CalcOp::Less);
            break;
        case '>':
            if (next == '=' || next == '>') ++i;
            new_op(next == '=' ? CalcOp::NotLess : next == '>' ? CalcOp::ShiftRight : CalcOp::Greater);
            break;
        default:
            if ((c >= '0' && c <= '9') || c == '.') {
                token += c;
                break;
            }
            return std::nullopt; // MQ: "Unparsable in Calculation"
        }
        was_paren = false;
    }
    finish();
    while (n > 0) out.push_back({st[n--], 0});

    double result = 0;
    if (!evaluate(out, result)) return std::nullopt;
    return result;
}

namespace {

std::string_view trim_ws(std::string_view s) {
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return s;
}

} // namespace

int int_from_text(std::string_view text, int fail) {
    text = trim_ws(text);
    std::from_chars(text.data(), text.data() + text.size(), fail); // a failure leaves `fail`
    return fail;
}

std::int64_t int64_from_text(std::string_view text, std::int64_t fail) {
    text = trim_ws(text);
    std::from_chars(text.data(), text.data() + text.size(), fail);
    return fail;
}

double double_from_text(std::string_view text, double fail) {
    text = trim_ws(text);
    float_from_chars(text.data(), text.data() + text.size(), fail);
    return fail;
}

std::string get_arg(std::string_view text, int n, bool csv, char separator) {
    if (const auto nul = text.find('\0'); nul != std::string_view::npos) text = text.substr(0, nul);
    const bool custom = separator != 0;
    const auto is_sep = [&](char c) {
        return custom ? c == separator : (c == ' ' || c == '\t' || (csv && c == ','));
    };
    std::size_t i = 0;
    const auto skip_seps = [&] {
        while (i < text.size() && is_sep(text[i])) ++i;
    };
    // MQ's GetNextArg: past n - 1 arguments.
    skip_seps();
    bool quoted = false;
    for (int k = n - 1; k > 0; --k) {
        if (i >= text.size()) return {}; // used up: every later argument is empty (n can be near 2^31)
        while (i < text.size() && (quoted || !is_sep(text[i]))) {
            if (text[i] == '"') quoted = !quoted;
            ++i;
        }
        if (quoted) return {}; // no closing quote: an empty argument
        skip_seps();
    }
    std::string out;
    quoted = false;
    for (; i < text.size() && (quoted || !is_sep(text[i])); ++i) {
        if (text[i] == '"') quoted = !quoted;
        else out += text[i];
    }
    return out;
}

} // namespace hailstone::mq
