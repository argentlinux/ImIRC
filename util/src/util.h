#ifndef util_h__
#define util_h__

#include <string>
#include <stdint.h>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <format>
#include "boost/log/trivial.hpp"

#define BOOST_LOGFMT(level, fmt_str, ...) \
    BOOST_LOG_TRIVIAL(level) << std::format(fmt_str, ##__VA_ARGS__)

#define USE_GLFW

namespace util
{
	std::string BytesToHex(const std::string& Bytes);
	std::string HexToBytes(const std::string& Hex);
	std::string to_upper(const std::string& str);
	std::string to_lower(const std::string& str);
	bool is_numeric(const std::string& str);
	std::string RunProcess(std::string command, std::string arguments);
}

#endif // util_h__
