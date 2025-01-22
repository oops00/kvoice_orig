#pragma once
#include <exception>
#include <string>
#include <format>
#include <utility>

namespace kvoice {
class voice_exception : public std::exception {
public:
    template <typename... Ts>
    static voice_exception create_formatted(const std::string &fmt, Ts&&...args);

    explicit voice_exception(const std::string &msg)
        : std::exception(msg.c_str()) {}
};

/**
 * @brief creates new @p voice_exception object with formatted message
 * @tparam Ts types of @p args
 * @param fmt format string
 * @param args args that are passed to format function
 * @return new @p voice_exception object
 */
template <typename ...Ts>
inline voice_exception voice_exception::create_formatted(const std::string &fmt, Ts&& ...args) {
    return voice_exception{ std::vformat(fmt, std::make_format_args(std::forward<Ts&>(args)...)) };
}

}
