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

#include <rx/count.h>
#include <rx/iterate.h>

#include "get.h"

namespace shk {

TEST_CASE("Count") {
  auto count = Count();

  SECTION("empty") {
    CHECK(get<int>(count(Iterate(std::vector<int>{}))) == 0);
  }

  SECTION("one value") {
    CHECK(get<int>(count(Iterate(std::vector<int>{ 1 }))) == 1);
  }

  SECTION("two values") {
    CHECK(get<int>(count(Iterate(std::vector<int>{ 1, 2 }))) == 2);
  }
}

}  // namespace shk
