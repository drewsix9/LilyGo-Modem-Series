"""
CRB Trap Image Processing Backend
Handles beetle detection, classification, and trap control
"""
from flask import Flask, request, jsonify
import zlib
import os
import logging
import time
from datetime import datetime
from supabase import create_client, Client

# YOLOv8 & Image Processing
import cv2
import numpy as np
from ultralytics import YOLO

app = Flask(__name__)

# --- LOGGING CONFIGURATION ---
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# --- CONFIGURATION ---
SAVE_DIR = "/mnt/ramdisk/debug_images"
os.makedirs(SAVE_DIR, exist_ok=True)

# Credentials - Update with your actual details
SUPABASE_URL = "https://estunmbmwzxactvrqjgo.supabase.co"
SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImVzdHVubWJtd3p4YWN0dnJxamdvIiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc3MjgwODAxMiwiZXhwIjoyMDg4Mzg0MDEyfQ.GIQZMPlHHozJV3JqeIhJLfSM_OWjPao5B8J-zjEiLi8"
supabase: Client = create_client(SUPABASE_URL, SUPABASE_KEY)

# --- YOLOV8 MODEL INITIALIZATION (NCNN FORMAT) ---
# Load the custom trained model in NCNN format for Raspberry Pi optimization
# Export your model to NCNN: yolo export model=path/to/best.pt format=ncnn imgsz=640
MODEL_PATH = os.getenv(
    "YOLO_MODEL_PATH", "~/crb-backend/best_ncnn_model")
MODEL_PATH = os.path.expanduser(MODEL_PATH)

# Validate NCNN model files exist


def validate_ncnn_model(model_path):
    """Validate that both .param and .bin files exist for NCNN model"""
    # If model_path is a directory, look for model.ncnn.param and model.ncnn.bin inside
    if os.path.isdir(model_path):
        param_path = os.path.join(model_path, "model.ncnn.param")
        bin_path = os.path.join(model_path, "model.ncnn.bin")
    else:
        # If it's a file path, derive bin from param
        if model_path.endswith('.param'):
            param_path = model_path
            bin_path = model_path.replace('.param', '.bin')
        else:
            param_path = model_path + '.param'
            bin_path = model_path + '.bin'

    param_exists = os.path.exists(param_path)
    bin_exists = os.path.exists(bin_path)

    if not param_exists:
        logger.error(f"NCNN .param file not found: {param_path}")
        return False

    if not bin_exists:
        logger.error(f"NCNN .bin file not found: {bin_path}")
        logger.error(
            "NCNN models require both .param and .bin files in the same directory")
        return False

    return True


try:
    if not validate_ncnn_model(MODEL_PATH):
        logger.warning("Model validation failed, model will not be loaded")
        model = None
    else:
        logger.info(f"[INIT] Loading model from: {MODEL_PATH}")
        model = YOLO(MODEL_PATH, task='detect')
        logger.info(
            f"[OK] YOLOv8 model loaded successfully from: {MODEL_PATH}")
except Exception as e:
    logger.error(f"[FAIL] Failed to load YOLOv8 model at startup: {e}")
    logger.error(
        "Ensure model files exist and are not corrupted. Model will be loaded on first inference attempt.")
    model = None


def get_model():
    """Get model instance, loading on first use if needed (lazy-load fallback)"""
    global model
    if model is None:
        logger.warning(
            f"[LAZY-LOAD] Model not loaded at startup, attempting to load now...")
        try:
            model = YOLO(MODEL_PATH, task='detect')
            logger.info(f"[OK] Model lazy-loaded successfully")
        except Exception as e:
            logger.error(f"[FAIL] Failed to lazy-load model: {e}")
            return None
    return model


