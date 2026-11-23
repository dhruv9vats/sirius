# NVL4 SF3000 SNMG reproducer

The standard TPC-H harness measured **182.81 s stock → 132.53 s patched**
on invocation 3 (sum of Q1–Q22, 27.5% lower latency).

From the Sirius root, switch to the reproducer branch:

```bash
git switch repro/nvl4-snmg-sf3000
```

If `cucascade` is not initialized yet, point it at the fork before cloning:

```bash
git config --local submodule.cucascade.url https://github.com/dhruv9vats/cuCascade.git
git submodule update --init --checkout -- cucascade
```

Check out the cuCascade branch explicitly:

```bash
git -C cucascade fetch https://github.com/dhruv9vats/cuCascade.git repro/nvl4-snmg-sf3000
git -C cucascade switch -c repro/nvl4-snmg-sf3000 FETCH_HEAD
git submodule status -- cucascade
```

If the local cuCascade branch already exists, omit `-c` and `FETCH_HEAD`.
The status should show `db020514…` without a leading `+`, confirming that
the checkout matches Sirius's pin.

Initialize the other build submodules and build with
`pixi run make TEST_BUILD_TARGET=`. On an idle four-GPU NVL4 node, run this
from the Sirius root with the [tuned YAML](nvl4-snmg-repro.yaml):

```bash
env -u SIRIUS_BUILD_PATH -u SIRIUS_GDS -u SIRIUS_PRIORITY_DOWNSTREAM_FIRST \
    -u SIRIUS_Q6_LOCAL_PARQUET_READ -u CUCASCADE_Q6_READ_DEPTH -u CUCASCADE_Q6_IO_PROBE \
    CUDA_DEVICE_ORDER=PCI_BUS_ID CUDA_VISIBLE_DEVICES=0,1,2,3 SIRIUS_DISABLE=0 \
    SIRIUS_LOG_BACKEND=spdlog SIRIUS_LOG_LEVEL=info TZ=UTC \
    SIRIUS_PRE_SQL='SET enable_duckdb_fallback = false' \
    pixi run python test/tpch_performance/performance_test.py \
    --input /scratch/sirius/datasets/tpch_sf3000 \
    --data-source parquet --scale-factor 3000 --engine gpu --iterations 3 --pin none \
    --config test/tpch_performance/nvl4-snmg-repro.yaml \
    --output /var/tmp/nvl4-snmg-repro --name full-stack-ab-patched
```

For the stock baseline, run the same command afterward from a separate
build at Sirius `ef7eb295` and its pinned cuCascade `9e6cdc8f`. Use a copy
of the tuned YAML with only `executor.scan_manager.uring` removed, and
change `--name` to `full-stack-ab-stock`.

In each output's `csv/runtimes.csv`, sum Q1–Q22 at `iteration=2` for the
third invocation. The command reproduces the setup, not the exact seconds:
GPFS cache state and contention are not controlled.
