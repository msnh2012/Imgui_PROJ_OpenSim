#pragma once

#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osc
{
    // a basic CSV parser that has an Rust-iterator-like `next` API for pulling out
    // each row, which is returned as a list of strings (python-like)
    //
    // not designed to be fast: only (mostly) correct and easy to use
    class CSVReader final {
    public:
        explicit CSVReader(std::shared_ptr<std::istream>);
        CSVReader(CSVReader const&) = delete;
        CSVReader(CSVReader&&) noexcept;
        CSVReader& operator=(CSVReader const&) = delete;
        CSVReader& operator=(CSVReader&&) noexcept;
        ~CSVReader() noexcept;

        std::optional<std::vector<std::string>> next();

    private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // a basic CSV writer
    class CSVWriter final {
    public:
        explicit CSVWriter(std::shared_ptr<std::ostream>);
        CSVWriter(CSVWriter const&) = delete;
        CSVWriter(CSVWriter&&) noexcept;
        CSVWriter& operator=(CSVWriter const&) = delete;
        CSVWriter& operator=(CSVWriter&&) noexcept;
        ~CSVWriter() noexcept;

        void writeRow(std::vector<std::string> const&);

    private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}