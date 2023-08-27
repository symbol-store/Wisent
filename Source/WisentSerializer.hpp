#include "WisentHelpers.h"
#include <string>
namespace wisent {
namespace serializer {
WisentRootExpression* load(std::string const& path, std::string const& sharedMemoryName,
                           std::string const& csvPrefix, bool disableRLE = false,
                           bool disableCsvHandling = false, bool forceReload = false);
void unload(std::string const& sharedMemoryName);
void free(std::string const& sharedMemoryName);
} // namespace serializer
} // namespace wisent
