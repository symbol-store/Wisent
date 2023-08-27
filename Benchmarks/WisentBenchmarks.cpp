#include "../Source/CsvLoading.hpp"
#include "../Source/SharedMemorySegment.hpp"
#include "../Source/WisentHelpers.h"
#include "ITTNotifySupport.hpp"
#include <benchmark/benchmark.h>
#include <cpp-httplib/httplib.h>
#include <map>
#include <nlohmann/json.hpp>
#include <rapidjson/document.h>
#include <simdjson.h>
#include <simdjson/error.h>
#include <simdjson/implementation.h>
#include <simdjson/padded_string.h>

using json = nlohmann::json;

static auto const vtune = VTuneAPIInterface{"Wisent"};

static std::map<std::string, std::string> loadedFiles;

static bool VERBOSE = false;

static std::map<std::string, std::map<int64_t, int64_t>> predicateValues = {{"owid-deaths",
                                                                             {
                                                                                 {1, 1998},
                                                                                 {2, 1949},
                                                                                 {4, 1926},
                                                                                 {8, 1911},
                                                                                 {16, 1905},
                                                                                 {32, 1902},
                                                                                 {64, 1901},
                                                                                 {128, 1900},
                                                                                 {256, 1899},
                                                                             }},
                                                                            {"opsd-weather",
                                                                             {
                                                                                 {1, 1483221600},
                                                                                 {2, 899377200},
                                                                                 {4, 607455000},
                                                                                 {8, 461493900},
                                                                                 {16, 388513350},
                                                                                 {32, 352023075},
                                                                                 {64, 333777937},
                                                                                 {128, 324655368},
                                                                                 {256, 320094083},
                                                                             }}};

static std::map<std::string, std::string> predicateColumnMap = {{"owid-deaths", "Year"},
                                                                {"opsd-weather", "utc_timestamp"}};

static std::map<std::string, std::string> aggregateColumnMap = {
    {"owid-deaths", "Accidents (excl. road) - Death Rates"}, {"opsd-weather", "GB_temperature"}};

class SharedMemoryData {
public:
  SharedMemoryData(std::string const& name, std::string const& sizeSuffix, bool asJson = false,
                   bool asBson = false, bool csvLoading = true)
      : sharedMemoryName(name + (asJson ? (csvLoading ? "_json" : "_rawjson") : "") +
                         (asBson ? (csvLoading ? "_bson" : "_rawbson") : "")),
        sharedMemory(nullptr) {
    // request data loading
    auto filepath = "../Data/" + name + "/datapackage" + sizeSuffix + ".json";
    auto& prevFilepath = loadedFiles[sharedMemoryName];
    httplib::Client client("localhost", 3000);
    client.set_connection_timeout(3600);
    client.set_read_timeout(3600);
    client.set_write_timeout(3600);
    httplib::Params params = {{"name", sharedMemoryName},
                              {"path", filepath},
                              {"toJson", asJson ? "true" : "false"},
                              {"toBson", asBson ? "true" : "false"},
                              {"loadCSV", csvLoading ? "true" : "false"}};
    if(filepath != prevFilepath) {
      client.Get("/erase", params, httplib::Headers());
    }
    client.Get("/load", params, httplib::Headers());
    prevFilepath = filepath;
    // open shared memory
    sharedMemory = &createOrGetMemorySegment(sharedMemoryName);
    sharedMemory->load();
    if(!sharedMemory->loaded()) {
      throw std::runtime_error("cannot load '" + sharedMemoryName + "'");
    }
  }

  ~SharedMemoryData() {
    if(sharedMemory != nullptr) {
      sharedMemory->unload();
      sharedMemory = nullptr;
    }
    sharedMemorySegments().erase(sharedMemoryName);
    httplib::Client client("localhost", 3000);
    httplib::Params params = {{"name", sharedMemoryName}};
    client.Get("/unload", params, httplib::Headers());
  }

  template <typename T> T begin() {
    assert(sharedMemory);
    return static_cast<T>(sharedMemory->baseAddress());
  }

  template <typename T> T end() {
    assert(sharedMemory);
    return begin<T>() + sharedMemory->size();
  }

private:
  std::string sharedMemoryName;
  SharedMemorySegment* sharedMemory;
};

class LazyExpression {
public:
  LazyExpression(WisentRootExpression* root, uint64_t index) : root(root), index(index) {}

