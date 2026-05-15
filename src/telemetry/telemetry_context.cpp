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

#include "telemetry/telemetry_context.hpp"

#include "config.hpp"
#include "cucascade/memory/common.hpp"
#include "log/logging.hpp"
#include "op/sirius_physical_delim_join.hpp"
#include "op/sirius_physical_operator.hpp"
#include "pipeline/sirius_pipeline.hpp"
#include "telemetry-bridge/gen/channel.rs.h"
#include "telemetry-bridge/gen/operator.rs.h"
#include "telemetry-bridge/gen/plan.rs.h"
#include "telemetry-bridge/gen/port.rs.h"

#include <unistd.h>

#include <ranges>
#include <string>

namespace sirius::telemetry {

telemetry_context::telemetry_context(std::optional<std::string> query_label)
  : engine_uuid_(uuid::now_v7()),
    worker_uuid_(uuid::now_v7()),
    context_(quent::create_context(uuid::now_v7(),
                                   duckdb::Config::ENABLE_QUENT ? "ndjson" : "noop",
                                   duckdb::Config::QUENT_OUTPUT_DIRECTORY)),
    engine_observer_(quent::engine::create_observer(*context_)),
    worker_observer_(quent::worker::create_observer(*context_)),
    query_label_(std::move(query_label)),
    storage_memory_handle_(quent::memory::create(*context_,
                                                 {
                                                   .instance_name   = "storage",
                                                   .parent_group_id = engine_uuid_,
                                                 })),
    host_memory_handle_(quent::memory::create(*context_,
                                              {
                                                .instance_name   = "host",
                                                .parent_group_id = engine_uuid_,
                                              })),
    device_memory_handle_(quent::memory::create(*context_,
                                                {
                                                  .instance_name   = "device",
                                                  .parent_group_id = engine_uuid_,
                                                })),
    storage_to_host_channel_handle_(
      quent::channel::create(*context_,
                             {
                               .instance_name   = "storage_to_host",
                               .parent_group_id = engine_uuid_,
                               .source_id       = storage_memory_handle_->uuid(),
                               .target_id       = host_memory_handle_->uuid(),
                             })),
    storage_to_device_channel_handle_(
      quent::channel::create(*context_,
                             {
                               .instance_name   = "storage_to_device",
                               .parent_group_id = engine_uuid_,
                               .source_id       = storage_memory_handle_->uuid(),
                               .target_id       = device_memory_handle_->uuid(),
                             })),
    host_to_device_channel_handle_(
      quent::channel::create(*context_,
                             {
                               .instance_name   = "host_to_device",
                               .parent_group_id = engine_uuid_,
                               .source_id       = host_memory_handle_->uuid(),
                               .target_id       = device_memory_handle_->uuid(),
                             })),
    host_to_storage_channel_handle_(
      quent::channel::create(*context_,
                             {
                               .instance_name   = "host_to_storage",
                               .parent_group_id = engine_uuid_,
                               .source_id       = host_memory_handle_->uuid(),
                               .target_id       = storage_memory_handle_->uuid(),
                             })),
    device_to_storage_channel_handle_(
      quent::channel::create(*context_,
                             {
                               .instance_name   = "device_to_storage",
                               .parent_group_id = engine_uuid_,
                               .source_id       = device_memory_handle_->uuid(),
                               .target_id       = storage_memory_handle_->uuid(),
                             })),
    device_to_host_channel_handle_(
      quent::channel::create(*context_,
                             {
                               .instance_name   = "device_to_host",
                               .parent_group_id = engine_uuid_,
                               .source_id       = device_memory_handle_->uuid(),
                               .target_id       = host_memory_handle_->uuid(),
                             }))
{
  const std::string& engine_name = duckdb::Config::QUENT_ENGINE_NAME;

  engine_observer_->init(engine_uuid_,
                         quent::engine::Init{
                           .implementation =
                             quent::engine::Implementation{
                               .name              = engine_name,
                               .version           = "",
                               .custom_attributes = {},
                             },
                           .instance_name = engine_name,
                         });

  worker_observer_->init(worker_uuid_,
                         quent::worker::Init{
                           .parent_engine_id = engine_uuid_,
                           .instance_name    = fmt::format("worker-{}", getpid()),
                         });

  SIRIUS_LOG_INFO("Telemetry context initialized (engine={})", engine_name);
}

telemetry_context::~telemetry_context()
{
  worker_observer_->exit(worker_uuid_);
  engine_observer_->exit(engine_uuid_);

  storage_memory_handle_->exit();
  host_memory_handle_->exit();
  device_memory_handle_->exit();

  storage_to_host_channel_handle_->exit();
  storage_to_device_channel_handle_->exit();
  host_to_device_channel_handle_->exit();
  host_to_storage_channel_handle_->exit();
  device_to_host_channel_handle_->exit();
  device_to_storage_channel_handle_->exit();
}

const quent::memory::MemoryHandle& telemetry_context::memory_handle(
  const cucascade::memory::memory_space_id memory_space) const
{
  switch (memory_space.tier) {
    case cucascade::memory::Tier::GPU: {
      // TODO: only switching based on tier for now, once we go multi-gpu
      // we would need to switch over memory_space.device_id too.
      return *device_memory_handle_;
    }
    case cucascade::memory::Tier::HOST: {
      return *host_memory_handle_;
    }
    case cucascade::memory::Tier::DISK: {
      return *storage_memory_handle_;
    }
    case cucascade::memory::Tier::SIZE: {
      throw std::invalid_argument("Tier::SIZE is not a valid tier");
    }
  }

  throw std::invalid_argument("Invalid Tier");
}

const quent::channel::ChannelHandle& telemetry_context::channel_handle(
  const cucascade::memory::memory_space_id source_memory_space,
  const cucascade::memory::memory_space_id target_memory_space) const
{
  switch (source_memory_space.tier) {
    case cucascade::memory::Tier::GPU: {
      switch (target_memory_space.tier) {
        case cucascade::memory::Tier::GPU: {
          // TODO: only switching based on tier for now, once we go multi-gpu
          // we would need to switch over memory_space.device_id too.
          throw std::invalid_argument(
            "No channel handle exists for source and target both being GPU");
        }
        case cucascade::memory::Tier::HOST: {
          return *device_to_host_channel_handle_;
        }
        case cucascade::memory::Tier::DISK: {
          return *device_to_storage_channel_handle_;
        }
        case cucascade::memory::Tier::SIZE: {
          throw std::invalid_argument("Tier::SIZE is not a valid source tier");
        }
      }
    }
    case cucascade::memory::Tier::HOST: {
      switch (target_memory_space.tier) {
        case cucascade::memory::Tier::GPU: {
          return *host_to_device_channel_handle_;
        }
        case cucascade::memory::Tier::HOST: {
          throw std::invalid_argument(
            "No channel handle exists for source and target both being HOST");
        }
        case cucascade::memory::Tier::DISK: {
          return *host_to_storage_channel_handle_;
        }
        case cucascade::memory::Tier::SIZE: {
          throw std::invalid_argument("Tier::SIZE is not a valid source tier");
        }
      }
    }
    case cucascade::memory::Tier::DISK: {
      switch (target_memory_space.tier) {
        case cucascade::memory::Tier::GPU: {
          return *storage_to_device_channel_handle_;
        }
        case cucascade::memory::Tier::HOST: {
          return *storage_to_host_channel_handle_;
        }
        case cucascade::memory::Tier::DISK: {
          throw std::invalid_argument(
            "No channel handle exists for source and target both being DISK");
        }
        case cucascade::memory::Tier::SIZE: {
          throw std::invalid_argument("Tier::SIZE is not a valid source tier");
        }
      }
    }
    case cucascade::memory::Tier::SIZE: {
      throw std::invalid_argument("Tier::SIZE is not a valid source tier");
    }
  }

  throw std::invalid_argument("Invalid Tiers");
}

void emit_plan_telemetry(
  const quent::Context& context,
  const duckdb::vector<duckdb::shared_ptr<pipeline::sirius_pipeline>>& pipelines,
  const uuid::UUID plan_id,
  const query_telemetry_info telemetry_info)
{
  auto operator_obs = quent::operator_::create_observer(context);
  auto port_obs     = quent::port::create_observer(context);
  auto plan_obs     = quent::plan::create_observer(context);

  // Collect edges while iterating
  rust::Vec<quent::plan::Edges> edges;

  for (const auto& pipeline : pipelines) {
    const auto pipeline_uuid         = pipeline->pipeline_uuid();
    const auto operators             = pipeline->get_operators();
    const std::string operator_chain = [&operators]() {
      std::string chain{};
      for (const auto& name : operators | std::views::transform([](const auto& op) {
                                return fmt::format(
                                  "{}({})", op.get().get_name(), op.get().operator_id);
                              })) {
        if (chain.empty()) {
          chain = name;
          continue;
        }
        chain = fmt::format("{} -> {}", chain, name);
      }
      return chain;
    }();

    operator_obs->declaration(
      pipeline_uuid,
      quent::operator_::Declaration{
        .plan_id             = plan_id,
        .parent_operator_ids = {},
        .instance_name       = operator_chain,
        .type_name           = fmt::format("Pipeline Id {}", pipeline->get_pipeline_id()),
        .custom_attributes   = {},
      });

    // Receiver ports on pipeline source operators.
    if (auto source = pipeline->get_source()) {
      for (std::string_view port_id : source->get_port_ids()) {
        if (const op::sirius_physical_operator::port* port = source->get_port(port_id)) {
          port_obs->declaration(port->source_port_uuid,
                                quent::port::Declaration{
                                  .operator_id   = pipeline_uuid,
                                  .instance_name = fmt::format("{}_receiver", port_id),
                                });
        }
      }
    }

    // Sender ports on pipeline sink(last) operators.
    for (const auto& [next_operator, next_operator_port_name, pseudo_sink_port_uuid] :
         pipeline->get_next_ports_after_sink()) {
      // Declare the pseudo-sink port
      port_obs->declaration(pseudo_sink_port_uuid,
                            quent::port::Declaration{
                              .operator_id   = pipeline_uuid,
                              .instance_name = fmt::format("{}_sender", next_operator_port_name),
                            });

      // Find the target port on the downstream operator
      if (const op::sirius_physical_operator::port* target_port =
            next_operator->get_port(next_operator_port_name)) {
        edges.push_back(quent::plan::Edges{
          .source = pseudo_sink_port_uuid,
          .target = target_port->source_port_uuid,
        });
      }
    }
  }

  plan_obs->declaration(plan_id,
                        quent::plan::Declaration{
                          .parent =
                            quent::plan::Parent{
                              .query_id = telemetry_info.query_id,
                              .plan_id  = uuid::new_nil(),  // no parent plan
                            },
                          .instance_name = "pipeline_plan",
                          .edges         = std::move(edges),
                          .worker_id     = telemetry_info.worker_id,
                        });
}

}  // namespace sirius::telemetry
