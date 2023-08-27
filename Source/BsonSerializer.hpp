#include <string>
namespace bson {
namespace serializer {
void* loadAsBson(std::string const& path, std::string const& sharedMemoryName,
                 std::string const& csvPrefix, bool disableCsvHandling = false,
                 bool forceReload = false);
void* loadAsJson(std::string const& path, std::string const& sharedMemoryName,
                 std::string const& csvPrefix, bool disableCsvHandling = false,
                 bool forceReload = false);
void unload(std::string const& sharedMemoryName);
void free(std::string const& sharedMemoryName);
} // namespace serializer
} // namespace bson
