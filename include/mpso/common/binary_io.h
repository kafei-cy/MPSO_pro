#pragma once

#include <mpso/common/types.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace mpso {

inline std::ofstream openOutput(const std::string& path)
{
    std::ofstream out(path, std::ios::binary | std::ios::out);
    if (!out.is_open()) {
        throw std::runtime_error("failed to open output file: " + path);
    }
    return out;
}

inline std::ifstream openInput(const std::string& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in.is_open()) {
        throw std::runtime_error("failed to open input file: " + path);
    }
    return in;
}

template<typename T>
void writeBinary(std::ofstream& out, const T* data, u64 count, const std::string& path)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if (count == 0) {
        return;
    }

    out.write(reinterpret_cast<const char*>(data), count * sizeof(T));
    if (!out) {
        throw std::runtime_error("failed to write file: " + path);
    }
}

template<typename T>
void readBinary(std::ifstream& in, T* data, u64 count, const std::string& path)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if (count == 0) {
        return;
    }

    in.read(reinterpret_cast<char*>(data), count * sizeof(T));
    if (!in) {
        throw std::runtime_error("failed to read file: " + path);
    }
}

template<typename T>
void writeBinary(std::ofstream& out, const std::vector<T>& values, const std::string& path)
{
    writeBinary(out, values.data(), values.size(), path);
}

template<typename T>
void readBinary(std::ifstream& in, std::vector<T>& values, const std::string& path)
{
    readBinary(in, values.data(), values.size(), path);
}

}
