/**
 * @file picomath.hpp
 * @author Cesar Guirao Robles (a.k.a. Nitro) <cesar@no2.es>
 * @brief Math expression evaluation. BSD 3-Clause License
 * @version 1.1.0
 * @date 2022-06-01
 *
 * @copyright Copyright (c) 2022, Cesar Guirao Robles (a.k.a. Nitro) <cesar@no2.es>
 *
 */
#ifndef PICOMATH_HPP
#define PICOMATH_HPP

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace picomath {

// Enable to use correct and precise float parsing
// #define PM_USE_PRECISE_FLOAT_PARSING

// Enable to use floats instead of doubles
// #define PM_USE_FLOAT

#ifndef PM_MAX_ARGUMENTS
#define PM_MAX_ARGUMENTS 8
#endif

#define PM_LIKELY(x)   __builtin_expect((x), 1)
#define PM_UNLIKELY(x) __builtin_expect((x), 0)
#define PM_INLINE      inline __attribute__((always_inline))

class Result;
class Expression;
class PicoMath;

#ifdef PM_USE_FLOAT
using number_t = float;
#ifdef PM_USE_PRECISE_FLOAT_PARSING
#define PM_STR_TO_FLOAT strtof
#endif
#else
using number_t = double;
#ifdef PM_USE_PRECISE_FLOAT_PARSING
#define PM_STR_TO_FLOAT strtod
#endif
#endif
using argument_list_t        = std::array<number_t, PM_MAX_ARGUMENTS>;
using error_t                = std::string;
using custom_function_many_t = Result (*)(size_t argc, const argument_list_t &list);
using custom_function_1_t    = number_t (*)(number_t);

class Result {
    friend class Expression;

    number_t                 result;
    std::unique_ptr<error_t> error;

  public:
    Result(number_t value = 0) noexcept : result(value), error() { // NOLINT
    }

    Result(std::string &&description) noexcept
        : result(0), error(std::make_unique<error_t>(std::move(description))) { // NOLINT
    }

    [[nodiscard]] auto isError() const -> bool {
        return error != nullptr;
    }

    [[nodiscard]] auto isOk() const -> bool {
        return error == nullptr;
    }

    [[nodiscard]] auto getError() const -> const char * {
        return error->c_str();
    }

    [[nodiscard]] auto getResult() const -> number_t {
        return result;
    }
};

#define PM_FUNCTION_2(fun)                                                                                             \
    [](size_t argc, const argument_list_t &args) -> Result {                                                           \
        if (argc != 2) {                                                                                               \
            return {"Two arguments needed"};                                                                           \
        }                                                                                                              \
        return fun(args[0], args[1]);                                                                                  \
    }

class PicoMath {
    friend Expression;

    struct Function {
        enum Type
        {
            FunctionMany = 0,
            Function1    = 1
        } type;
        custom_function_many_t many;
        custom_function_1_t    f1;
    };

    std::map<std::string, number_t, std::less<>> variables{};
    std::map<std::string, number_t, std::less<>> units{};
    std::map<std::string, Function, std::less<>> functions{};

  public:
    /**
     * @brief Adds a variable to the parsing context.
     * PicoMath returns a reference to entry inside the map of variables, that can be changed
     * during multiple evaluations
     *
     * @param name Name of the variable
     * @return number_t& Reference to variable's value
     */
    auto addVariable(const std::string &name) -> number_t & {
        return variables[name];
    }

    /**
     * @brief Adds a scale unit to the parsing context
     * PicoMath returns a reference to entry inside the map of variables, that can be changed
     * during multiple evaluations.
     *
     * @param name Name of the unit
     * @return number_t& Reference to the unit's scale value
     */
    auto addUnit(const std::string &name) -> number_t & {
        return units[name];
    }

    /**
     * @brief Adds a custom function to the parsing context
     * This overload allows the user to pass a function that can handle multiple arguments.
     *
     * @param name Name of the function
     * @param func Function pointer
     */
    auto addFunction(const std::string &name, custom_function_many_t func) -> void {
        auto &function = functions[name];
        function.type  = Function::Type::FunctionMany;
        function.many  = func;
    }

    /**
     * @brief Adds a custom function to the parsing context
     *
     * @param name Name of the function
     * @param func Function pointer
     */
    auto addFunction(const std::string &name, custom_function_1_t func) -> void {
        auto &function = functions[name];
        function.type  = Function::Type::Function1;
        function.f1    = func;
    }

    PicoMath() {
        // Constants
        addVariable("pi") = static_cast<number_t>(M_PI);
        addVariable("e")  = static_cast<number_t>(M_E);

        // Built-in functions
        addFunction("abs", std::abs);
        addFunction("ceil", std::ceil);
        addFunction("floor", std::floor);
        addFunction("round", std::round);
        addFunction("ln", std::log);
        addFunction("log", std::log10);
        addFunction("cos", std::cos);
        addFunction("sin", std::sin);
        addFunction("acos", std::acos);
        addFunction("asin", std::asin);
        addFunction("cosh", std::cosh);
        addFunction("sinh", std::sinh);
        addFunction("tan", std::tan);
        addFunction("tanh", std::tanh);
        addFunction("sqrt", std::sqrt);
        addFunction("atan2", PM_FUNCTION_2(std::atan2));
        addFunction("pow", PM_FUNCTION_2(std::pow));
        addFunction("min", [](size_t argc, const argument_list_t &args) -> Result {
            number_t result{std::numeric_limits<number_t>::max()};
            size_t   i = 0;
            while (i < argc) {
                result = std::min(args[i], result);
                i++;
            }
            return result;
        });
        addFunction("max", [](size_t argc, const argument_list_t &args) -> Result {
            number_t result{std::numeric_limits<number_t>::min()};
            size_t   i = 0;
            while (i < argc) {
                result = std::max(args[i], result);
                i++;
            }
            return result;
        });
    }

    /**
     * @brief Evaluates the expression and returns a value or an error if the expression is invalid
     *
     * @param expression Expression to evaluate
     * @return Result Result containing the result value or an error
     */
    auto evalExpression(const char *expression) -> Result;

    /**
     * @brief Creates a Expression that can be used to evaluate multiple expression separated by commas.
     *
     * @param expression MultiExpression to evaluate
     * @return Expression Expression object maintaining the context of the multi-evaluation
     */
    auto evalMultiExpression(const char *expression) -> Expression;
};

class Expression {
    friend PicoMath;

    const PicoMath &context;
    const char *    originalStr{};
    const char *    str{};

    Expression(const PicoMath &picomathContext, const char *expression)
        : context(picomathContext), originalStr(expression), str(expression) {
    }

    auto evalSingle() -> Result {
        Result ret = evalExpression();
        consumeSpace();
        if (PM_LIKELY(isEOF())) {
            return ret;
        }
        return generateError("Invalid characters after expression");
    }

  public:
    /**
     * @brief Evaluates the next expression in the multi expression context
     *
     * @param outResult Result of the evaluation
     * @return true The evaluation succeeded and the outResult parameter contains a valid output value.
     * @return false There are no more expressions to evaluate
     */
    auto evalNext(Result *outResult) -> bool {
        consumeSpace();
        if (isEOF()) {
            return false;
        }
        *outResult = evalExpression();
        consumeSpace();
        if (peek() == ',') {
            consume();
            consumeSpace();
        }
        return true;
    }

  private:
    auto generateError(const char *error, std::string_view identifier = {}) const -> Result {
        std::string out = "In character " + std::to_string(static_cast<int>(str - originalStr - 1)) + ": ";
        out += error;
        if (identifier.empty()) {
            out += " found: ";
            out += isEOF() ? "End of string" : str;
        } else {
            out += ' ';
            out += '`';
            out += identifier;
            out += '`';
        }
        return out;
    }

    PM_INLINE auto evalExpression() -> Result {
        consumeSpace();
        if (PM_UNLIKELY(isEOF())) {
            return generateError("Unexpected end of the string");
        }
        return parseAddition();
    }

    PM_INLINE auto consume() -> char {
        return *str++;
    }

    [[nodiscard]] PM_INLINE auto peek() const -> char {
        return *str;
    }

    PM_INLINE void consumeSpace() {
        while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
            consume();
        }
    }

    [[nodiscard]] PM_INLINE auto isDigit() const -> bool {
        return *str >= '0' && *str <= '9';
    }

    [[nodiscard]] PM_INLINE auto isAlpha() const -> bool {
        return (*str >= 'a' && *str <= 'z') || (*str >= 'A' && *str <= 'Z') || (*str == '_');
    }

    [[nodiscard]] PM_INLINE auto isUnitChar() const -> bool {
        return isAlpha() || *str == '%';
    }

    [[nodiscard]] PM_INLINE auto isEOF() const -> bool {
        return *str == 0;
    }

    [[nodiscard]] PM_INLINE auto isSign() const -> bool {
        return *str == '+' || *str == '-';
    }

    [[nodiscard]] PM_INLINE auto isExponent() const -> bool {
        // This doesn't read out of bounds because:
        // if first character is 'e' or 'E', next character can be a valid character
        // or zero if it's the end of the string
        return (*str == 'e' || *str == 'E') &&
               (*(str + 1) == '+' || *(str + 1) == '-' || (*(str + 1) >= '0' && *(str + 1) <= '9'));
    }

    PM_INLINE auto parseFunction(std::string_view identifier) noexcept -> Result {
        auto f = context.functions.find(identifier);
        if (PM_UNLIKELY(f == context.functions.end())) {
            return generateError("Unknown function", identifier);
        }

        // Consume '('
        consume();
        consumeSpace();

        std::array<number_t, PM_MAX_ARGUMENTS> arguments{};
        size_t                                 argc = 0;

        if (peek() != ')') {
            while (true) {
                if (PM_UNLIKELY(argc == PM_MAX_ARGUMENTS)) {
                    return generateError("Too many arguments");
                }
                Result argument = evalExpression();
                if (PM_UNLIKELY(argument.isError())) {
                    return argument;
                }
                arguments[argc] = argument.result;
                argc++;
                consumeSpace();
                if (peek() != ',') {
                    break;
                }
                consume();
            }
        }

        if (PM_UNLIKELY(peek() != ')')) {
            return generateError("Expected ')'");
        }
        consume();

        if (f->second.type == PicoMath::Function::Type::Function1) {
            if (PM_UNLIKELY(argc != 1)) {
                return generateError("One argument required");
            }
            return {f->second.f1(arguments[0])};
        }
        Result ret = f->second.many(argc, arguments);
        if (PM_UNLIKELY(ret.isError())) {
            // Improve information in errors generated inside functions
            return generateError(ret.getError(), identifier);
        }
        return ret;
    }

    PM_INLINE auto parseParenthesized() -> Result {
        consume();
        consumeSpace();
        Result exp = evalExpression();
        if (PM_UNLIKELY(exp.isError())) {
            return exp;
        }
        consumeSpace();
        // consume ')'
        if (PM_UNLIKELY(peek() != ')')) {
            return generateError("Expected ')'");
        }
        consume();
        return exp;
    }

    auto parsePrefixUnaryOperator() -> Result {
        char op = consume();
        consumeSpace();
        Result unary = parseSubExpression();
        if (PM_UNLIKELY(unary.isError())) {
            return unary;
        }
        if (op == '-') {
            unary.result = -unary.result;
        }
        return unary;
    }

    PM_INLINE auto parseVariableOrFunction() noexcept -> Result {
        const char *start = str;
        size_t      size  = 0;
        do {
            consume();
            size++;
        } while (isAlpha());

        std::string_view identifier{start, size};
        consumeSpace();
        if (peek() == '(') {
            // function call
            return parseFunction(identifier);
        }
        auto f = context.variables.find(identifier);
        if (PM_UNLIKELY(f == context.variables.end())) {
            return generateError("Unknown variable", identifier);
        }
        return {f->second};
    }

    PM_INLINE auto parseNumber() noexcept -> Result {
        number_t ret = 0;

#if defined(PM_USE_PRECISE_FLOAT_PARSING)
        char *end;
        ret = PM_STR_TO_FLOAT(str, &end);
        if (PM_UNLIKELY(errno == ERANGE)) {
            std::string_view identifier{str, static_cast<size_t>(end - str)};
            return generateError("Float out of range", identifier);
        }
        str = end;
#else
        while (PM_LIKELY(isDigit())) {
            ret = ret * 10 + (*str - '0');
            consume();
        }
        // Decimal point
        if (peek() == '.') {
            consume();
            number_t weight = 1;
            while (isDigit()) {
                weight /= 10;
                ret += (*str - '0') * weight;
                consume();
            }
        }
        if (PM_UNLIKELY(isExponent())) {
            consume();
            bool sign = false;
            if (isSign()) {
                if (consume() == '-') {
                    sign = true;
                }
            }
            int exp = 0;
            while (PM_LIKELY(isDigit())) {
                exp = exp * 10 + (*str - '0');
                consume();
            }
            ret *= std::pow(static_cast<number_t>(10), sign ? -exp : exp);
        }
#endif
        // Space before units
        consumeSpace();

        // Units and percentage
        if (isUnitChar()) {
            const char *start = str;
            size_t      size  = 0;
            do {
                consume();
                size++;
            } while (isUnitChar());

            std::string_view identifier{start, size};

            auto f = context.units.find(identifier);
            if (PM_UNLIKELY(f == context.units.end())) {
                return generateError("Unknown unit", identifier);
            }
            ret = ret * f->second;
        }
        return ret;
    }

    PM_INLINE auto parseSubExpression() -> Result {
        if (isDigit() || peek() == '.') {
            // Number
            return parseNumber();
        }
        if (peek() == '(') {
            // Parenthesized expression
            return parseParenthesized();
        }
        if (peek() == '-' || peek() == '+') {
            // Prefix unary operator
            return parsePrefixUnaryOperator();
        }
        if (isAlpha()) {
            // Variable or function
            return parseVariableOrFunction();
        }
        return generateError("Invalid character");
    }

    auto parseAddition() -> Result {
        consumeSpace();
        Result left = parseMultiplication();
        if (PM_UNLIKELY(left.isError())) {
            return left;
        }
        consumeSpace();
        while (*str == '+' || *str == '-') {
            char op = consume();
            consumeSpace();
            Result right = parseMultiplication();
            if (PM_UNLIKELY(right.isError())) {
                return right;
            }
            if (op == '+') {
                left.result += right.result;
            } else {
                left.result -= right.result;
            }
            consumeSpace();
        }
        return left;
    }

    auto parseMultiplication() -> Result {
        consumeSpace();
        Result left = parseSubExpression();
        if (PM_UNLIKELY(left.isError())) {
            return left;
        }
        consumeSpace();
        while (*str == '*' || *str == '/') {
            char op = consume();
            consumeSpace();
            Result right = parseSubExpression();
            if (PM_UNLIKELY(right.isError())) {
                return right;
            }
            if (op == '*') {
                left.result *= right.result;
            } else if (op == '/') {
                left.result /= right.result;
            }
            consumeSpace();
        }
        return left;
    }
};

inline auto PicoMath::evalExpression(const char *expression) -> Result {
    Expression exp(*this, expression);
    return exp.evalSingle();
}

inline auto PicoMath::evalMultiExpression(const char *expression) -> Expression {
    return {*this, expression};
}

} // namespace picomath
#endif
