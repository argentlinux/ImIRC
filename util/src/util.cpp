#include "util.h"
#include <algorithm>
#include <cctype>
#include <array>
#include <memory>
#include <stdexcept>

namespace util
{
#if defined(LINUX)
	std::string RunProcess(std::string command, std::string arguments)
	{
		std::string full_command = command + " " + arguments;
		std::array<char, 128> buffer;
		std::string result;
		std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_command.c_str(), "r"), pclose);

		if (!pipe)
			throw std::runtime_error("popen() failed!");

		while (fgets(buffer.data(), (int)buffer.size(), pipe.get()) != nullptr)
			result += buffer.data();

		return result;
	}
#else
	std::string RunProcess(std::string /*command*/, std::string /*arguments*/)
	{
		return {};
	}
#endif

	std::string BytesToHex(const std::string& Bytes)
	{
		std::stringstream ss;
		ss << std::hex << std::setfill('0');
		for (unsigned char byte : Bytes)
			ss << std::setw(2) << static_cast<int>(byte);
		return ss.str();
	}

	std::string HexToBytes(const std::string& Hex)
	{
		std::string bytes;
		for (size_t i = 0; i < Hex.length(); i += 2)
		{
			std::string byte_string = Hex.substr(i, 2);
			unsigned char byte = static_cast<unsigned char>(std::stoi(byte_string, nullptr, 16));
			bytes.push_back(static_cast<char>(byte));
		}
		return bytes;
	}

	std::string to_upper(const std::string& str)
	{
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), ::toupper);
		return result;
	}

	std::string to_lower(const std::string& str)
	{
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
		return result;
	}

	bool is_numeric(const std::string& str)
	{
		if (str.empty())
			return false;
		return std::all_of(str.begin(), str.end(), ::isdigit);
	}
}