  LazyExpression operator[](size_t childOffset) const {
    auto const& expr = expression();
    assert(childOffset < expr.startChildOffset - expr.endChildOffset);
    return {root, expr.startChildOffset + childOffset};
  }

  LazyExpression operator[](std::string const& keyName) const {
    auto const& expr = expression();
    auto const& arguments = getExpressionArguments(root);
    auto const& argumentTypes = getArgumentTypes(root);
    auto const& expressions = getExpressionSubexpressions(root);
    for(auto i = expr.startChildOffset; i < expr.endChildOffset; ++i) {
      if(argumentTypes[i] != WisentArgumentType::ARGUMENT_TYPE_EXPRESSION) {
        continue;
      }
      auto const& child = expressions[arguments[i].asExpression];
      auto const& key = viewString(root, child.symbolNameOffset);
      if(std::string_view{key} == keyName) {
        return {root, i};
      }
    }
    throw std::runtime_error(keyName + " not found.");
  }

  template <typename T> class Iterator : public std::iterator<std::forward_iterator_tag, T> {
  public:
    Iterator(WisentRootExpression* root, uint64_t index)
        : root(root), arguments(getExpressionArguments(root)),
          argumentTypes(getArgumentTypes(root)), index(index), validIndexEnd(index) {
      updateValidIndexEnd();
    }
    virtual ~Iterator() = default;

    Iterator operator++(int) { return Iterator(root, incrementIndex(1)); }
    Iterator& operator++() {
      incrementIndex(1);
      return *this;
    }

    bool isValid() { return index < validIndexEnd; }

    T& operator*() const {
      if constexpr(std::is_same_v<T, int64_t>) {
        return arguments[index].asLong;
      } else if constexpr(std::is_same_v<T, double_t>) {
        return arguments[index].asDouble;
      } else {
        throw std::runtime_error("non-numerical types not yet implemented");
      }
    }
    T* operator->() const { return &operator*(); }

    Iterator operator+(std::ptrdiff_t v) const { return incrementIndex(v); }
    bool operator==(const Iterator& rhs) const { return index == rhs.index; }
    bool operator!=(const Iterator& rhs) const { return index != rhs.index; }

  private:
    WisentRootExpression* root;
    WisentArgumentValue* arguments;
    WisentArgumentType* argumentTypes;
    uint64_t index;
    uint64_t validIndexEnd;

    uint64_t incrementIndex(std::ptrdiff_t increment) {
      index += increment;
      updateValidIndexEnd();
      return index;
    }

    void updateValidIndexEnd() {
      if(index >= validIndexEnd) {
        if(argumentTypes[index] & WisentArgumentType_RLE_BIT) {
          if((argumentTypes[index] & ~WisentArgumentType_RLE_BIT) == expectedArgumentType()) {
            validIndexEnd = index + static_cast<uint32_t>(argumentTypes[index + 1]);
          }
        } else {
          if(argumentTypes[index] == expectedArgumentType()) {
            validIndexEnd = index + 1;
          }
        }
      }
    }

    constexpr WisentArgumentType expectedArgumentType() {
      if constexpr(std::is_same_v<T, int64_t>) {
        return WisentArgumentType::ARGUMENT_TYPE_LONG;
      } else if constexpr(std::is_same_v<T, double_t>) {
        return WisentArgumentType::ARGUMENT_TYPE_DOUBLE;
      } else if constexpr(std::is_same_v<T, std::string>) {
        return WisentArgumentType::ARGUMENT_TYPE_STRING;
      }
    }
  };

  template <typename T> Iterator<T> begin() {
    return Iterator<T>(root, expression().startChildOffset);
  }
  template <typename T> Iterator<T> end() { return Iterator<T>(root, expression().endChildOffset); }

private:
  WisentRootExpression* root;
  uint64_t index;

  WisentExpression const& expression() const {
    auto const& arguments = getExpressionArguments(root);
    auto const& argumentTypes = getArgumentTypes(root);
    auto const& expressions = getExpressionSubexpressions(root);
    assert(argumentTypes[index] == WisentArgumentType::ARGUMENT_TYPE_EXPRESSION);
    return expressions[arguments[index].asExpression];
  }
};

