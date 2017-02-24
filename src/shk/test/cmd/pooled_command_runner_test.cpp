#include <catch.hpp>

#include "cmd/limited_command_runner.h"
#include "cmd/pooled_command_runner.h"

#include "../in_memory_file_system.h"
#include "../dummy_command_runner.h"

namespace shk {

TEST_CASE("PooledCommandRunner") {
  std::unordered_map<std::string, int> pools{
      { "a", 0 }, { "b", 1 }, { "c", 2 } };
  InMemoryFileSystem fs;

  const auto runner = makePooledCommandRunner(
      pools,
      std::unique_ptr<CommandRunner>(new DummyCommandRunner(fs)));

  const auto limited_runner = makePooledCommandRunner(
      pools,
      makeLimitedCommandRunner(
          [&]() { return 0; },
          0.5,
          2,
          std::unique_ptr<CommandRunner>(
              new DummyCommandRunner(fs))));

  const auto cmd = DummyCommandRunner::constructCommand({}, {});

  SECTION("SizeWithoutDelayedCommands") {
    CHECK(runner->size() == 0);
    bool callback_called = false;
    runner->invoke(cmd, "", [&](CommandRunner::Result &&) {
      callback_called = true;
    });
    CHECK(runner->size() == 1);
    CHECK(!callback_called);
    CHECK(!runner->runCommands());
    CHECK(callback_called);
    CHECK(runner->size() == 0);
  }

  SECTION("SizeWithDelayedCommands") {
    CHECK(runner->size() == 0);
    runner->invoke(cmd, "a", [&](CommandRunner::Result &&) {});
    CHECK(runner->size() == 1);
    runner->invoke(cmd, "a", [&](CommandRunner::Result &&) {});
    CHECK(runner->size() == 2);
    runner->invoke(cmd, "b", [&](CommandRunner::Result &&) {});
    CHECK(runner->size() == 3);
  }

  SECTION("CanRunMore") {
    CHECK(limited_runner->canRunMore());
    limited_runner->invoke(cmd, "", [&](CommandRunner::Result &&) {});
    CHECK(limited_runner->canRunMore());
    limited_runner->invoke(cmd, "", [&](CommandRunner::Result &&) {});
    CHECK(!limited_runner->canRunMore());
  }

  SECTION("CanRunMoreWithDelayedCommands") {
    // Pool b is size 1 so it will never reach the parallellism limit
    // of 2 in the limited_runner.
    limited_runner->invoke(cmd, "b", [&](CommandRunner::Result &&) {});
    CHECK(limited_runner->canRunMore());
    limited_runner->invoke(cmd, "b", [&](CommandRunner::Result &&) {});
    CHECK(limited_runner->canRunMore());
    limited_runner->invoke(cmd, "b", [&](CommandRunner::Result &&) {});
    CHECK(limited_runner->canRunMore());
  }

  SECTION("ConsolePoolIsSize1") {
    // The built-in console pool is size 1 so it will never reach the
    // parallellism limit of 2 in the limited_runner.
    limited_runner->invoke(cmd, "console", [&](CommandRunner::Result &&) {});
    CHECK(limited_runner->canRunMore());
    limited_runner->invoke(cmd, "console", [&](CommandRunner::Result &&) {});
    CHECK(limited_runner->canRunMore());
    limited_runner->invoke(cmd, "console", [&](CommandRunner::Result &&) {});
    CHECK(limited_runner->canRunMore());
  }

  SECTION("DelayedCommandsAreEventuallyInvoked") {
    static constexpr int callbacks_count = 5;
    int callbacks_called = 0;
    for (int i = 0; i < callbacks_count; i++) {
      runner->invoke(cmd, "b", [&](CommandRunner::Result &&) {
        callbacks_called++;
      });
    }
    CHECK(runner->size() == callbacks_count);
    CHECK(callbacks_called == 0);
    while (!runner->empty()) {
      CHECK(!runner->runCommands());
    }
    CHECK(callbacks_called == callbacks_count);
  }

  SECTION("DelayedCommandsAreRunInOrder") {
    static constexpr int callbacks_count = 5;
    int callbacks_called = 0;
    for (int i = 0; i < callbacks_count; i++) {
      runner->invoke(cmd, "b", [&callbacks_called, i](
          CommandRunner::Result &&) {
        CHECK(callbacks_called == i);
        callbacks_called++;
      });
    }
    while (!runner->empty()) {
      CHECK(!runner->runCommands());
    }
    CHECK(callbacks_called == callbacks_count);
  }

}

}  // namespace shk