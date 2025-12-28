#include <chrono>
#include <thread>
#include <cstdarg>

#include <gtest/gtest.h>
#include "exprtk.hpp"
#include "expression.h"

enum class Color { RED, GREEN, YELLOW, RESET };

inline const char* ColorCode(Color c) {
    switch (c) {
    case Color::RED: return "\x1B[31m";
    case Color::GREEN: return "\x1B[32m";
    case Color::YELLOW: return "\x1B[33m";
    default: return "\x1B[0m";
    }
}

inline void ColoredPrint(Color c, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::cout << ColorCode(c);
    // Simple use of vprintf to stdout (or format into std::string with vsnprintf)
    vprintf(fmt, args);
    std::cout << ColorCode(Color::RESET);
    va_end(args);
}

TEST(Expressions, Playground){
    exprtk::symbol_table<float> symbol_table;
    exprtk::expression<float> expression;
    exprtk::parser<float> parser;

    std::string expression_string = "43.0";
    float x;

    //symbol_table.add_variable("x", x);
    //symbol_table.add_constants();

    //expression.register_symbol_table(symbol_table);

    parser.compile(expression_string, expression);
    for (x = -5.0; x <= 5.0; x += 1.0)
    {
        const float y = expression.value();
        ColoredPrint(Color::YELLOW, "%19.15f\t%19.15f", x, y);
    }
}

TEST(Expressions, CompileFail) {
    std::string expression_string = "clamp(-1.0, x, 3.0"; // Missing closing parenthesis

    float x;

    exprtk::symbol_table<float> symbol_table;
    symbol_table.add_variable("x", x);
    symbol_table.add_constants();

    exprtk::expression<float> expression;
    expression.register_symbol_table(symbol_table);

    exprtk::parser<float> parser;
    bool ret = parser.compile(expression_string, expression);

    if (!ret)
    {
        ColoredPrint(Color::YELLOW, "Error: %s", parser.error().c_str());
    }
    ASSERT_FALSE(ret);
}

TEST(Expressions, ClassExpr) {
    double x = 2.95;
    
    Expression<double> expr;
    expr.addVariable("x", x);

    size_t ret = expr.compile("x + 10.0");
    if (ret) {
        for (std::size_t i = 0; i < ret; ++i)
        {
            ColoredPrint(Color::YELLOW, "Error: %s", expr.getError(i).diagnostic.c_str());
        }
    }

    ASSERT_EQ(ret, 0);
    const double y = expr.evaluate();
    ColoredPrint(Color::YELLOW, "%19.15f\t%19.15f", x, y);
    ASSERT_NEAR(y, 12.95, 0.1);
}

TEST(Expressions, ClassExprFail) {
    double x = 2.95;

    Expression<double> expr;
    expr.addVariable("x", x);

    size_t ret = expr.compile("clamp(1.3, x");
    if (ret) {
        for (std::size_t i = 0; i < ret; ++i)
        {
            ColoredPrint(Color::YELLOW, "Error: %s", expr.getError(i).diagnostic.c_str());
        }
    }

    ASSERT_NE(ret, 0);
    const double y = expr.evaluate();
    ColoredPrint(Color::YELLOW, "%19.15f\t%19.15f", x, y);
}