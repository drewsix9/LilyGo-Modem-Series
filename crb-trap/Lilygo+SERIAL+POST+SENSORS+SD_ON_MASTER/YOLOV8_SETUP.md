# YOLOv8 Integration Setup Guide for Raspberry Pi

## 🚀 Quick Start

### Step 1: Install Dependencies on Raspberry Pi

```bash
# Update system packages
sudo apt-get update && sudo apt-get upgrade -y

# Install Python dependencies for YOLOv8 + OpenCV on ARM
pip install --upgrade pip setuptools wheel
pip install ultralytics opencv-python-headless numpy flask supabase
```

### Step 2: Export Your Trained Model to NCNN Format

**On your training machine** (with the trained `.pt` model):

```bash
# Install YOLOv8 (if not already installed)
pip install ultralytics

# Export model to NCNN format (optimized for Raspberry Pi)
# Replace 'path/to/best.pt' with your actual model
python -c "from ultralytics import YOLO; model = YOLO('path/to/best.pt'); model.export(format='ncnn', imgsz=640)"
```

This will generate NCNN model files in the same directory as your `.pt` model:

- `best.ncnn.param` (model parameters)
- `best.ncnn.bin` (model weights)

### Step 3: Transfer NCNN Model to Raspberry Pi

```bash
# From your machine, copy the NCNN model to the Pi
scp best.ncnn.* pi@raspberrypi.local:/path/to/models/
```

### Step 4: Configure Model Path on Raspberry Pi

Set the environment variable before running Flask:

```bash
# Option A: Set in current session
export YOLO_MODEL_PATH="/path/to/models/best.ncnn.param"
python api.py

# Option B: Add to ~/.bashrc for persistence
echo 'export YOLO_MODEL_PATH="/path/to/models/best.ncnn.param"' >> ~/.bashrc
source ~/.bashrc
python api.py

# Option C: Create a .env file in the app directory
echo 'YOLO_MODEL_PATH=/path/to/models/best.ncnn.param' > .env
# And load it in your Flask app with: python-dotenv package
```

### Step 5: Run the Flask API

```bash
# Option A: Direct
python api.py

# Option B: With Gunicorn (production)
pip install gunicorn
gunicorn -w 1 -b 0.0.0.0:5000 api:app

# Option C: As a systemd service (persistent)
sudo nano /etc/systemd/system/crb-api.service
```

## 🔧 Production Systemd Service Setup

Create `/etc/systemd/system/crb-api.service`:

```ini
[Unit]
Description=CRB Flask API with YOLOv8
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/path/to/Lilygo+SERIAL+POST+SENSORS+SD_ON_MASTER
Environment="YOLO_MODEL_PATH=/path/to/models/best.ncnn.param"
ExecStart=/usr/bin/python3 -m gunicorn -w 1 -b 0.0.0.0:5000 api:app
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable crb-api
sudo systemctl start crb-api
sudo systemctl status crb-api
```

## 📊 Key Changes in Updated API

### 1. **Global Model Load**

- Model is loaded once at startup, not per-request
- Saves memory and latency

### 2. **`process_inference()` Function**

Comprehensive error handling with logging:

- Image decoding validation
- Inference error recovery
- Box parsing with confidence tracking
- Automatic image annotation and encoding
- Returns: `(counts, annotated_bytes, inference_time_ms, error_msg)`

### 3. **Conditional Upload Logic**

Only uploads and creates DB records if beetles are detected:

- ✓ Annotated image to Ramdisk
- ✓ Annotated image to Supabase Storage
- ✓ `image_uploads` record created
- ✓ `detection_results` record with counts and timing
- ✓ Platform action: `"flip"` if detected, `"stay"` if not

### 4. **Always-On Telemetry**

Battery and solar voltage synced regardless of detection:

```python
# Always runs even if no beetles detected
supabase.table("traps").update(trap_update_data)
```

## 🎯 API Response Format

**When beetles detected** (200 OK):

```json
{
  "success": true,
  "detected": true,
  "counts": {
    "male": 2,
    "female": 1,
    "unknown": 0
  },
  "image_upload_id": "45abc123...",
  "inference_time_ms": 324,
  "inference_error": null,
  "platform_action": "flip",
  "message": "Positive detection recorded and uploaded"
}
```

**When no beetles detected** (200 OK):

```json
{
  "success": true,
  "detected": false,
  "counts": {
    "male": 0,
    "female": 0,
    "unknown": 0
  },
  "image_upload_id": null,
  "inference_time_ms": 287,
  "inference_error": null,
  "platform_action": "stay",
  "message": "No beetles detected"
}
```

**On error** (500):

```json
{
  "success": false,
  "error": "Upload failed: ...",
  "inference_result": {
    "detected": true,
    "counts": {...},
    "inference_time_ms": 312
  }
}
```

## 📈 Performance Optimization Tips

| Item         | Recommendation                                           |
| ------------ | -------------------------------------------------------- |
| Model        | Use **YOLOv8n** (Nano) for fastest inference             |
| Confidence   | Set to 0.5 (adjust in `process_inference()`)             |
| JPEG Quality | 90 (adjust in `cv2.imencode()`)                          |
| Async        | Consider Celery for background processing if >1s latency |

## 🐛 Debugging

Check logs in real-time:

```bash
# With gunicorn
sudo journalctl -u crb-api -f

# Direct Flask
python api.py  # Logs printed to console
```

Look for these tags in logs:

- `[INFERENCE]` - Start of processing
- `[INFERENCE RESULT]` - Detection counts
- `[UPLOAD]` - Uploading positive detections
- `[NO DETECTION]` - No beetles found
- `[TELEMETRY]` - Voltage sync
- `[ERROR]` - Any failures

## ✅ Checklist

- [ ] NCNN model exported and transferred to Pi
- [ ] `YOLO_MODEL_PATH` environment variable set
- [ ] Dependencies installed on Pi
- [ ] Flask app runs without model errors
- [ ] Test with a sample image: `curl -X POST --data-binary @test_image.jpg "http://raspberrypi.local:5000/upload-trap-image?trap_id=trap001&..."`
- [ ] Supabase credentials verified
- [ ] Voltage telemetry working
- [ ] Platform flipping on positive detections

---

**Last updated**: April 2026
