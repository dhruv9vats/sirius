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

#pragma once

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
#include <string>

namespace sirius::pipeline {
class sirius_pipeline;
}  // namespace sirius::pipeline

namespace sirius::telemetry {

/// Owns the top-level telemetry states for a single Sirius session (sirius_interface level).
class telemetry_context {
 public:
  telemetry_context(std::optional<std::string> query_label);
  ~telemetry_context();

  // Non-copyable, non-movable (owns opaque Rust boxes)
  telemetry_context(const telemetry_context&)            = delete;
  telemetry_context& operator=(const telemetry_context&) = delete;
  telemetry_context(telemetry_context&&)                 = delete;
  telemetry_context& operator=(telemetry_context&&)      = delete;

  [[nodiscard]] const uuid::UUID& engine_id() const { return engine_uuid_; }
  [[nodiscard]] const uuid::UUID& worker_id() const { return worker_uuid_; }
  [[nodiscard]] const quent::Context& context() const { return *context_; }
  [[nodiscard]] const std::optional<std::string>& query_label() const { return query_label_; }

  [[nodiscard]] const quent::memory::MemoryHandle& memory_handle(
    const cucascade::memory::memory_space_id memory_space) const;

  [[nodiscard]] const quent::channel::ChannelHandle& channel_handle(
    const cucascade::memory::memory_space_id source_memory_space,
    const cucascade::memory::memory_space_id target_memory_space) const;

 private:
  uuid::UUID engine_uuid_;
  uuid::UUID worker_uuid_;
  rust::Box<quent::Context> context_;
  rust::Box<quent::engine::EngineObserver> engine_observer_;
  rust::Box<quent::worker::WorkerObserver> worker_observer_;
  std::optional<std::string> query_label_;

  // memory handles
  rust::Box<quent::memory::MemoryHandle> storage_memory_handle_;
  rust::Box<quent::memory::MemoryHandle> host_memory_handle_;
  rust::Box<quent::memory::MemoryHandle> device_memory_handle_;

  // channel handles
  rust::Box<quent::channel::ChannelHandle> storage_to_host_channel_handle_;
  rust::Box<quent::channel::ChannelHandle> storage_to_device_channel_handle_;

  rust::Box<quent::channel::ChannelHandle> host_to_device_channel_handle_;
  rust::Box<quent::channel::ChannelHandle> host_to_storage_channel_handle_;

