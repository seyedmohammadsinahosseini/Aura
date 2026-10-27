#!/bin/env python

import onnx
import onnx_graphsurgeon as gs

# Load model
model = onnx.load("det_500m_FP32.onnx")
graph = gs.import_onnx(model)

# Print current outputs (optional)
print("Before reordering:")
for o in graph.outputs:
    print(" ", o.name)

# Build a dict for easier lookup
outputs = {out.name: out for out in graph.outputs}

# Define desired order
desired_order = [
    "443/sink_port_0",
    "468/sink_port_0",
    "493/sink_port_0",
    "446/sink_port_0",
    "471/sink_port_0",
    "496/sink_port_0",
    "449/sink_port_0",
    "474/sink_port_0",
    "499/sink_port_0"
]

# Verify all exist
for name in desired_order:
    if name not in outputs:
        raise ValueError(f"Output '{name}' not found in current graph outputs.")

# Reorder outputs
graph.outputs = [outputs[name] for name in desired_order]

# Save new model
onnx.save(gs.export_onnx(graph), "det_500m_FP32_reordered.onnx")

print("✅ Outputs reordered successfully:")
for o in graph.outputs:
    print(" ", o.name)
