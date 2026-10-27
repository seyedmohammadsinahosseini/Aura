# AGENT BRIEF — Head Detection + Pose Estimation + Face Recognition Pipeline

> **این سند دستورالعمل دقیق پیاده‌سازی است. هیچ تصمیم معماری از خودت نگیر.
> هر آنچه در اینجا نوشته شده باید دقیقاً پیاده‌سازی شود.**

---

## ۱. هدف کلی سیستم

سیستم باید بتواند در یک ویدیوی ۴K real-time، تمام افراد حاضر در تصویر را **ردیابی کند** — حتی اگر صورتشان دیده نشود. هر بار که صورت یک فرد در زاویه مناسب قرار گرفت، آن را شناسایی کن و این شناسه را به track آن فرد متصل کن. از این لحظه به بعد، آن فرد **شناخته‌شده** است حتی اگر پشتش به دوربین باشد.

---

## ۲. معماری کلی — روش C (Head Track + Conditional Face)

```
4K Frame
    │
    ▼
[Step 1] Head Detector (YOLOv10n — TensorRT FP16)
    │  → bbox سر تمام افراد در تصویر (همه زاویه‌ها)
    │
    ▼
[Step 2] ByteTrack Tracker
    │  → هر bbox یک track_id ثابت می‌گیرد
    │  → track_id تا زمانی که فرد در تصویر است پایدار می‌ماند
    │
    ▼
[Step 3] WHENet Head Pose Estimator (روی head crop)
    │  → سه زاویه برمی‌گرداند: yaw, pitch, roll
    │  → اگر سر پشت به دوربین باشد: yaw نزدیک ±180° برمی‌گرداند
    │  → این مدل نیاز به دیدن صورت ندارد — روی سر کار می‌کند
    │
    ▼
[Step 4] Yaw Gate (شرط)
    │  اگر abs(yaw) < 45° → ادامه به Step 5
    │  اگر abs(yaw) >= 45° → فقط track_id حفظ می‌شود، Step 5 skip می‌شود
    │
    ▼
[Step 5] Face Aligner (فقط اگر از Gate رد شد)
    │  → از head crop، یک tight face crop با ۵ landmark تراز می‌کند
    │  → از InsightFace face_align استفاده کن (نه یک face detector کامل)
    │
    ▼
[Step 6] Face Recognizer (ArcFace یا AdaFace — TensorRT FP16)
    │  → یک embedding 512-dim تولید می‌کند
    │  → این embedding با gallery مقایسه می‌شود
    │
    ▼
[Step 7] Identity Cache
    │  → اگر شناسایی شد: identity به track_id متصل می‌شود
    │  → از این لحظه، این track_id = identity_known
    │  → دیگر نیازی به تکرار Step 5 و 6 برای این track نیست
    │
    ▼
خروجی نهایی: {track_id, bbox, identity (اگر شناسایی شده), yaw}
```

---

## ۳. جزئیات هر مرحله

### Step 1 — Head Detector

**مدل:** YOLOv10n fine-tuned روی CrowdHuman یا SCUT-HEAD  
**فرمت:** TensorRT Engine، FP16، batch=1  
**ورودی:** frame resize شده به 640×640 (resize با CUDA، نه CPU)  
**خروجی:** لیست bbox های سر به فرمت `[x1, y1, x2, y2, confidence]`  
**هدف latency:** زیر 1.5ms روی RTX 3060 به بالا  

**نکته مهم:** این مدل **همه** سرها را در **همه زاویه‌ها** detect می‌کند — پشت، کنار، رو به جلو. فقط head detector است، نه face detector.

---

### Step 2 — ByteTrack

**کتابخانه:** `pip install bytetracker` یا از repo رسمی `ifzhang/ByteTrack`  
**ورودی:** خروجی head detector در هر فریم  
**خروجی:** همان bbox ها با `track_id` اضافه شده  

**تنظیمات مهم:**
```python
tracker = BYTETracker(
    track_thresh=0.5,     # آستانه confirm شدن track جدید
    track_buffer=30,      # چند فریم track بدون detection زنده بماند
    match_thresh=0.8,     # آستانه IoU برای match کردن
    frame_rate=30         # FPS ویدیو
)
```

**نکته:** `track_buffer=30` یعنی اگر فرد ۳۰ فریم از دید خارج شد، track حذف می‌شود. مقدار را بر اساس FPS تنظیم کن.

---

### Step 3 — WHENet Head Pose Estimator

