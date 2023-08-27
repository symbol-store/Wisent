#include "WisentSerializer.hpp"
#include "CsvLoading.hpp"
#include "SharedMemorySegment.hpp"
#include "WisentHelpers.h"
#include <cassert>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

using json = nlohmann::json;

class JsonToWisent : public json::json_sax_t {
private:
  WisentRootExpression* root;
  std::vector<uint64_t> cumulArgCountPerLayer;
  std::vector<bool> wasKeyValue;
  std::vector<uint64_t> argumentIteratorStack{0};
  std::vector<uint64_t> expressionIndexStack{0};
  uint64_t nextExpressionIndex{0};
  uint64_t layerIndex{0};
  SharedMemorySegment& sharedMemory;
  std::string const& csvPrefix;
  bool disableRLE;
  bool disableCsvHandling;
  uint64_t numRepeatedArgumentTypes; // count repeated type for triggering RLE encoding

public:
  JsonToWisent(uint64_t expressionCount, std::vector<uint64_t>&& argumentCountPerLayer,
               SharedMemorySegment& sharedMemory, std::string const& csvPrefix, bool disableRLE,
               bool disableCsvHandling)
      : root(nullptr), cumulArgCountPerLayer(std::move(argumentCountPerLayer)),
        sharedMemory(sharedMemory), csvPrefix(csvPrefix), disableRLE(disableRLE),
        disableCsvHandling(disableCsvHandling), numRepeatedArgumentTypes(0) {
    // we need the accumulated count at each layer
    std::partial_sum(cumulArgCountPerLayer.begin(), cumulArgCountPerLayer.end(),
                     cumulArgCountPerLayer.begin());
    root =
        allocateExpressionTree(cumulArgCountPerLayer.back(), expressionCount, sharedMemoryMalloc);
    wasKeyValue.resize(cumulArgCountPerLayer.size(), false);
  }

  WisentRootExpression* getRoot() { return root; }

  bool null() override {
    addSymbol("Null");
    handleKeyValueEnd();
    return true;
  }

  bool boolean(bool val) override {
    addSymbol(val ? "True" : "False");
    handleKeyValueEnd();
    return true;
  }

  bool number_integer(number_integer_t val) override {
    addLong(val);
    handleKeyValueEnd();
    return true;
  }

  bool number_unsigned(number_unsigned_t val) override {
    addLong(val);
    handleKeyValueEnd();
    return true;
  }

  bool number_float(number_float_t val, const string_t& /*s*/) override {
    addDouble(val);
    handleKeyValueEnd();
    return true;
  }

  bool string(string_t& val) override {
    if(!handleCsvFile(val)) {
      addString(val);
    }
    handleKeyValueEnd();
    return true;
  }

  bool start_object(std::size_t /*elements*/) override {
    startExpression("Object");
    return true;
  }

  bool end_object() override {
    endExpression();
    handleKeyValueEnd();
    return true;
  }

  bool start_array(std::size_t /*elements*/) override {
    startExpression("List");
    return true;
  }

  bool end_array() override {
    endExpression();
    handleKeyValueEnd();
    return true;
  }

  bool key(string_t& val) override {
    startExpression(val);
    wasKeyValue[layerIndex] = true;
    return true;
  }

  void handleKeyValueEnd() {
    if(wasKeyValue[layerIndex]) {
      wasKeyValue[layerIndex] = false;
      endExpression();
    }
  }

  bool binary(json::binary_t& val) override {
    throw std::runtime_error("binary value not implemented");
  }

  bool parse_error(std::size_t position, const std::string& last_token,
                   const json::exception& ex) override {
    throw std::runtime_error("parse_error(position=" + std::to_string(position) + ", last_token=" +
                             last_token + ",\n            ex=" + std::string(ex.what()) + ")");
  }

private:
  uint64_t getNextArgumentIndex() {
    return getExpressionSubexpressions(root)[expressionIndexStack.back()].startChildOffset +
           argumentIteratorStack.back()++;
  }

  void applyTypeRLE(std::uint64_t argIndex) {
    if(disableRLE) {
      return;
    }
    if(numRepeatedArgumentTypes == 0) {
      numRepeatedArgumentTypes = 1;
      return;
    }
    if(getArgumentTypes(root)[argIndex - 1] != getArgumentTypes(root)[argIndex]) {
      resetTypeRLE(argIndex);
      numRepeatedArgumentTypes = 1;
      return;
    }
    ++numRepeatedArgumentTypes;
  }

