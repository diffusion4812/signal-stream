#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include "exprtk.hpp"  // Include the exprtk header

template <typename T = double,
    typename = typename std::enable_if<
    std::is_same<T, float>::value ||
    std::is_same<T, double>::value>::type>
class Expression {
public:
    Expression() {
        symbol_table_.add_constants();
    }

    // Add a variable to the symbol table
    bool addVariable(const std::string& name, double& value) {
        if (variables_.find(name) != variables_.end()) {
            return false;  // Variable already exists
        }
        variables_[name] = &value;
        symbol_table_.add_variable(name, value);
        return true;
    }

    size_t compile(const std::string& expression_str) {
        expression_.register_symbol_table(symbol_table_);
        bool success = parser_.compile(expression_str, expression_);
        return parser_.error_count();
    }

    T evaluate() {
        return expression_.value();
    }

    exprtk::parser_error::type getError(size_t i) {
        return parser_.get_error(i);
    }

    std::string getLastError() const {
        return parser_.error();
    }

private:
    exprtk::expression<T> expression_;
    exprtk::parser<T> parser_;
    exprtk::symbol_table<T> symbol_table_;
    std::unordered_map<std::string, T*> variables_;
};