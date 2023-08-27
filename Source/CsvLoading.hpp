#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <rapidcsv.h>
#include <string>
#include <vector>

using json = nlohmann::json;

static auto openCsvFile(std::string const& filepath) {
  return rapidcsv::Document(filepath, rapidcsv::LabelParams(), rapidcsv::SeparatorParams(),
                            rapidcsv::ConverterParams(), rapidcsv::LineReaderParams());
}

template <typename T>
static std::vector<std::optional<T>> loadCsvData(rapidcsv::Document const& doc,
                                                 std::string const& columnName) {
  // load the csv data
  std::vector<std::optional<T>> column;
  try {
    auto numRows = doc.GetRowCount();
    column.reserve(numRows);
    auto columnIndex = doc.GetColumnIdx(columnName);
    for(auto rowIndex = 0L; rowIndex < numRows; ++rowIndex) {
      column.emplace_back(doc.GetCell<std::optional<T>>(
          columnIndex, rowIndex, [](std::string const& str, auto& val) {
            if constexpr(!std::is_same_v<T, std::string>) {
              if(str.empty()) {
                val = {};
                return;
              }
            }
            if constexpr(std::is_same_v<T, int64_t>) {
              size_t pos;
              val = std::stol(str, &pos);
              if(pos != str.length()) {
                throw std::invalid_argument("failed to convert the whole string");
              }
            } else if constexpr(std::is_same_v<T, double_t>) {
              size_t pos;
              val = std::stod(str, &pos);
              if(pos != str.length()) {
                throw std::invalid_argument("failed to convert the whole string");
              }
            } else {
              val = str;
            }
          }));
    }
  } catch(std::invalid_argument const& /*e*/) {
    return {};
  }
  return std::move(column);
}

template <typename T>
static json loadCsvDataToJson(rapidcsv::Document const& doc, std::string const& columnName) {
  // load the csv data
  json column(json::value_t::array);
  try {
    auto numRows = doc.GetRowCount();
    auto columnIndex = doc.GetColumnIdx(columnName);
    for(auto rowIndex = 0L; rowIndex < numRows; ++rowIndex) {
      column.push_back(
          doc.GetCell<json>(columnIndex, rowIndex, [](std::string const& str, auto& val) {
            if constexpr(!std::is_same_v<T, std::string>) {
              if(str.empty()) {
                val = json{};
                return;
              }
            }
            if constexpr(std::is_same_v<T, int64_t>) {
              size_t pos;
              val = std::stol(str, &pos);
              if(pos != str.length()) {
                throw std::invalid_argument("failed to convert the whole string");
              }
            } else if constexpr(std::is_same_v<T, double_t>) {
              size_t pos;
              val = std::stod(str, &pos);
              if(pos != str.length()) {
                throw std::invalid_argument("failed to convert the whole string");
              }
            } else {
              val = str;
            }
          }));
    }
  } catch(std::invalid_argument const& /*e*/) {
    return json{};
  }
  return std::move(column);
}
