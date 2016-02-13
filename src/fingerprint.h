#pragma once

#include <functional>
#include <string>

#include <sys/types.h>

#include "file_system.h"
#include "hash.h"

namespace shk {

/**
 * A Fingerprint is information about a file that Shuriken stores in the
 * invocation log. It contains information that can be used to detect if the
 * file has been modified (or started or ceased existing) since the Fingerprint
 * was last taken. This is the basis of what Shuriken uses to find out if a
 * build step has become dirty and needs to be re-invoked.
 *
 * Unlike Ninja, which only uses file timestamps, Shuriken uses (a hash of) the
 * contents of the file to do dirtiness checking. The reason Shuriken does not
 * rely only on timestamps is the same as most of the other changes compared to
 * Ninja: It is possible for builds to do the wrong thing when using only
 * timestamps. This can happen if a file is modified within the same second as
 * the build of it finished. Then Ninja will not see that the file has changed.
 *
 * The algorithm that Shuriken uses is inspired by the one used by git:
 * https://www.kernel.org/pub/software/scm/git/docs/technical/racy-git.txt
 *
 * When performing a no-op build, this algorithm allows Shuriken to usually not
 * have to do more than stat-ing inputs and outputs before it can decide that
 * nothing has to be done.
 *
 * Fingerprint objects are stored as-is to disk in the invocation log, so they
 * must be POD objects with no pointers. Changing the contents of Fingerprint
 * results in a breaking change to the invocation log format.
 */
struct Fingerprint {
  /**
   * Fingerprint::Stat is a subset of the full Stat information. It contains
   * only things that Fingerprints are concerned with. For example, it does not
   * contain st_dev, because it's not stable over time on network file systems.
   */
  struct Stat {
    size_t size = 0;
    ino_t ino = 0;
    /**
     * Contains only a subset of the st_mode data, but it contains enough to be
     * able to probe with S_ISDIR.
     */
    mode_t mode = 0;
    time_t mtime = 0;
    time_t ctime = 0;

    /**
     * Returns true if the file was successfully stat-ed. False for example if
     * the file does not exist.
     */
    bool couldAccess() const;

    bool isDir() const;

    bool operator==(const Stat &other) const;
    bool operator!=(const Stat &other) const;
    bool operator<(const Stat &other) const;
  };

  Stat stat;
  /**
   * Timestamp of when the Fingerprint was taken.
   */
  time_t timestamp = 0;
  Hash hash;

  bool operator==(const Fingerprint &other) const;
  bool operator!=(const Fingerprint &other) const;
  bool operator<(const Fingerprint &other) const;
};

struct MatchesResult {
  bool clean = false;
  /**
   * Set to true if fingerprintMatch had to do an (expensive) file content
   * hashing operation in order to know if an update is required. In these
   * situations it is beneficial to recompute the fingerprint for the file.
   * There is then a good chance that hashing will no longer be needed later.
   */
  bool should_update = false;
};

/**
 * Take the fingerprint of a file.
 */
Fingerprint takeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    const std::string &path) throw(IoError);

/**
 * Like takeFingerprint, but uses old_fingerprint if possible. If
 * old_fingerprint is clean and not should_update, this function returns an
 * exact copy of it.
 *
 * This is useful when the user of the function already has a Fingerprint of a
 * file but needs to get a Fingerprint that is up to date. If old_fingerprint is
 * clean, then this function is significantly faster than takeFingerprint,
 * because it only has to do a stat rather than a full hash of the file.
 */
Fingerprint retakeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    const std::string &path,
    const Fingerprint &old_fingerprint);

/**
 * Check if a file still matches a given fingerprint.
 */
MatchesResult fingerprintMatches(
    FileSystem &file_system,
    const std::string &path,
    const Fingerprint &fingerprint) throw(IoError);

}  // namespace shk

namespace std
{

template<>
struct hash<shk::Fingerprint> {
  using argument_type = shk::Fingerprint;
  using result_type = std::size_t;

  result_type operator()(const argument_type &f) const {
    return hash<shk::Hash>()(f.hash) ^ f.timestamp;
  }
};

}  // namespace std