  rust::Box<quent::channel::ChannelHandle> device_to_host_channel_handle_;
  rust::Box<quent::channel::ChannelHandle> device_to_storage_channel_handle_;
};

// A POD to hold common identifiers for useful telemetry.
struct query_telemetry_info {
  uuid::UUID query_id;
  uuid::UUID worker_id;
};

/// Emit plan-level telemetry (operator declarations, port declarations, edges)
/// for the given set of pipelines. Called once during query construction.
void emit_plan_telemetry(
  const quent::Context& context,
  const duckdb::vector<duckdb::shared_ptr<pipeline::sirius_pipeline>>& pipelines,
  uuid::UUID plan_id,
  query_telemetry_info telemetry_info);

// /**
//  * @brief Implementation of idata_batch_probe that forwards state transitions to a telemetry
//  * observer.
//  *
//  * This probe bridges the cucascade data_batch state machine with the telemetry instrumentation
//  * system. Each state transition is forwarded to the Rust-side DataBatchObserver via the cxx FFI
//  * bridge, which emits the corresponding DataBatchEvent.
//  *
//  * Usage:
//  * @code
//  *   auto probe = std::make_unique<quent_data_batch_probe>();
//  *   auto batch = std::make_shared<cucascade::data_batch>(batch_id, std::move(data),
//  * std::move(probe));
//  * @endcode
//  */
// class quent_data_batch_probe : public cucascade::idata_batch_probe {
//  public:
//   /**
//    * @brief Construct a probe that forwards state transitions to a telemetry observer.
//    *
//    * Creates its own DataBatchObserver from the given telemetry_context.
//    * The memory resource UUID for idle events is resolved at runtime based on the
//    * data's current memory tier.
//    *
//    * @param ctx The telemetry context providing instrumentation and memory UUIDs.
//    */
//   explicit quent_data_batch_probe(const telemetry::telemetry_context& ctx)
//     : _observer(telemetry::data_batch::create_observer(ctx.context())),
//       _entity_uuid(uuid::UUID::now_v7()),
//       _device_memory_uuid(ctx.get_device_memory_uuid()),
//       _host_memory_uuid(ctx.get_host_memory_uuid()),
//       _storage_memory_uuid(ctx.get_storage_memory_uuid())
//   {
//   }

//   ~quent_data_batch_probe() override { _observer->destructed(_entity_uuid); }

//   void state_transitioned_to(const cucascade::batch_state& new_state,
//                              const uint64_t& /*batch_id*/,
//                              const cucascade::idata_representation& data,
//                              const size_t& processing_count,
//                              const size_t& task_created_count) override
//   {
//     auto memory_uuid_for_tier = [this](cucascade::memory::Tier tier) -> uuid::UUID {
//       switch (tier) {
//         case cucascade::memory::Tier::GPU: return _device_memory_uuid;
//         case cucascade::memory::Tier::HOST: return _host_memory_uuid;
//         case cucascade::memory::Tier::DISK: return _storage_memory_uuid;
//         default: return uuid::UUID::new_nil();
//       }
//     };

//     auto make_idle = [&]() {
//       return telemetry::data_batch::idle{
//         .device_id        = data.get_device_id(),
//         .use_memory       = memory_uuid_for_tier(data.get_current_tier()),
//         .use_memory_bytes = static_cast<uint64_t>(data.get_size_in_bytes()),
//       };
//     };

//     switch (new_state) {
//       case cucascade::batch_state::idle:
//         switch (data.get_current_tier()) {
//           case cucascade::memory::Tier::GPU: {
//             _observer->idle_on_gpu(_entity_uuid, make_idle());
//             break;
//           }
//           case cucascade::memory::Tier::HOST: {
//             _observer->idle_on_host(_entity_uuid, make_idle());
//             break;
//           }
//           case cucascade::memory::Tier::DISK: {
//             _observer->idle_on_storage(_entity_uuid, make_idle());
//             break;
//           }
//           default: {
//             SIRIUS_LOG_WARN(
//               "Unaccounted data_batch state encountered while emitting telemetry event");
//             break;
//           }
//         }
//         break;
//       case cucascade::batch_state::task_created:
//         _observer->task_created(_entity_uuid,
//                                 telemetry::data_batch::task_created{
//                                   .processing_count   = static_cast<uint64_t>(processing_count),
//                                   .task_created_count =
//                                   static_cast<uint64_t>(task_created_count),
//                                 });
//         break;
//       case cucascade::batch_state::processing:
//         _observer->processing(_entity_uuid,
//                               telemetry::data_batch::processing{
//                                 .processing_count   = static_cast<uint64_t>(processing_count),
//                                 .task_created_count = static_cast<uint64_t>(task_created_count),
//                               });
//         break;
//       case cucascade::batch_state::in_transit:
//         _observer->in_transit(
//           _entity_uuid,
//           telemetry::data_batch::in_transit{
//             .use_channel           = uuid::UUID::new_nil(),
//             .use_channel_type_name = "",
//             .use_channel_bytes     = static_cast<uint64_t>(data.get_size_in_bytes()),
//           });
//         break;
//     }
//     quent::channel::create(const int &ctx, ::quent::channel::Initializing data)
//   }

//  private:
//   rust::Box<telemetry::data_batch::observer> _observer;
//   uuid::UUID _entity_uuid;
//   uuid::UUID _device_memory_uuid;
//   uuid::UUID _host_memory_uuid;
//   uuid::UUID _storage_memory_uuid;
// };

}  // namespace sirius::telemetry
