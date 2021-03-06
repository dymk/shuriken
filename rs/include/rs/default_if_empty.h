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

#pragma once

#include <rs/if_empty.h>
#include <rs/just.h>

namespace shk {

/**
 * Takes a stream of values and makes a stream of values that has all the values
 * in that stream. If that stream turns out to be empty, the provided values are
 * emitted.
 */
template <typename ...Ts>
auto DefaultIfEmpty(Ts &&...ts) {
  return IfEmpty(Just(ts...));
}

}  // namespace shk
