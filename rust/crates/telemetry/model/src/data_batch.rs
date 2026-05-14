use quent_model::{Attributes, fsm, state};
use serde::{Deserialize, Serialize};

#[derive(Attributes, Debug, Deserialize, Serialize)]
pub struct MemorySpaceId {
    pub tier: String,
    pub device_id: i32,
}

state! {
    Created {
        attributes: {
            batch_id: i32,
        },
    }
}

state! {
    Idle {
        attributes: {
            memory_space_id: MemorySpaceId,
        },
        usages: {
            memory: quent_stdlib::memory::Memory,
        },
    }
}

state! {
    InTransit {
        usages: {
            channel: quent_stdlib::channel::Channel,
        },
    }
}

fsm! {
    DataBatch {
        states: {
            created: Created,
            idle: Idle,
            in_transit: InTransit
        },
        entry: created,
        exit_from: { idle },
        transitions: {
            created => idle,
            idle => in_transit,
            in_transit => idle,
        },
    }
}
