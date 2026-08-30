// 极简异常基类：只保留 assertion，业务异常统一走 std::runtime_error。
// 全新编写：只依赖 <stdexcept>，不引入 curl/sqlite 等无关依赖。
#pragma once
#include <stdexcept>
#include <string>

class Exception : public std::runtime_error
{
public:
    Exception(const char *file, int line, const std::string &message)
        : std::runtime_error(message + " (in " + file + ":" + std::to_string(line) + ")") {}
};

#define THROW_EXCEPTION(message) throw Exception(__FILE__, __LINE__, message)
#define ASSERT(condition) \
    if (!(condition)) THROW_EXCEPTION("Assertion failed: " #condition)