void runWisent(benchmark::State& state, std::string const& dataset, std::string sizeSuffix,
               int64_t selectivityFraction) {
  auto const& predValue = predicateValues[dataset][selectivityFraction];
  auto const& predColumnStr = predicateColumnMap[dataset];
  auto const& aggColumnStr = aggregateColumnMap[dataset];
  SharedMemoryData data(dataset, sizeSuffix);
  auto* root = data.begin<WisentRootExpression*>();
  vtune.startSampling("Wisent");
  auto agg = 0.0;
  for(auto _ : state) {
    auto table = LazyExpression(root, 0)["resources"][0]["Object"]["path"]["Table"];
    auto aggColumn = table[aggColumnStr];
    auto predColumn = table[predColumnStr];
    auto predIt = predColumn.begin<int64_t>();
    agg = 0.0;
    for(auto aggIt = aggColumn.begin<double_t>(); aggIt != aggColumn.end<double_t>();
        ++aggIt, ++predIt) {
      if(!aggIt.isValid() || !predIt.isValid()) {
        continue;
      }
      if(*predIt > predValue) {
        continue;
      }
      agg += *aggIt;
    }
    assert(agg > 0.0);
    benchmark::DoNotOptimize(agg);
  }
  vtune.stopSampling();
  if(VERBOSE) {
    std::cout << "output: agg=" << agg << std::endl;
  }
}

void runJsonCsv(benchmark::State& state, std::string const& dataset, std::string sizeSuffix,
                int64_t selectivityFraction) {
  auto predValue = predicateValues[dataset][selectivityFraction];
  auto const& predColumnStr = predicateColumnMap[dataset];
  auto const& aggColumnStr = aggregateColumnMap[dataset];
  SharedMemoryData data(dataset, sizeSuffix, true, false, false);
  auto* dataPtr = data.begin<char const*>();
  vtune.startSampling("Json");
  auto agg = 0.0;
  for(auto _ : state) {
    json j = json::parse(dataPtr);
    auto const& csvFile = j["resources"][0]["path"].get<std::string>();
    auto csvfilePath = "../Data/" + dataset + "/" + csvFile;
    auto doc = openCsvFile(csvfilePath);
    auto const& aggColumn =
        doc.GetColumn<double_t>(aggColumnStr, [](std::string const& str, auto& val) {
          if(str.empty()) {
            val = 0.0;
          } else {
            val = atof(str.c_str());
          }
        });
    auto const& predColumn =
        doc.GetColumn<int64_t>(predColumnStr, [](std::string const& str, auto& val) {
          if(str.empty()) {
            val = 0;
          } else {
            val = atoi(str.c_str());
          }
        });
    agg = std::inner_product(
        aggColumn.begin(), aggColumn.end(), predColumn.begin(), 0.0, std::plus<>(),
        [&predValue](auto aggElem, auto predElem) { return predElem > predValue ? 0 : aggElem; });
    assert(agg > 0.0);
    benchmark::DoNotOptimize(agg);
  }
  vtune.stopSampling();
  if(VERBOSE) {
    std::cout << "output: agg=" << agg << std::endl;
  }
}

void runJson(benchmark::State& state, std::string const& dataset, std::string sizeSuffix,
             int64_t selectivityFraction) {
  auto predValue = predicateValues[dataset][selectivityFraction];
  auto const& predColumnStr = predicateColumnMap[dataset];
  auto const& aggColumnStr = aggregateColumnMap[dataset];
  SharedMemoryData data(dataset, sizeSuffix, true, false);
  auto* dataPtr = data.begin<char const*>();
  vtune.startSampling("Json");
  auto agg = 0.0;
  for(auto _ : state) {
    json j = json::parse(dataPtr);
    auto const& table = j["resources"][0]["path"]["Table"];
    auto const& aggColumn = table[aggColumnStr];
    auto const& predColumn = table[predColumnStr];
    agg =
        std::inner_product(aggColumn.begin(), aggColumn.end(), predColumn.begin(), 0.0,
                           std::plus<>(), [&predValue](auto const& aggElem, auto const& predElem) {
                             if(aggElem.is_null() || predElem.is_null()) {
                               return 0.0;
                             }
                             if(predElem.template get<int64_t>() > predValue) {
                               return 0.0;
                             }
                             return aggElem.template get<double_t>();
                           });
    assert(agg > 0.0);
    benchmark::DoNotOptimize(agg);
  }
  vtune.stopSampling();
  if(VERBOSE) {
    std::cout << "output: agg=" << agg << std::endl;
  }
}

