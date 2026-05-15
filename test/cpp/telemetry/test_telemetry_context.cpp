/*
 * Copyright 2025, Sirius Contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "catch.hpp"
#include "cucascade/data/data_batch.hpp"
#include "cucascade/memory/common.hpp"
#include "duckdb/common/common.hpp"
#include "telemetry-bridge/gen/channel.rs.h"
#include "telemetry-bridge/gen/context.rs.h"
#include "telemetry-bridge/gen/engine.rs.h"
#include "telemetry-bridge/gen/memory.rs.h"
#include "telemetry-bridge/gen/uuid.rs.h"
#include "telemetry-bridge/gen/worker.rs.h"

#include <optional>
#include <stdexcept>
#include <string>

// Expose owned handles so these tests can verify reference dispatch directly.
#define private public
#include "telemetry/telemetry_context.hpp"
#undef private

namespace {

using cucascade::memory::memory_space_id;
using cucascade::memory::Tier;
using sirius::telemetry::telemetry_context;

TEST_CASE("telemetry context const memory_handle returns handle for memory tier",
          "[telemetry][telemetry_context]")
{
  telemetry_context context(std::nullopt);
  const telemetry_context& const_context = context;

  REQUIRE(&const_context.memory_handle(memory_space_id{Tier::GPU, 0}) ==
          &*context.device_memory_handle_);
  REQUIRE(&const_context.memory_handle(memory_space_id{Tier::HOST, 0}) ==
          &*context.host_memory_handle_);
  REQUIRE(&const_context.memory_handle(memory_space_id{Tier::DISK, 0}) ==
          &*context.storage_memory_handle_);

  SECTION("device id is ignored while telemetry tracks one handle per tier")
  {
    REQUIRE(&const_context.memory_handle(memory_space_id{Tier::GPU, 7}) ==
            &*context.device_memory_handle_);
    REQUIRE(&const_context.memory_handle(memory_space_id{Tier::HOST, 7}) ==
            &*context.host_memory_handle_);
    REQUIRE(&const_context.memory_handle(memory_space_id{Tier::DISK, 7}) ==
            &*context.storage_memory_handle_);
  }

  SECTION("invalid tiers throw")
  {
    REQUIRE_THROWS_AS(const_context.memory_handle(memory_space_id{Tier::SIZE, 0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(const_context.memory_handle(memory_space_id{static_cast<Tier>(-1), 0}),
                      std::invalid_argument);
  }
}

TEST_CASE("telemetry context const channel_handle returns handle for tier pair",
          "[telemetry][telemetry_context]")
{
  telemetry_context context(std::nullopt);
  const telemetry_context& const_context = context;

  const memory_space_id gpu{Tier::GPU, 0};
  const memory_space_id host{Tier::HOST, 0};
  const memory_space_id storage{Tier::DISK, 0};

  REQUIRE(&const_context.channel_handle(storage, host) ==
          &*context.storage_to_host_channel_handle_);
  REQUIRE(&const_context.channel_handle(storage, gpu) ==
          &*context.storage_to_device_channel_handle_);
  REQUIRE(&const_context.channel_handle(host, gpu) == &*context.host_to_device_channel_handle_);
  REQUIRE(&const_context.channel_handle(host, storage) ==
          &*context.host_to_storage_channel_handle_);
  REQUIRE(&const_context.channel_handle(gpu, host) == &*context.device_to_host_channel_handle_);
  REQUIRE(&const_context.channel_handle(gpu, storage) ==
          &*context.device_to_storage_channel_handle_);

  SECTION("same-tier transfers have no channel")
  {
    REQUIRE_THROWS_AS(const_context.channel_handle(gpu, memory_space_id{Tier::GPU, 1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(const_context.channel_handle(host, memory_space_id{Tier::HOST, 1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(const_context.channel_handle(storage, memory_space_id{Tier::DISK, 1}),
                      std::invalid_argument);
  }

  SECTION("Tier::SIZE source or target throws")
  {
    const memory_space_id invalid{Tier::SIZE, 0};

    REQUIRE_THROWS_AS(const_context.channel_handle(invalid, gpu), std::invalid_argument);
    REQUIRE_THROWS_AS(const_context.channel_handle(gpu, invalid), std::invalid_argument);
    REQUIRE_THROWS_AS(const_context.channel_handle(host, invalid), std::invalid_argument);
    REQUIRE_THROWS_AS(const_context.channel_handle(storage, invalid), std::invalid_argument);
  }
}

}  // namespace
