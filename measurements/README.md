# Measurements of the thesis

Timing data and analysis scripts behind chapter 5 of the master's thesis *Post-processing library
for Vulkan game engines* (University of Vienna, 2026). All runs took place on a MacBook Pro with an
Apple M4 Pro under macOS 15.7.9 with MoltenVK of the Vulkan SDK 1.4.357.1, in release builds without
the validation layer. Every configuration had run once before in a debug build with the validation
layer, which reported no error in the library's commands.

## Programs

- Standalone benchmark: `tests/bench.cpp` with `tests/bench.sh` on the branch `benchmark` of this
  repository. One run of the program measures one configuration: 500 calls of `apply()` without
  timing, then 500 timed calls, each with reset, recording, submission and waiting. `bench.sh`
  runs the rounds, with both image sizes as blocks in random order and the configurations in random
  order inside each block.
  - Series of 2026-10-25 without GPU timestamps: commit
    [`2a3b81f`](https://github.com/orcunilker/ViennaVulkanPostProcessingLibrary/tree/2a3b81fcb140e5ee6660bbc49f0b072711d8ef23)
  - Series of 2026-10-25 with GPU timestamps: commit
    [`63631c5`](https://github.com/orcunilker/ViennaVulkanPostProcessingLibrary/tree/63631c5d41a26e38ea06e8bd48bb9cb59534615a)
  - Chain series of 2026-11-06, both variants from one program (timestamps with
    `VVPP_GPU_TIMESTAMPS=1`): tag `measurements-2026-11-06`, the commit that adds this folder. The
    function `setInvertPasses()` exists only on this branch and fills the chain with the invert shader
    for `invert3`, `invert5` and `invert15`.
- Engine benchmark: `examples/postprocessing_measure` in the fork of the Vienna Vulkan Engine, commit
  [`96c8215`](https://github.com/orcunilker/VVE-using-VVPPL/tree/96c821569a0cfaa585dc81f4b6347e8f584bea0b)
  on the branch `V3_measurement`, with the library `v1.0`. Its timestamps are written only with
  `VVPP_GPU_TIMESTAMPS=1`.

## Data

Each CSV line is one run, the mean of its timed calls or frames in ms. Within a round, every
configuration ran once.

- `2026-10-25/`: the series of Tables 5.4 and 5.5 and Figures 5.2 and 5.3, 20 rounds.
  - `standalone.csv`, `standalone-gpu.csv`: standalone benchmark at 1920 × 1080 and 3840 × 2160,
    the empty chain, the 15 single effects and the full chain, without and with GPU timestamps.
    `mean_ms` is the call time, `gpu_ms` the time between the two timestamps around `apply()`.
  - `engine.csv`, `engine-gpu.csv`: engine benchmark at 1920 × 1080, the direct copy, the empty
    chain and the full chain. `mean_ms` is the frame time, `gpu_post_ms` the post-processing GPU
    time and `gpu_frame_ms` the GPU time of the whole command buffer, both 0 without timestamps.
  - `protocol.json`: the protocol fixed before the series.
- `2026-11-06/`: the chain series of Table 5.6 and Figure 5.4, 20 rounds. Besides the configurations
  of the standalone benchmark above, the default chains of the example applications (`vsg3`:
  chromatic aberration, vignette and film grain, `vve5`: in addition tonemapping and grayscale) and
  the invert shader repeated in three, five and fifteen passes (`invert3`, `invert5`, `invert15`).
  `protocol.json` lists the effects of every chain.

## Reproducing the tables and figures

The scripts need Python 3 with numpy, scipy and matplotlib. The thesis was built with numpy 2.5.4,
scipy 1.18.1 and matplotlib 3.11.2. From this folder:

```sh
python3 analyze.py 2026-10-25/standalone.csv effect_cost.tex . 2026-10-25/standalone-gpu.csv
python3 analyze_engine.py 2026-10-25/engine.csv engine_cost.tex . 2026-10-25/engine-gpu.csv
python3 analyze_chains.py 2026-11-06/standalone.csv 2026-11-06/standalone-gpu.csv chain_cost.tex .
```

`analyze.py` writes Table 5.4 (`effect_cost.tex`) and Figure 5.2 (`eval_additivity.pgf`),
`analyze_engine.py` Table 5.5 (`engine_cost.tex`) and Figure 5.3 (`eval_engine_rounds.pgf`), and
`analyze_chains.py` Table 5.6 (`chain_cost.tex`) and Figure 5.4 (`eval_chains.pgf`). All three print
the differences, confidence intervals and p-values that chapter 5 reports: paired differences per
round, 95 % confidence intervals, t-tests with the Holm correction and the Wilcoxon signed-rank test
as a check. In the thesis, `engine_cost.tex` additionally has three `\phantom{{<}\,}` set by hand
for the alignment of the column.