void runRapidJson(benchmark::State& state, std::string const& dataset, std::string sizeSuffix,
                  int64_t selectivityFraction) {
  auto predValue = predicateValues[dataset][selectivityFraction];
  auto const& predColumnStr = predicateColumnMap[dataset];
  auto const& aggColumnStr = aggregateColumnMap[dataset];
  SharedMemoryData data(dataset, sizeSuffix, true, false);
  auto* dataPtr = data.begin<char const*>();
  vtune.startSampling("Json");
  auto agg = 0.0;
  for(auto _ : state) {
    rapidjson::Document j;
    j.Parse(dataPtr);
    auto const& table = j["resources"][0]["path"]["Table"];
    auto const& aggColumn = table[aggColumnStr.c_str()];
    auto const& predColumn = table[predColumnStr.c_str()];
    agg =
        std::inner_product(aggColumn.Begin(), aggColumn.End(), predColumn.Begin(), 0.0,
                           std::plus<>(), [&predValue](auto const& aggElem, auto const& predElem) {
                             if(aggElem.IsNull() || predElem.IsNull()) {
                               return 0.0;
                             }
                             if(predElem.GetInt() > predValue) {
                               return 0.0;
                             }
                             return aggElem.GetDouble();
                           });
    assert(agg > 0.0);
    benchmark::DoNotOptimize(agg);
  }
  vtune.stopSampling();
  if(VERBOSE) {
    std::cout << "output: agg=" << agg << std::endl;
  }
}

void runSimdJson(benchmark::State& state, std::string const& dataset, std::string sizeSuffix,
                 int64_t selectivityFraction) {
  auto predValue = predicateValues[dataset][selectivityFraction];
  auto const& predColumnStr = predicateColumnMap[dataset];
  auto const& aggColumnStr = aggregateColumnMap[dataset];
  SharedMemoryData data(dataset, sizeSuffix, true, false);
  auto* dataPtr = data.begin<char const*>();
  size_t dataLength = strlen(dataPtr);
  assert(simdjson::validate_utf8(dataPtr, dataLength));
  simdjson::padded_string paddedData(dataPtr, dataLength);
  simdjson::ondemand::parser parser;
  vtune.startSampling("Json");
  auto agg = 0.0;
  for(auto _ : state) {
    auto j = parser.iterate(paddedData);
    try {
      auto table = j["resources"].get_array().at(0)["path"]["Table"];
      auto predColumn = table[predColumnStr];
      std::vector<bool> bitArray;
      std::transform(predColumn.begin(), predColumn.end(), std::back_inserter(bitArray),
                     [&predValue](auto predElem) {
                       return predElem.type() != simdjson::ondemand::json_type::null &&
                              (int64_t)predElem <= predValue;
                     });
      auto aggColumn = table[aggColumnStr];
      agg = std::inner_product(aggColumn.begin(), aggColumn.end(), bitArray.begin(), 0.0,
                               std::plus<>(), [&bitArray](auto aggElem, auto bitValue) {
                                 if(!bitValue) {
                                   return 0.0;
                                 }
                                 if(aggElem.type() == simdjson::ondemand::json_type::null) {
                                   return 0.0;
                                 }
                                 return (double_t)aggElem;
                               });
    } catch(simdjson::simdjson_error& err) {
      std::cerr << "SimdJson parsing error '" << err.what() << "' at " << j.current_location()
                << std::endl;
    }
    assert(agg > 0.0);
    benchmark::DoNotOptimize(agg);
  }
  vtune.stopSampling();
  if(VERBOSE) {
    std::cout << "output: agg=" << agg << std::endl;
  }
}

