# Portfolio figures

Generated from existing evidence numbers / architecture text (2026-08-05).  
Not formal clean-baseline artwork; captions must keep the same “不能声称” boundaries.

| File | Use |
|---|---|
| [`01_platform_topology.png`](01_platform_topology.png) | ThinkPad vs Orange Pi roles + CAN boundary |
| [`02_runtime_layers.png`](02_runtime_layers.png) | V1 Runtime layering |
| [`03_rt1_other_vs_fifo.png`](03_rt1_other_vs_fifo.png) | RT1 smoke: same-core OTHER vs FIFO |
| [`04_rt6_segments_p50.png`](04_rt6_segments_p50.png) | RT6 baseline segment p50 (software peer) |

Regenerate:

```bash
python3 evidence/portfolio/figures/render_portfolio_figures.py
```