**مدل:** WHENet  
**لینک:** `https://github.com/Ascend-Research/HeadPoseEstimation-WHENet`  
**چرا WHENet و نه Shaw-git:** WHENet روی **سر** کار می‌کند نه صورت. یعنی وقتی سر پشت به دوربین است هم زاویه برمی‌گرداند. Shaw-git فقط وقتی صورت دیده می‌شود کار می‌کند — برای این سیستم مناسب نیست.

**ورودی:** head crop از روی frame اصلی (نه resize شده)  
**خروجی:** `(yaw, pitch, roll)` به درجه  

**تفسیر خروجی:**
- `yaw ≈ 0°` → فرد رو به دوربین است
- `yaw ≈ ±45°` → پروفایل نیمه
- `yaw ≈ ±90°` → پروفایل کامل
- `yaw ≈ ±180°` → کاملاً پشت به دوربین

**مدل را به TensorRT تبدیل کن:**
```bash
# ابتدا به ONNX
python export_onnx.py --weights whenet.pth

# سپس به TRT engine
trtexec --onnx=whenet.onnx --saveEngine=whenet.engine --fp16 --workspace=2048
```

---

### Step 4 — Yaw Gate

```python
YAW_THRESHOLD = 45  # قابل تنظیم توسط کاربر

def should_run_face_recognition(yaw: float) -> bool:
    return abs(yaw) < YAW_THRESHOLD
```

**این مقدار باید قابل تنظیم باشد.** کاربر ممکن است بخواهد آن را به 35 یا 60 تغییر دهد.

---

### Step 5 — Face Aligner (نه Face Detector)

**ابزار:** `insightface` کتابخانه، فقط بخش `face_align`  
**هدف:** تبدیل head crop به یک tight face crop تراز شده (aligned)  
**چرا aligner و نه detector:** detector کامل (مثل SCRFD) وقت اضافه می‌برد. ما از قبل می‌دانیم سر کجاست — فقط نیاز داریم crop را برای recognizer آماده کنیم.

```python
from insightface.utils import face_align

# ۵ نقطه landmark را از یک مدل سبک مثل MobileNet-based predictor بگیر
# سپس:
aligned_face = face_align.norm_crop(head_crop, landmark=landmarks_5pt, image_size=112)
```

**اگر landmark predictor سبکی نداری:** از `insightface` مدل `buffalo_sc` استفاده کن که کوچک‌ترین مدل موجود در این کتابخانه است.

---

### Step 6 — Face Recognizer

**مدل:** AdaFace (MobileNet backbone) یا ArcFace (MobileNet)  
**لینک AdaFace:** `https://github.com/mk-minchul/AdaFace`  
**فرمت:** TensorRT FP16  
**ورودی:** تصویر 112×112 normalized  
**خروجی:** embedding vector با ابعاد 512  

**تبدیل به TRT:**
```bash
trtexec --onnx=adaface_mobilenet.onnx --saveEngine=adaface.engine --fp16 --workspace=1024
```

**مقایسه با gallery:**
```python
import numpy as np

def find_identity(embedding, gallery, threshold=0.4):
    """
    gallery = {person_name: embedding_vector}
    threshold: فاصله cosine — پایین‌تر = شبیه‌تر
    اگر کمترین فاصله از threshold بیشتر بود → ناشناس
    """
    min_dist = float('inf')
    identity = "unknown"
    for name, gallery_emb in gallery.items():
        dist = 1 - np.dot(embedding, gallery_emb)  # cosine distance
        if dist < min_dist:
            min_dist = dist
            if dist < threshold:
                identity = name
    return identity, min_dist
```

---

### Step 7 — Identity Cache

```python
class IdentityCache:
    def __init__(self):
        self.cache = {}
        # ساختار: {track_id: {"identity": str, "confidence": float, "confirmed_frame": int}}
    
    def is_identified(self, track_id: int) -> bool:
        return track_id in self.cache and self.cache[track_id]["identity"] != "unknown"
    
    def set_identity(self, track_id: int, identity: str, confidence: float, frame_idx: int):
        self.cache[track_id] = {
            "identity": identity,
            "confidence": confidence,
            "confirmed_frame": frame_idx
        }
    
    def get_identity(self, track_id: int) -> str:
        if track_id in self.cache:
            return self.cache[track_id]["identity"]
        return None
    
    def remove_lost_tracks(self, active_track_ids: list):
        """
        track های از دست رفته را از cache حذف کن
        هر فریم فراخوانی شود
        """
        lost = [tid for tid in self.cache if tid not in active_track_ids]
        for tid in lost:
            del self.cache[tid]
```

