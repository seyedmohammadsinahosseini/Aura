# Model Weights

This project depends on several deep-learning models for face detection, face
recognition, and head detection. **The model weights are NOT included in this
repository.** They must be downloaded and converted by the developer before the
application can run.

The application looks up the model files through the `models` section of
`core/config/config.json`. Paths are resolved relative to the project root or
the `core/config` directory (see `core/config/Config.cpp`), so files placed in
`core/models/` are picked up by the default configuration.

## Required models

| Purpose | Config keys | Expected files (relative to `core/`) | Format |
|---------|-------------|--------------------------------------|--------|
| Face detection | `detection_onnx`, `detection_engine` | `models/det_500m_FP32.onnx`, `models/det_500m_FP32.engine` | ONNX + TensorRT engine |
| Face recognition (embedding) | `recognition_onnx`, `recognition_engine` | `models/webface_r50.onnx`, `models/webface_r50.engine` | ONNX + TensorRT engine |
| Head detection | `head_onnx`, `head_engine` | `models/headdetection_v12.onnx`, `models/headdetection_v12.engine` | ONNX + TensorRT engine |
| Face detection (NVIDIA TAO) | — (reference / conversion) | `models/facedetect/model.etlt` | NVIDIA TAO (ETLT) |

Supporting files that ship with this repository:

* `models/reorder.py` — helper that reorders the output tensors of
  `det_500m_FP32.onnx` (multi-output detection heads) before TensorRT
  conversion.
* `models/facedetect/int8_calibration.txt` — calibration data used for
  INT8 TensorRT conversion of the TAO face detector.
* `models/openvino/*.xml.hash` — hashes of OpenVINO XML model files used
  during model validation/conversion.

## Where the application expects them

With the default `core/config/config.json`, models must be present at:

```
core/models/det_500m_FP32.onnx
core/models/det_500m_FP32.engine
core/models/webface_r50.onnx
core/models/webface_r50.engine
core/models/headdetection_v12.onnx
core/models/headdetection_v12.engine
```

The engine (`.engine`) files are used when available (prefered for speed);
the ONNX files are used for conversion or as a fallback.

## How to obtain them

> **Note:** download sources are intentionally left for **manual configuration**.
> No download URLs are hard-coded here or in the source code, because the exact
> model artifacts used by this deployment are not publicly documented by the
> project. Obtain the models from their respective original authors/distributors
> and respect their licenses.

General guidance:

1. **Face detection (`det_500m_FP32`)** — obtain the ONNX model from the
   detector's official release (this is a ResNet-500-style detection model with
   multiple output heads; the outputs are reordered with `models/reorder.py`).
2. **Face recognition (`webface_r50`)** — obtain the ResNet-50 face-recognition
   (embedding) model trained on the WebFace dataset from its official
   distribution.
3. **Head detection (`headdetection_v12`)** — obtain the head detector ONNX
   model from its original source.
4. **NVIDIA TAO face detector (`facedetect/model.etlt`)** — obtained from the
   NVIDIA TAO Model Zoo distribution.

### Converting to TensorRT engines

The application runs on TensorRT and loads the `.engine` files directly. After
obtaining the ONNX models, build the engines with the TensorRT `trtexec`
utility, for example:

```bash
trtexec --onnx=det_500m_FP32_reordered.onnx \
        --saveEngine=det_500m_FP32.engine \
        --fp16

trtexec --onnx=webface_r50.onnx \
        --saveEngine=webface_r50.engine \
        --fp16

trtexec --onnx=headdetection_v12.onnx \
        --saveEngine=headdetection_v12.engine \
        --fp16
```

`model.etlt` (NVIDIA TAO) is converted with the NVIDIA `tao-converter` tool to
a TensorRT engine; the INT8 calibration data in `facedetect/int8_calibration.txt`
can be used for INT8 quantization.

## Licenses

Model weights are third-party artifacts and are **not covered by this
project's license**. Before using a model you must verify that your intended use
is permitted by its license (including any dataset licenses, e.g. for face
recognition models trained on publicly available face datasets). This project is
provided without the model weights for this reason.

The `.hash` files and conversion scripts in this directory are provided for
convenience only and do not change the licensing of the models themselves.