  void resetTypeRLE(std::uint64_t endIndex) {
    if(numRepeatedArgumentTypes >= WisentArgumentType_RLE_MINIMUM_SIZE) {
      setRLEArgumentFlagOrPropagateTypes(root, endIndex - numRepeatedArgumentTypes,
                                         numRepeatedArgumentTypes);
    }
    numRepeatedArgumentTypes = 0;
  }

  void addLong(std::int64_t input) {
    uint64_t argIndex = getNextArgumentIndex();
    *makeLongArgument(root, argIndex) = input;
    applyTypeRLE(argIndex);
  }

  void addDouble(double_t input) {
    uint64_t argIndex = getNextArgumentIndex();
    *makeDoubleArgument(root, argIndex) = input;
    applyTypeRLE(argIndex);
  }

  void addString(std::string const& input) {
    auto storedString = storeString(&root, input.c_str(), sharedMemoryRealloc);
    uint64_t argIndex = getNextArgumentIndex();
    *makeStringArgument(root, argIndex) = storedString;
    applyTypeRLE(argIndex);
  }

  void addSymbol(std::string const& symbol) {
    auto storedString = storeString(&root, symbol.c_str(), sharedMemoryRealloc);
    uint64_t argIndex = getNextArgumentIndex();
    *makeSymbolArgument(root, argIndex) = storedString;
    applyTypeRLE(argIndex);
  }

  void addExpression(size_t expressionIndex) {
    uint64_t argIndex = getNextArgumentIndex();
    *makeExpressionArgument(root, argIndex) = expressionIndex;
    resetTypeRLE(argIndex);
  }

  void startExpression(std::string const& head) {
    auto expressionIndex = nextExpressionIndex++;
    addExpression(expressionIndex);
    auto storedString = storeString(&root, head.c_str(), sharedMemoryRealloc);
    auto startChildOffset = cumulArgCountPerLayer[layerIndex++];
    *makeExpression(root, expressionIndex) = WisentExpression{
        storedString, startChildOffset,
        0 // not known yet; set during endExpression()
    };
    argumentIteratorStack.push_back(0);
    expressionIndexStack.push_back(expressionIndex);
  }

  void endExpression() {
    auto& expression = getExpressionSubexpressions(root)[expressionIndexStack.back()];
    expression.endChildOffset = expression.startChildOffset + argumentIteratorStack.back();
    resetTypeRLE(expression.endChildOffset);
    argumentIteratorStack.pop_back();
    expressionIndexStack.pop_back();
    cumulArgCountPerLayer[--layerIndex] = expression.endChildOffset;
  }

  bool handleCsvFile(std::string const& filename) {
    if(disableCsvHandling) {
      return false;
    }
    auto extPos = filename.find_last_of(".");
    if(extPos == std::string::npos || filename.substr(extPos) != ".csv") {
      return false;
    }
    startExpression("Table");
    auto doc = openCsvFile(csvPrefix + filename);
    for(auto const& columnName : doc.GetColumnNames()) {
      if(!handleCsvColumn<int64_t>(doc, columnName, [this](auto val) { addLong(val); })) {
        if(!handleCsvColumn<double_t>(doc, columnName, [this](auto val) { addDouble(val); })) {
          if(!handleCsvColumn<std::string>(doc, columnName,
                                           [this](auto const& val) { addString(val); })) {
            throw std::runtime_error("failed to handle csv column: '" + columnName + "'");
          }
        }
      }
    }
    endExpression();
    return true;
  }

  template <typename T, typename Func>
  bool handleCsvColumn(rapidcsv::Document const& doc, std::string const& columnName,
                       Func&& addValueFunc) {
    auto column = loadCsvData<T>(doc, columnName);
    if(column.empty()) {
      return false;
    }
    // store as a column expression
    startExpression(columnName);
    for(auto const& val : column) {
      val ? addValueFunc(*val) : addSymbol("Missing");
    }
    endExpression();
    return true;
  }
};