# --- INFERENCE HELPER FUNCTION WITH ERROR HANDLING ---
def process_inference(image_bytes):
    """
    Process image inference using YOLOv8 NCNN model.

    Returns:
        tuple: (counts_dict, annotated_bytes, inference_time_ms, error_msg)
               - counts_dict: {"male": int, "female": int, "unknown": int}
               - annotated_bytes: JPEG bytes of annotated image (or None if error)
               - inference_time_ms: Inference duration in milliseconds
               - error_msg: None if successful, error string if failed
    """
    try:
        current_model = get_model()
        if current_model is None:
            logger.error("Model not loaded and lazy-load failed")
            return None, None, 0, "Model not available"

        # Step 1: Decode image bytes to numpy array
        try:
            logger.debug(f"[DECODE] Received {len(image_bytes)} bytes")
            nparr = np.frombuffer(image_bytes, np.uint8)
            img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

            if img is None:
                raise ValueError(
                    "Failed to decode image - invalid or corrupted format")
            logger.info(f"[OK] Image decoded: {img.shape}")
        except Exception as e:
            logger.error(f"[FAIL] Image decode error: {e}")
            return None, None, 0, f"Image decode failed: {str(e)}"

        # Step 2: Run inference
        try:
            logger.debug(
                f"[INFERENCE] Starting inference on image shape: {img.shape}")
            inference_start = time.time()
            results = current_model(img, conf=0.5, verbose=False)[0]
            inference_time_ms = int((time.time() - inference_start) * 1000)
            logger.info(f"[OK] Inference complete: {inference_time_ms}ms")
        except Exception as e:
            logger.error(f"[FAIL] Inference error: {e}")
            logger.error(
                f"[DEBUG] Model type: {type(current_model)}, Model path: {MODEL_PATH}")
            return None, None, 0, f"Inference failed: {str(e)}"

        # Step 3: Count detections by class
        try:
            counts = {"male": 0, "female": 0, "unknown": 0}

            # results.names maps class index to label (e.g., {0: 'male', 1: 'female', 2: 'unknown'})
            if results.boxes is not None and len(results.boxes) > 0:
                logger.debug(f"[BOXES] Found {len(results.boxes)} detections")
                for box in results.boxes:
                    try:
                        cls_id = int(box.cls[0])
                        confidence = float(box.conf[0])
                        label = results.names[cls_id].lower(
                        ) if cls_id in results.names else "unknown"

                        # Only count if in our expected classes
                        if label in counts:
                            counts[label] += 1
                            logger.debug(
                                f"  Detected: {label} (conf: {confidence:.2f})")
                    except Exception as box_err:
                        logger.warning(f"  Warning parsing box: {box_err}")
                        continue

            total_beetles = sum(counts.values())
            logger.info(
                f"[OK] Detection counts: Male={counts['male']}, Female={counts['female']}, Unknown={counts['unknown']}, Total={total_beetles}")
        except Exception as e:
            logger.error(f"[FAIL] Box parsing error: {e}")
            return None, None, inference_time_ms, f"Failed to parse detections: {str(e)}"

        # Step 4: Annotate image if beetles found
        try:
            logger.debug("[ANNOTATE] Plotting results...")
            annotated_img = results.plot()

            # Encode annotated image back to bytes
            _, buffer = cv2.imencode('.jpg', annotated_img, [
                                     cv2.IMWRITE_JPEG_QUALITY, 90])
            if buffer is None:
                raise ValueError("Failed to encode annotated image")

            annotated_bytes = buffer.tobytes()
            logger.info(
                f"[OK] Image annotated and encoded: {len(annotated_bytes)} bytes")
        except Exception as e:
            logger.error(f"[FAIL] Image annotation error: {e}")
            return counts, None, inference_time_ms, f"Annotation failed: {str(e)}"

        # Successful inference
        return counts, annotated_bytes, inference_time_ms, None

    except Exception as e:
        logger.error(f"[FAIL] Unexpected error in process_inference: {e}")
        import traceback
        logger.error(traceback.format_exc())
        return None, None, 0, f"Unexpected error: {str(e)}"


@app.route('/health', methods=['GET'])
def health_check():
    return jsonify({
        "status": "online",
        "service": "crb-backend-ingest",
        "timestamp": datetime.now().isoformat()
    }), 200

# 1. ORIGINAL ECHO TEST (For UART/Modem Debugging)


@app.route('/echo-test', methods=['POST'])
def echo_test():
    trap_id = request.args.get(
        'trap_id') or request.headers.get('x-trap-id', 'unknown')
    expected_crc = request.args.get(
        'image_crc32') or request.headers.get('x-image-crc32')

    image_bytes = request.get_data()
    received_size = len(image_bytes)

    if received_size == 0:
        return jsonify({"success": False, "error": "Empty body"}), 400

    computed_crc_num = zlib.crc32(image_bytes) & 0xFFFFFFFF
    computed_crc_hex = format(computed_crc_num, '08X')

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    filename = f"{trap_id}-{timestamp}-computed-{computed_crc_hex}.jpg"
    filepath = os.path.join(SAVE_DIR, filename)

    with open(filepath, "wb") as f:
        f.write(image_bytes)

    print(f"\n[{datetime.now().isoformat()}] --- Echo Test ---")
    print(f"Trap ID: {trap_id} | Size: {received_size} bytes")
    print(
        f"Match: {computed_crc_hex == (expected_crc.upper() if expected_crc else '')}")

    return jsonify({
        "success": True,
        "received_size": received_size,
        "computed_crc32": computed_crc_hex,
        "expected_crc32": expected_crc,
        "saved_as": filename
    })

# 2. NEW SUPABASE UPLOAD ROUTE (The "Thesis" Production Path)