**منطق اصلی:** اگر یک track_id یک‌بار شناسایی شد، دیگر Step 5 و 6 برای آن اجرا نمی‌شوند. این مهم‌ترین بهینه‌سازی CPU/GPU است.

---

## ۴. Loop اصلی — کد کامل ساختار

```python
def process_frame(frame, frame_idx, tracker, pose_estimator, face_aligner, 
                  face_recognizer, identity_cache, gallery):
    
    # --- Step 1: Head Detection ---
    frame_resized = cuda_resize(frame, (640, 640))
    head_detections = head_detector.infer(frame_resized)  # TRT inference
    
    # --- Step 2: Tracking ---
    tracks = tracker.update(head_detections, frame.shape[:2])
    active_ids = [t.track_id for t in tracks]
    identity_cache.remove_lost_tracks(active_ids)
    
    results = []
    
    for track in tracks:
        track_id = track.track_id
        bbox = track.tlbr  # [x1, y1, x2, y2] در مختصات frame اصلی
        
        # --- Step 3: Head Pose ---
        head_crop = crop_from_frame(frame, bbox, padding=0.1)
        yaw, pitch, roll = pose_estimator.infer(head_crop)
        
        # اگر track قبلاً شناسایی شده → skip recognition
        if identity_cache.is_identified(track_id):
            identity = identity_cache.get_identity(track_id)
        
        # --- Step 4: Yaw Gate ---
        elif should_run_face_recognition(yaw):
            
            # --- Step 5: Face Align ---
            aligned_face = face_aligner.align(head_crop)
            
            if aligned_face is not None:
                # --- Step 6: Recognition ---
                embedding = face_recognizer.infer(aligned_face)
                identity, confidence = find_identity(embedding, gallery)
                
                # --- Step 7: Cache ---
                identity_cache.set_identity(track_id, identity, confidence, frame_idx)
            else:
                identity = "unknown"
        else:
            identity = None  # خارج از زاویه — هنوز شناسایی نشده
        
        results.append({
            "track_id": track_id,
            "bbox": bbox,
            "yaw": yaw,
            "identity": identity
        })
    
    return results
```

---

## ۵. ساختار پروژه

```
head_pipeline/
├── models/
│   ├── head_detector.engine      ← TRT engine مدل head detection
│   ├── whenet.engine             ← TRT engine مدل pose estimation
│   └── adaface.engine            ← TRT engine مدل face recognition
│
├── gallery/
│   └── embeddings.pkl            ← فایل pickle از {name: embedding_array}
│
├── src/
│   ├── head_detector.py          ← wrapper برای head_detector.engine
│   ├── pose_estimator.py         ← wrapper برای whenet.engine
│   ├── face_aligner.py           ← InsightFace align utility
│   ├── face_recognizer.py        ← wrapper برای adaface.engine
│   ├── tracker.py                ← ByteTrack wrapper
│   ├── identity_cache.py         ← کلاس IdentityCache
│   └── pipeline.py               ← loop اصلی process_frame
│
├── tools/
│   ├── build_gallery.py          ← برای ساخت gallery از عکس‌های ثبت‌شده
│   ├── export_onnx.py            ← تبدیل .pt به .onnx
│   └── convert_trt.sh            ← اسکریپت تبدیل onnx → engine
│
├── config.yaml                   ← تمام پارامترها اینجا
└── main.py                       ← نقطه ورود برنامه
```

---

## ۶. فایل config.yaml

```yaml
# تمام پارامترهای قابل تنظیم اینجا هستند — هیچ magic number در کد نباشد

head_detector:
  engine_path: "models/head_detector.engine"
  input_size: [640, 640]
  confidence_threshold: 0.5
  nms_threshold: 0.45

tracker:
  track_thresh: 0.5
  track_buffer: 30       # فریم — چقدر یک track بدون detection زنده بماند
  match_thresh: 0.8
  frame_rate: 30

pose_estimator:
  engine_path: "models/whenet.engine"
  input_size: [224, 224]

yaw_gate:
  threshold_degrees: 45  # اگر abs(yaw) از این کمتر بود → recognition اجرا شود

face_aligner:
  output_size: 112

face_recognizer:
  engine_path: "models/adaface.engine"
  embedding_dim: 512
  similarity_threshold: 0.4  # پایین‌تر = سخت‌گیرانه‌تر

gallery:
  path: "gallery/embeddings.pkl"
```

