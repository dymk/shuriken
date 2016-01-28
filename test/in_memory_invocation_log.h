#include "invocation_log.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace shk {

/**
 * An InvocationLog implementation that is memory backed rather than disk based
 * like the real InvocationLog. Used for testing and for dry runs.
 */
class InMemoryInvocationLog : public InvocationLog {
 public:
  void createdDirectory(const std::string &path) throw(IoError) override;
  void removedDirectory(const std::string &path) throw(IoError) override;
  void ranCommand(
      const Hash &build_step_hash,
      const Entry &entry) throw(IoError) override;
  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override;

  const std::unordered_set<std::string> &createdDirectories() const {
    return _created_directories;
  }

  const std::unordered_map<Hash, Entry> &entries() const {
    return _entries;
  }

 private:
  std::unordered_map<Hash, Entry> _entries;
  std::unordered_set<std::string> _created_directories;
};

}  // namespace shk