@app.route('/upload-trap-image', methods=['POST'])
def upload_trap_image():
    try:
        # --- 1. METADATA PARSING ---
        trap_id = request.args.get('trap_id')
        captured_at = datetime.utcnow().isoformat()
        expected_crc = request.args.get('image_crc32')
        image_filename = request.args.get('filename')

        # Telemetry
        gps_lat = request.args.get('gps_lat')
        gps_lon = request.args.get('gps_lon')
        ldr_value = request.args.get('ldr_value')
        battery_voltage = request.args.get('battery_voltage')
        solar_voltage = request.args.get('solar_voltage')
        is_fallen = request.args.get('is_fallen', 'false').lower() == 'true'

        # --- 2. VALIDATION ---
        if not all([trap_id, expected_crc, image_filename]):
            return jsonify({
                "success": False,
                "error": "Missing required parameters"
            }), 400

        image_bytes = request.get_data()
        if not image_bytes:
            return jsonify({"success": False, "error": "Empty request body"}), 400

        # --- 3. CRC32 CALCULATION ---
        computed_crc_num = zlib.crc32(image_bytes) & 0xFFFFFFFF
        computed_crc_hex = format(computed_crc_num, '08X').upper()
        expected_crc_hex = expected_crc.upper().replace("0X", "")

        if computed_crc_hex != expected_crc_hex:
            logger.warning(
                f"[CRC WARNING] Mismatch! Expected: {expected_crc_hex}, Computed: {computed_crc_hex}")

        # --- 4. STEP 1: VALIDATE TRAP EXISTS ---
        trap_check = supabase.table("traps").select("id").eq(
            "trap_id", trap_id).maybe_single().execute()
        if not trap_check.data:
            return jsonify({"success": False, "error": f"Trap not found: {trap_id}"}), 404

        # --- 5. STEP 2: RUN ML INFERENCE ---
        logger.info(
            f"[INFERENCE] Starting YOLOv8 inference for trap {trap_id}...")
        counts, annotated_bytes, inference_time_ms, inference_error = process_inference(
            image_bytes)

        total_beetles = sum(counts.values()) if counts else 0
        image_upload_id = None

        logger.info(f"[INFERENCE RESULT] Males: {counts.get('male', 0) if counts else 'N/A'}, "
                    f"Females: {counts.get('female', 0) if counts else 'N/A'}, "
                    f"Unknown: {counts.get('unknown', 0) if counts else 'N/A'}, "
                    f"Total: {total_beetles}, "
                    f"Time: {inference_time_ms}ms")

        # --- 5A. ALWAYS SAVE RAW IMAGE FOR DEBUGGING ---
        try:
            trap_ram_dir = os.path.join(SAVE_DIR, trap_id)
            os.makedirs(trap_ram_dir, exist_ok=True)
            raw_filepath = os.path.join(
                trap_ram_dir, f"raw-{image_filename}")
            with open(raw_filepath, "wb") as f:
                f.write(image_bytes)
            logger.info(
                f"[RAMDISK] Raw image saved for debugging: {raw_filepath}")
        except Exception as e:
            logger.error(f"[RAMDISK ERROR] Failed to save raw image: {e}")

        # --- 6. CONDITIONAL: ONLY UPLOAD IF BEETLES DETECTED ---
        if total_beetles > 0 and annotated_bytes and not inference_error:
            logger.info(
                f"[UPLOAD] Beetle(s) detected! Uploading annotated image...")

            try:
                # 6A. SAVE ANNOTATED IMAGE TO RAMDISK (Optional for debug)
                trap_ram_dir = os.path.join(SAVE_DIR, trap_id)
                os.makedirs(trap_ram_dir, exist_ok=True)
                ram_filepath = os.path.join(
                    trap_ram_dir, f"annotated-{image_filename}")
                with open(ram_filepath, "wb") as f:
                    f.write(annotated_bytes)
                logger.info(f"[RAMDISK] Annotated image saved: {ram_filepath}")

                # 6B. UPLOAD ANNOTATED IMAGE TO SUPABASE STORAGE
                image_path = f"{trap_id}/annotated-{image_filename}"
                supabase.storage.from_("trap-images").upload(
                    path=image_path,
                    file=annotated_bytes,
                    file_options={
                        "content-type": "image/jpeg", "upsert": "true"}
                )
                logger.info(f"[STORAGE] Uploaded to: {image_path}")

                # 6C. INSERT IMAGE_UPLOADS RECORD
                upload_data = {
                    "trap_id": trap_id,
                    "captured_at": captured_at,
                    "gps_lat": float(gps_lat) if gps_lat else None,
                    "gps_lon": float(gps_lon) if gps_lon else None,
                    "ldr_value": int(float(ldr_value)) if ldr_value else None,
                    "is_fallen": is_fallen,
                    "battery_voltage": int(battery_voltage) if battery_voltage else None,
                    "solar_voltage": int(solar_voltage) if solar_voltage else None,
                    "image_path": image_path,
                    "image_filename": f"annotated-{image_filename}",
                    "image_size_bytes": len(annotated_bytes),
                    "content_type": "image/jpeg",
                    "upload_status": "uploaded"
                }
                db_res = supabase.table("image_uploads").insert(
                    upload_data).execute()
                if not db_res.data:
                    raise Exception("Failed to insert image_uploads record")

                image_upload_id = db_res.data[0]['id']
                logger.info(
                    f"[DATABASE] image_uploads record created: id={image_upload_id}")

                # 6D. INSERT DETECTION_RESULTS RECORD
                detection_data = {
                    "image_upload_id": image_upload_id,
                    "beetle_count": total_beetles,
                    "male_count": counts.get('male', 0),
                    "female_count": counts.get('female', 0),
                    "unknown_count": counts.get('unknown', 0),
                    "inference_time_ms": inference_time_ms,
                    "model_name": "YOLOv8-NCNN-Custom",
                    "remarks": "Processed on-device (Raspberry Pi)"
                }
                detection_res = supabase.table(
                    "detection_results").insert(detection_data).execute()
                logger.info(f"[DATABASE] detection_results record created")

            except Exception as e:
                logger.error(
                    f"[UPLOAD ERROR] Failed to process positive detection: {e}")
                return jsonify({
                    "success": False,
                    "error": f"Upload failed: {str(e)}",
                    "inference_result": {
                        "detected": True,
                        "counts": counts if counts else {},
                        "inference_time_ms": inference_time_ms
                    }
                }), 500

        elif total_beetles == 0:
            logger.info(
                f"[NO DETECTION] No beetles detected. Skipping upload and DB insertion.")
        else:
            logger.warning(f"[INFERENCE ERROR] {inference_error}")

        # --- 7. ALWAYS UPDATE TRAPS TABLE WITH VOLTAGE TELEMETRY & GPS ---
        try:
            trap_update_data = {
                "battery_voltage": float(battery_voltage) if battery_voltage else None,
                "solar_voltage": float(solar_voltage) if solar_voltage else None,
                "latitude": float(gps_lat) if gps_lat else None,
                "longitude": float(gps_lon) if gps_lon else None,
                "last_voltage_update": datetime.utcnow().isoformat()
            }

            # If trap is fallen, mark as inactive
            if is_fallen:
                trap_update_data["status"] = "inactive"
                logger.info(
                    f"[STATUS UPDATE] Trap {trap_id} marked as INACTIVE (fallen detected)")

            traps_update = supabase.table("traps").update(
                trap_update_data).eq("trap_id", trap_id).execute()
            logger.info(
                f"[TELEMETRY] Voltage & GPS data synced for trap {trap_id} (lat={gps_lat}, lon={gps_lon})")
        except Exception as e:
            logger.error(
                f"[TELEMETRY ERROR] Failed to update traps table: {e}")

        # --- 8. DETERMINE SERVO ACTION BASED ON BEETLE COUNTS ---
        servo_action = "no_action"  # Default: no beetles detected
        servo_angle = 90  # Neutral position

        if total_beetles > 0 and counts:
            male_count = counts.get('male', 0)
            female_count = counts.get('female', 0)

            if male_count > female_count:
                servo_action = "servo_male"
                servo_angle = 45
                logger.info(
                    f"[SERVO] Male majority ({male_count} male, {female_count} female) -> 45 degrees")
            elif female_count > male_count:
                servo_action = "servo_female"
                servo_angle = 135
                logger.info(
                    f"[SERVO] Female majority ({female_count} female, {male_count} male) -> 135 degrees")
            else:
                # Tie or both zero - default to male
                servo_action = "servo_male"
                servo_angle = 45
                logger.info(
                    f"[SERVO] Tie or equal counts -> default to male at 45 degrees")

        # --- 9. RESET SERVO TO NEUTRAL BEFORE SLEEP ---
        logger.info(
            f"[SERVO] Returning servo to neutral position (90 degrees) before sleep")

        # --- 10. FINAL JSON RESPONSE ---
        return jsonify({
            "success": True,
            "detected": total_beetles > 0,
            "counts": counts if counts else {"male": 0, "female": 0, "unknown": 0},
            "image_upload_id": image_upload_id,
            "inference_time_ms": inference_time_ms,
            "inference_error": inference_error,
            "servo_action": servo_action,
            "servo_angle": servo_angle,
            "message": "Positive detection recorded and uploaded" if total_beetles > 0 else "No beetles detected"
        }), 200

    except Exception as e:
        logger.error(f"[CRITICAL ERROR] Unexpected error: {e}")
        return jsonify({"success": False, "error": str(e)}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
