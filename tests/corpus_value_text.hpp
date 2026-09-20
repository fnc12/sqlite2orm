#pragma once

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

/**
 *  The one rendering of a queried value the corpus compares on, shared by the two sides that have
 *  to agree: the test, which asks SQLite what a query returns, and the program the test generates,
 *  compiles and runs, which asks the same question through sqlite_orm. Both sides include this
 *  header -- the generated program gets `tests/` on its include path -- so a value can only differ
 *  because the generated code returned something else, never because the two sides spelled the
 *  same number differently.
 */
namespace corpus_test_helpers {

    /**
     *  A REAL the way SQLite's own text conversion writes it: fifteen significant digits.
     *
     *  SQLite writes a whole REAL with a trailing `.0` and this does not, because sqlite_orm picks
     *  the result type of an expression on its own and gives `sum()` or `abs()` a `double` where
     *  SQLite hands back an INTEGER. That is a difference between two C++ types, not between two
     *  values, and the corpus is here to catch the second kind.
     */
    inline std::string realText(double value) {
        if (std::isinf(value)) {
            return value < 0 ? "-Inf" : "Inf";
        }
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.15g", value);
        return buffer;
    }

    inline std::string cell(std::nullptr_t);
    inline std::string cell(const std::string&);
    inline std::string cell(bool);
    inline std::string cell(double);
    template<class T>
    std::string cell(const T&);
    template<class T>
    std::string cell(const std::optional<T>&);
    template<class T>
    std::string cell(const std::unique_ptr<T>&);
    template<class... Ts>
    std::string cell(const std::tuple<Ts...>&);

    inline std::string cell(std::nullptr_t) {
        return "NULL";
    }

    inline std::string cell(const std::string& value) {
        return value;
    }

    inline std::string cell(bool value) {
        return value ? "1" : "0";
    }

    inline std::string cell(double value) {
        return realText(value);
    }

    template<class T>
    std::string cell(const T& value) {
        return std::to_string(value);
    }

    template<class T>
    std::string cell(const std::optional<T>& value) {
        return value ? cell(*value) : cell(nullptr);
    }

    template<class T>
    std::string cell(const std::unique_ptr<T>& value) {
        return value ? cell(*value) : cell(nullptr);
    }

    /** The columns of one row, `|`-separated, the way the test writes an expected row. */
    template<class... Ts>
    std::string cell(const std::tuple<Ts...>& row) {
        std::string text;
        bool first = true;
        std::apply(
            [&text, &first](const Ts&... values) {
                (((text += first ? "" : "|"), (text += cell(values)), (first = false)), ...);
            },
            row);
        return text;
    }

}  // namespace corpus_test_helpers
