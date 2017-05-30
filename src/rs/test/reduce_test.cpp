// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <catch.hpp>

#include <vector>

#include <rs/empty.h>
#include <rs/iterate.h>
#include <rs/just.h>
#include <rs/reduce.h>
#include <rs/subscriber.h>

#include "test_util.h"

namespace shk {

TEST_CASE("Reduce") {
  auto sum = Reduce(100, [](int a, int v) { return a + v; });
  auto fail_on = [](int fail_value, int call_count) {
    return Reduce(100, [fail_value, call_count, times_called = 0](
        int a, int v) mutable {
      CHECK(++times_called <= call_count);

      if (v == fail_value) {
        throw std::runtime_error("fail_on");
      } else {
        return a + v;
      }
    });
  };

  SECTION("empty") {
    CHECK(GetOne<int>(sum(Empty())) == 100);
  }

  SECTION("one value") {
    CHECK(GetOne<int>(sum(Just(1))) == 101);
  }

  SECTION("two values") {
    CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 }))) == 103);
  }

  SECTION("request zero") {
    CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 0) == 0);
  }

  SECTION("request one") {
    CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 1) == 103);
  }

  SECTION("request two") {
    CHECK(GetOne<int>(sum(Iterate(std::vector<int>{ 1, 2 })), 2) == 103);
  }

  SECTION("error on first") {
    auto error = GetError(fail_on(0, 1)(Iterate(std::vector<int>{ 0 })));
    CHECK(GetErrorWhat(error) == "fail_on");
  }

  SECTION("error on first of two") {
    // The reducer functor should be invoked only once
    auto error = GetError(fail_on(0, 1)(Iterate(std::vector<int>{ 0, 1 })));
    CHECK(GetErrorWhat(error) == "fail_on");
  }
}

}  // namespace shk