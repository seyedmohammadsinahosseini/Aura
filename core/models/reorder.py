#!/usr/bin/env python3
"""Reorder the output tensors of the face-detection ONNX model.

The TensorRT binding code in `modules/detection/CDetection.cpp` consumes the detector's nine
multi-scale outputs in a fixed order, while the released ONNX graph exposes them in a different
order. This script rewrites the graph's output list in place so that `trtexec` produces an engine
whose bindings line up with the code.

Usage:
    python3 reorder.py [input.onnx] [output.onnx]

Defaults keep the historical behaviour (read `det_500m_FP32.onnx` next to this file, write
`det_500m_FP32_reordered.onnx`).

Requires: onnx, onnx-graphsurgeon   ->  pip install onnx onnx-graphsurgeon
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

def _require_onnx():
    """Import the tooling deps lazily so `--help` works without them."""
    try:
        import onnx
        import onnx_graphsurgeon as gs
    except ModuleNotFoundError as exc:
        sys.exit(f"missing dependency {exc.name!r}: pip install onnx onnx-graphsurgeon")
    return onnx, gs

# Output order expected by the detection bindings: three scales x three heads.
DESIRED_ORDER = [
    "443/sink_port_0",
    "468/sink_port_0",
    "493/sink_port_0",
    "446/sink_port_0",
    "471/sink_port_0",
    "496/sink_port_0",
    "449/sink_port_0",
    "474/sink_port_0",
    "499/sink_port_0",
]

HERE = Path(__file__).resolve().parent


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input", nargs="?", type=Path, default=HERE / "det_500m_FP32.onnx")
    parser.add_argument("output", nargs="?", type=Path, default=HERE / "det_500m_FP32_reordered.onnx")
    parser.add_argument(
        "--order",
        nargs="*",
        default=DESIRED_ORDER,
        help="override the desired sink-port order if you are using a differently named export",
    )
    args = parser.parse_args(argv)

    if not args.input.is_file():
        sys.exit(f"input model not found: {args.input}")

    onnx, gs = _require_onnx()
    graph = gs.import_onnx(onnx.load(str(args.input)))

    print("Outputs before reordering:")
    for out in graph.outputs:
        print("  ", out.name)

    by_name = {out.name: out for out in graph.outputs}
    missing = [name for name in args.order if name not in by_name]
    if missing:
        print(
            "error: these expected outputs are not in the graph:\n  "
            + "\n  ".join(missing)
            + "\nUse --order to pass the sink_port names of your export.",
            file=sys.stderr,
        )
        return 2

    # Anything not listed is appended rather than dropped, so a differently named
    # export never silently loses an output.
    extra = [out for out in graph.outputs if out.name not in args.order]
    graph.outputs = [by_name[name] for name in args.order] + extra

    args.output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(gs.export_onnx(graph), str(args.output))

    print(f"Outputs reordered -> {args.output}")
    for out in graph.outputs:
        print("  ", out.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
