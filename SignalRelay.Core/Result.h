#pragma once
// Explicit result/error type used instead of exceptions across module boundaries.

#include <string>
#include <utility>
#include <variant>

namespace sr {

struct Error {
    std::string code;     // stable machine-readable identifier
    std::string message;  // human-readable, never contains secrets
};

template <typename T>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}
    Result(Error e) : v_(std::move(e)) {}
    static Result fail(std::string code, std::string message) { return Result(Error{std::move(code), std::move(message)}); }

    bool ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }
    T& value() { return std::get<0>(v_); }
    const T& value() const { return std::get<0>(v_); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    const Error& error() const { return std::get<1>(v_); }

private:
    std::variant<T, Error> v_;
};

struct Unit {};
using Status = Result<Unit>;
inline Status okStatus() { return Status(Unit{}); }
inline Status failStatus(std::string code, std::string message) { return Status(Error{std::move(code), std::move(message)}); }

}  // namespace sr