WisentRootExpression* wisent::serializer::load(std::string const& path,
                                               std::string const& sharedMemoryName,
                                               std::string const& csvPrefix, bool disableRLE,
                                               bool disableCsvHandling, bool forceReload) {
  auto& sharedMemory = createOrGetMemorySegment(sharedMemoryName);
  if(!forceReload && sharedMemory.exists() && !sharedMemory.loaded()) {
    sharedMemory.load();
  }
  if(sharedMemory.loaded()) {
    if(!forceReload) {
      return reinterpret_cast<WisentRootExpression*>(sharedMemory.baseAddress());
    }
    free(sharedMemoryName);
  }
  setCurrentSharedMemory(sharedMemory);

  std::ifstream ifs(path);
  if(!ifs.good()) {
    throw std::runtime_error("failed to read: " + path);
  }
  // 1st traversal just to calculate the total size needed
  uint64_t expressionCount = 0;
  std::vector<uint64_t> argumentCountPerLayer;
  argumentCountPerLayer.reserve(16);
  json::parse(ifs, [&csvPrefix, &disableCsvHandling, &expressionCount, &argumentCountPerLayer,
                    layerIndex = uint64_t{0}, wasKeyValue = std::vector<bool>(16)](
                       int depth, json::parse_event_t event, json& parsed) mutable {
    if(wasKeyValue.size() <= depth) {
      wasKeyValue.resize(wasKeyValue.size() * 2, false);
    }
    if(argumentCountPerLayer.size() <= layerIndex) {
      argumentCountPerLayer.resize(layerIndex + 1, 0);
    }
    if(event == json::parse_event_t::key) {
      argumentCountPerLayer[layerIndex]++;
      expressionCount++;
      wasKeyValue[depth] = true;
      layerIndex++;
      return true;
    }
    if(event == json::parse_event_t::object_start || event == json::parse_event_t::array_start) {
      argumentCountPerLayer[layerIndex]++;
      expressionCount++;
      layerIndex++;
      return true;
    }
    if(event == json::parse_event_t::object_end || event == json::parse_event_t::array_end) {
      layerIndex--;
      if(wasKeyValue[depth]) {
        wasKeyValue[depth] = false;
        layerIndex--;
      }
      return true;
    }
    if(event == json::parse_event_t::value) {
      argumentCountPerLayer[layerIndex]++;
      if(!disableCsvHandling && parsed.is_string()) {
        auto filename = parsed.get<std::string>();
        auto extPos = filename.find_last_of(".");
        if(extPos != std::string::npos && filename.substr(extPos) == ".csv") {
          auto doc = openCsvFile(csvPrefix + filename);
          auto rows = doc.GetRowCount();
          auto cols = doc.GetColumnCount();
          static const size_t numTableLayers = 2; // Column/Data
          if(argumentCountPerLayer.size() <= layerIndex + numTableLayers) {
            argumentCountPerLayer.resize(layerIndex + numTableLayers + 1, 0);
          }
          expressionCount++;                             // Table expression
          argumentCountPerLayer[layerIndex + 1] += cols; // Column expressions
          expressionCount += cols;
          argumentCountPerLayer[layerIndex + 2] += cols * rows; // Column data
        }
      }
      if(wasKeyValue[depth]) {
        wasKeyValue[depth] = false;
        layerIndex--;
      }
      return true;
    }
  });
  JsonToWisent jsonToWisent(expressionCount, std::move(argumentCountPerLayer), sharedMemory,
                            csvPrefix, disableRLE, disableCsvHandling);
  ifs.seekg(0);
  json::sax_parse(ifs, &jsonToWisent);
  ifs.close();
  return jsonToWisent.getRoot();
}

void wisent::serializer::unload(std::string const& sharedMemoryName) {
  auto& sharedMemory = createOrGetMemorySegment(sharedMemoryName);
  assert(sharedMemory.loaded());
  sharedMemory.unload();
}

void wisent::serializer::free(std::string const& sharedMemoryName) {
  auto& sharedMemory = createOrGetMemorySegment(sharedMemoryName);
  sharedMemory.erase();
  sharedMemorySegments().erase(sharedMemoryName);
}

extern "C" {
char* wisentLoad(char const* path, char const* sharedMemoryName, char const* csvPrefix) {
  return reinterpret_cast<char*>(wisent::serializer::load(path, sharedMemoryName, csvPrefix));
}
void wisentUnload(char const* sharedMemoryName) { wisent::serializer::unload(sharedMemoryName); };
void wisentFree(char const* sharedMemoryName) { wisent::serializer::free(sharedMemoryName); };
}