void runBson(benchmark::State& state, std::string const& dataset, std::string sizeSuffix,
             int64_t selectivityFraction) {
  auto predValue = predicateValues[dataset][selectivityFraction];
  auto const& predColumnStr = predicateColumnMap[dataset];
  auto const& aggColumnStr = aggregateColumnMap[dataset];
  SharedMemoryData data(dataset, sizeSuffix, false, true);
  auto* dataBeginPtr = data.begin<std::uint8_t const*>();
  auto* dataEndPtr = data.end<std::uint8_t const*>();
  vtune.startSampling("Bson");
  auto agg = 0.0;
  for(auto _ : state) {
    json j = json::from_bson(dataBeginPtr, dataEndPtr);
    auto const& table = j["resources"][0]["path"]["Table"];
    auto const& aggColumn = table[aggColumnStr];
    auto const& predColumn = table[predColumnStr];
    agg =
        std::inner_product(aggColumn.begin(), aggColumn.end(), predColumn.begin(), 0.0,
                           std::plus<>(), [&predValue](auto const& aggElem, auto const& predElem) {
                             if(aggElem.is_null() || predElem.is_null()) {
                               return 0.0;
                             }
                             if(predElem.template get<int64_t>() > predValue) {
                               return 0.0;
                             }
                             return aggElem.template get<double_t>();
                           });
    assert(agg > 0.0);
    benchmark::DoNotOptimize(agg);
  }
  vtune.stopSampling();
  if(VERBOSE) {
    std::cout << "output: agg=" << agg << std::endl;
  }
}

template <typename... Args>
benchmark::internal::Benchmark* RegisterBenchmarkNolint([[maybe_unused]] Args... args) {
#ifdef __clang_analyzer__
  // There is not way to disable clang-analyzer-cplusplus.NewDeleteLeaks
  // even though it is perfectly safe. Let's just please clang analyzer.
  return nullptr;
#else
  return benchmark::RegisterBenchmark(args...);
#endif
}

void initAndRunBenchmarks(int argc, char** argv) {
  for(auto i = 1; i < argc; ++i) {
    if(std::string(argv[i]) == "--verbose") {
      VERBOSE = true;
    }
  }
  // register smaller size variations
  for(std::string const& dataset : std::vector<std::string>{"owid-deaths", "opsd-weather"}) {
    for(std::string const& sizeSuffix : std::vector<std::string>{
            "_div256", "_div128", "_div64", "_div32", "_div16", "_div8", "_div4", "_div2"}) {
      for(int selectivityFraction : std::vector<int>{1}) {
        std::ostringstream name;
        name << dataset << ",size:" << sizeSuffix << ",selectivity:1/" << selectivityFraction;
        RegisterBenchmarkNolint(("Wisent," + name.str()).c_str(), runWisent, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("JsonCsv," + name.str()).c_str(), runJsonCsv, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("Json," + name.str()).c_str(), runJson, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("Bson," + name.str()).c_str(), runBson, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("RapidJson," + name.str()).c_str(), runRapidJson, dataset,
                                sizeSuffix, selectivityFraction);
        RegisterBenchmarkNolint(("SimdJson," + name.str()).c_str(), runSimdJson, dataset,
                                sizeSuffix, selectivityFraction);
      }
    }
  }
  // register larger sizes / selectivity variations
  for(std::string const& dataset : std::vector<std::string>{"owid-deaths", "opsd-weather"}) {
    for(std::string const& sizeSuffix : std::vector<std::string>{
            "_scale1", "_scale2", "_scale4", "_scale8", "_scale16", "_scale32", "_scale64"}) {
      for(int selectivityFraction : std::vector<int>{256, 128, 64, 32, 16, 8, 4, 2, 1}) {
        std::ostringstream name;
        name << dataset << ",size:" << sizeSuffix << ",selectivity:1/" << selectivityFraction;
        RegisterBenchmarkNolint(("Wisent," + name.str()).c_str(), runWisent, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("JsonCsv," + name.str()).c_str(), runJsonCsv, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("Json," + name.str()).c_str(), runJson, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("Bson," + name.str()).c_str(), runBson, dataset, sizeSuffix,
                                selectivityFraction);
        RegisterBenchmarkNolint(("RapidJson," + name.str()).c_str(), runRapidJson, dataset,
                                sizeSuffix, selectivityFraction);
        RegisterBenchmarkNolint(("SimdJson," + name.str()).c_str(), runSimdJson, dataset,
                                sizeSuffix, selectivityFraction);
      }
    }
  }
  // initialise and run google benchmark
  ::benchmark::Initialize(&argc, argv);
  ::benchmark::RunSpecifiedBenchmarks();
}

int main(int argc, char** argv) {
  try {
    initAndRunBenchmarks(argc, argv);
  } catch(std::exception& e) {
    std::cerr << "caught exception in main: " << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch(...) {
    std::cerr << "unhandled exception." << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
