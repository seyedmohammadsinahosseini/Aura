# Model weights

**Model weights are not distributed in this repository.** They are third-party artifacts with their
own (often research-only) licenses, and TensorRT engines are hardware-specific anyway. This document
says exactly what the code expects and how to get it there.

## What the application loads

Configured through the `models` section of `core/config/config.json`; resolved relative to the
config file's directory and its parents (see
[`core/config/Config.cpp`](../core/config/Config.cpp)), which is why files placed in `core/models/`
are picked up by the default configuration.

| Purpose | Config keys | Expected file | Format |
|---------|-------------|---------------|--------|
| Face detection | `detection_onnx`, `detection_engine` | `core/models/det_500m_FP32.onnx` / `.engine` | ONNX → TensorRT |
| Face embedding (recognition) | `recognition_onnx`, `recognition_engine` | `core/models/webface_r50.onnx` / `.engine` | ONNX → TensorRT |
| Head detection | `head_onnx`, `head_engine` | `core/models/headdetection_v12.onnx` / `.engine` | ONNX → TensorRT |
| Reference / INT8 path | — | `core/models/facedetect/model.etlt` | NVIDIA TAO (ETLT) |

The `.engine` is used when present; the `.onnx` is only an input to conversion.

`FaceConfig::IsValid()` only proves that the config file exists and parses — it does **not** check
that the referenced model files are on disk. A missing engine therefore surfaces later, when the
TensorRT loader in the inference service tries to open it, not as a configuration error.

## Supporting files that *are* in this repository

| File | Purpose |
|------|---------|
| [`core/models/reorder.py`](../core/models/reorder.py) | Reorders the detection model's multiple output tensors into the order the TensorRT binding code expects. Run it before `trtexec`. |
| [`core/models/facedetect/int8_calibration.txt`](../core/models/facedetect/int8_calibration.txt) | `TRT-8001-EntropyCalibration2` cache for INT8 quantisation of the TAO face detector. |
| `core/models/openvino/*.xml.hash` | Hashes recorded while validating OpenVINO IR exports during model bring-up. |

## Converting

```bash
# 1. reorder the detection outputs, then build engines for THIS GPU
python3 core/models/reorder.py core/models/det_500m_FP32.onnx det_500m_FP32_reordered.onnx

trtexec --onnx=det_500m_FP32_reordered.onnx --saveEngine=det_500m_FP32.engine --fp16
trtexec --onnx=core/models/webface_r50.onnx  --saveEngine=webface_r50.engine  --fp16
trtexec --onnx=core/models/headdetection_v12.onnx --saveEngine=headdetection_v12.engine --fp16

# 2. INT8 (optional): needs a calibration dataset and the NVIDIA tao-converter tool
tao-converter -o facedetect.engine -t fp16 -i nchw -e nchw \
              -c int8_calibration.txt model.etlt
```

Practical notes from using it:

- An engine built on one GPU architecture or TensorRT minor version generally will **not** load on
  another. Build on the deployment machine, keep `trtexec` invocations in your own infra repo.
- `--fp16` is the sweet spot for these three models; INT8 for the detector roughly halves latency
  budget at the cost of small-face recall, so re-check `thresholds.detection` after quantising.
- `reorder.py` matters: with the wrong output order the decoder silently produces boxes that look
  plausible in a debug draw but destroy tracking stability. Verify by drawing detections for a
  static scene — IDs should not flicker.

## Licenses and dataset caveats

- The weights stay under their upstream terms and are **not** covered by this project's MIT license.
- Face-recognition weights are typically trained on publicly scraped face datasets. Before using a
  model on real people, read the model license *and* the dataset terms, and confirm the intended use
  is permitted. Research-only licenses are common here and do not cover operational deployments.
- This repository deliberately ships no download URLs: the exact artifacts used in the original
  deployment are not publicly documented, and hard-coding a mirror would create a supply-chain
  dependency for anyone who clones this. Fetch weights from the original authors, record the checksum
  you used, and keep that record with your deployment.
- See [PRIVACY.md](PRIVACY.md) for what to consider before running recognition on identifiable people.