---

## ۷. نکات ضروری که نباید نادیده گرفته شوند

### ۷.۱ تبدیل مختصات
وقتی frame را برای head_detector به 640×640 resize می‌کنی، خروجی bbox ها در مختصات 640×640 هستند. قبل از crop کردن برای pose estimator، باید bbox را به مختصات frame اصلی 4K تبدیل کنی:

```python
scale_x = original_width / 640
scale_y = original_height / 640
bbox_original = bbox * [scale_x, scale_y, scale_x, scale_y]
```

### ۷.۲ Padding برای head crop
وقتی head crop می‌گیری، ۱۰٪ padding اضافه کن تا مدل pose بهتر کار کند:

```python
def crop_with_padding(frame, bbox, padding=0.1):
    x1, y1, x2, y2 = bbox
    w, h = x2 - x1, y2 - y1
    x1 = max(0, x1 - w * padding)
    y1 = max(0, y1 - h * padding)
    x2 = min(frame.shape[1], x2 + w * padding)
    y2 = min(frame.shape[0], y2 + h * padding)
    return frame[int(y1):int(y2), int(x1):int(x2)]
```

### ۷.۳ Thread safety برای Identity Cache
اگر pipeline چند thread دارد، IdentityCache باید با lock محافظت شود:

```python
import threading
self.lock = threading.Lock()

def set_identity(self, ...):
    with self.lock:
        self.cache[track_id] = ...
```

### ۷.۴ Gallery باید از قبل ساخته شده باشد
قبل از اجرای pipeline، باید `build_gallery.py` اجرا شود. این ابزار عکس‌های هر فرد را می‌گیرد، embedding می‌سازد و در `gallery/embeddings.pkl` ذخیره می‌کند. Pipeline خودش هیچ آموزشی انجام نمی‌دهد.

### ۷.۵ هر سه engine باید روی همان GPU compile شده باشند
فایل‌های `.engine` را نمی‌توان بین GPU های مختلف جابجا کرد. اگر GPU عوض شد، باید دوباره از ONNX به engine تبدیل شود.

---

## ۸. وابستگی‌های پروژه

```txt
# requirements.txt
tensorrt>=8.6
pycuda>=2022.1
torch>=2.0
ultralytics>=8.0        # برای head detector
bytetracker             # tracker
insightface>=0.7        # برای face_align
onnxruntime-gpu         # برای مرحله تست قبل از TRT
numpy>=1.24
opencv-python>=4.8
pyyaml
```

---

## ۹. ترتیب پیاده‌سازی (برای agent)

این ترتیب را دقیقاً رعایت کن. هر مرحله را کامل کن قبل از رفتن به بعدی:

1. **ساختار پوشه‌ها را بساز** — طبق بخش ۵
2. **`config.yaml` را بساز** — طبق بخش ۶
3. **`identity_cache.py` را بساز** — ساده‌ترین module، بدون dependency
4. **`tracker.py` را بساز** — ByteTrack wrapper
5. **`head_detector.py` را بساز** — TRT engine loader + preprocess/postprocess
6. **`pose_estimator.py` را بساز** — WHENet TRT wrapper
7. **`face_aligner.py` را بساز** — InsightFace align utility
8. **`face_recognizer.py` را بساز** — AdaFace TRT wrapper + gallery compare
9. **`pipeline.py` را بساز** — همه module ها را با هم ترکیب کن، طبق بخش ۴
10. **`tools/build_gallery.py` را بساز** — ابزار ساخت gallery
11. **`tools/convert_trt.sh` را بساز** — اسکریپت تبدیل ONNX → engine
12. **`main.py` را بساز** — نقطه ورود، config را بخوان، pipeline را اجرا کن

---

## ۱۰. چیزهایی که نباید اضافه کنی

- **هیچ مدل اضافه‌ای**: نه body detector، نه face detector کامل (SCRFD/MTCNN)، نه depth estimation
- **هیچ منطق re-ID پیچیده‌ای**: ByteTrack خودش re-association انجام می‌دهد
- **هیچ database یا server**: فقط فایل‌های local
- **هیچ GUI**: فقط output JSON یا annotated frame با OpenCV
- **هیچ multi-GPU**: فقط single GPU
- **هیچ منطق training یا fine-tune**: pipeline فقط inference است

---

*این سند کامل است. پیاده‌سازی را از بخش ۹ شروع کن.*
