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

#include <rs/concat.h>
#include <rs/empty.h>
#include <rs/just.h>

#include "test_util.h"

namespace shk {

TEST_CASE("Concat") {
  SECTION("no inputs") {
    auto stream = Concat();
    CHECK(GetAll<int>(stream) == std::vector<int>({}));
  }

  SECTION("one empty input") {
    auto stream = Concat(Empty());
    CHECK(GetAll<int>(stream) == std::vector<int>({}));
  }

  SECTION("two empty inputs") {
    auto stream = Concat(Empty(), Empty());
    CHECK(GetAll<int>(stream) == std::vector<int>({}));
  }

  SECTION("one input with one value") {
    auto stream = Concat(Just(1));
    CHECK(GetAll<int>(stream) == std::vector<int>({ 1 }));
  }

  SECTION("two inputs with one value") {
    auto stream = Concat(Just(1), Just(2));
    CHECK(GetAll<int>(stream) == std::vector<int>({ 1, 2 }));
  }
}

}  // namespace shk
