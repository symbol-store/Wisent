#include "BsonSerializer.hpp"
#include "CsvLoading.hpp"
#include "WisentSerializer.hpp"
#include <chrono>
#include <cpp-httplib/httplib.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
int main(int argc, char** argv) {
  int httpPort = 3000;
  bool forceReload = false;
  bool disableRLE = false;
  bool disableCsvHandling = false;
  bool loadArgAsJson = false;
  bool loadArgAsBson = false;
  std::vector<std::string> filepaths;
  std::map<std::string, std::pair<int64_t, int64_t>> averageTimings;
  for(int i = 1; i < argc; ++i) {
    if(std::string("--force-reload") == argv[i]) {
      forceReload = true;
      continue;
    }
    if(std::string("--disable-rle") == argv[i]) {
      disableRLE = true;
      continue;
    }
    if(std::string("--disable-csv-handling") == argv[i]) {
      disableCsvHandling = true;
      continue;
    }
    if(std::string("--http-port") == argv[i]) {
      httpPort = atoi(argv[++i]);
      continue;
    }
    if(std::string("--load-as-json") == argv[i]) {
      loadArgAsJson = true;
      continue;
    }
    if(std::string("--load-as-bson") == argv[i]) {
      loadArgAsBson = true;
      continue;
    }
    filepaths.emplace_back(argv[i]);
  }
  std::vector<std::string> names;
  names.reserve(filepaths.size());
  for(auto const& filepath : filepaths) {
    auto filenamePos = filepath.find_last_of("/\\");
    auto filename = filepath.substr(filenamePos + 1);
    auto extPos = filename.find_last_of(".");
    if(filename.substr(extPos) != ".json") {
      if(filename.substr(extPos) == ".csv") {
        auto start = std::chrono::high_resolution_clock::now();
        auto doc = openCsvFile(filepath);
        for(auto const& columnName : doc.GetColumnNames()) {
          json column = loadCsvDataToJson<int64_t>(doc, columnName);
          if(column.is_null()) {
            column = loadCsvDataToJson<double_t>(doc, columnName);
            if(column.is_null()) {
              column = loadCsvDataToJson<std::string>(doc, columnName);
            }
          }
          assert(!column.is_null());
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto timeDiff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        auto& [count, avg] = averageTimings.try_emplace(filepath, 0, 0).first->second;
        auto total = avg * count;
        count++;
        avg = (total + timeDiff) / count;
        std::cout << "took " << timeDiff << " ns (avg:" << avg << ")" << std::endl;
        continue;
      }
      std::cout << "unsupported, not a json file: " << filepath;
      continue;
    }
    auto filenameWithoutExt = filename.substr(0, extPos);
    auto csvPrefix = filepath.substr(0, filenamePos + 1);
    if(loadArgAsBson) {
      bson::serializer::loadAsBson(filepath, filenameWithoutExt, csvPrefix);
    } else if(loadArgAsJson) {
      void* ptr = bson::serializer::loadAsJson(filepath, filenameWithoutExt, csvPrefix);
    } else {
      auto root = wisent::serializer::load(filepath, filenameWithoutExt, csvPrefix, disableRLE,
                                           disableCsvHandling, forceReload);
    }
    names.emplace_back(filenameWithoutExt);
  }

  httplib::Server svr;
  svr.Get("/load", [&](const httplib::Request& req, httplib::Response& res) {
    auto const& name = req.get_param_value("name");
    auto const& filepath = req.get_param_value("path");
    bool loadCSV = true;
    if(req.has_param("loadCSV")) {
      auto const& str = req.get_param_value("loadCSV");
      loadCSV = (str.empty() || str == "True" || str == "true" || atoi(str.c_str()) > 0);
    }
    bool serializeToBson = false;
    if(req.has_param("toBson")) {
      auto const& str = req.get_param_value("toBson");
      serializeToBson = (str.empty() || str == "True" || str == "true" || atoi(str.c_str()) > 0);
    }
    bool serializeToJson = false;
    if(req.has_param("toJson")) {
      auto const& str = req.get_param_value("toJson");
      serializeToJson = (str.empty() || str == "True" || str == "true" || atoi(str.c_str()) > 0);
    }
    std::cout << "loading dataset '" << name << "' from '" << filepath << "'" << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    auto filenamePos = filepath.find_last_of("/\\");
    auto csvPrefix = filepath.substr(0, filenamePos + 1);
    if(serializeToBson) {
      bson::serializer::loadAsBson(filepath, name, csvPrefix, disableCsvHandling || !loadCSV);
    } else if(serializeToJson) {
      void* ptr =
          bson::serializer::loadAsJson(filepath, name, csvPrefix, disableCsvHandling || !loadCSV);
    } else {
      auto root = wisent::serializer::load(filepath, name, csvPrefix, disableRLE,
                                           disableCsvHandling || !loadCSV);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    auto& [count, avg] = averageTimings.try_emplace(name + filepath, 0, 0).first->second;
    auto total = avg * count;
    count++;
    avg = (total + timeDiff) / count;
    std::cout << "took " << timeDiff << " ns (avg:" << avg << ")" << std::endl;
    res.set_content("Done.", "text/plain");
  });
  svr.Get("/unload", [&](const httplib::Request& req, httplib::Response& res) {
    auto const& name = req.get_param_value("name");
    std::cout << "unloading dataset '" << name << "'" << std::endl;
    wisent::serializer::unload(name);
    res.set_content("Done.", "text/plain");
  });
  svr.Get("/erase", [&](const httplib::Request& req, httplib::Response& res) {
    auto const& name = req.get_param_value("name");
    std::cout << "erasing dataset '" << name << "'" << std::endl;
    wisent::serializer::free(name);
    res.set_content("Done.", "text/plain");
  });
  svr.Get("/stop",
          [&](const httplib::Request& /*req*/, httplib::Response& /*res*/) { svr.stop(); });
  std::cout << "Server running on port " << httpPort << "..." << std::endl;
  svr.listen("0.0.0.0", httpPort);
  for(auto const& name : names) {
    // deleting only the datasets loaded with the command line
    // clients manually handle the lifetime of the datasets they request
    std::cout << "Deleting " << name << "..." << std::endl;
    wisent::serializer::free(name);
  }
  return 0;
}