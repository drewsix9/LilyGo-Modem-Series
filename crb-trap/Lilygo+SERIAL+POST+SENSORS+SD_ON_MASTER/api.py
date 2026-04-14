from flask import Flask, request, jsonify
import zlib
import os
from datetime import datetime
from supabase import create_client, Client

app = Flask(__name__)

# --- CONFIGURATION ---
SAVE_DIR = "/mnt/ramdisk/debug_images"
os.makedirs(SAVE_DIR, exist_ok=True)

# Credentials - Update with your actual details
SUPABASE_URL = "https://estunmbmwzxactvrqjgo.supabase.co"
SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImVzdHVubWJtd3p4YWN0dnJxamdvIiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc3MjgwODAxMiwiZXhwIjoyMDg4Mzg0MDEyfQ.GIQZMPlHHozJV3JqeIhJLfSM_OWjPao5B8J-zjEiLi8"
supabase: Client = create_client(SUPABASE_URL, SUPABASE_KEY)


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
        captured_at = request.args.get(
            'captured_at') or datetime.utcnow().isoformat()
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
            print(
                f"[CRC WARNING] Mismatch! Expected: {expected_crc_hex}, Computed: {computed_crc_hex}")

        # --- 4. SAVE TO RAMDISK (FOR ML PROCESSING) ---
        # Create a specific directory for the trap if it doesn't exist
        trap_ram_dir = os.path.join(SAVE_DIR, trap_id)
        os.makedirs(trap_ram_dir, exist_ok=True)

        ram_filepath = os.path.join(trap_ram_dir, image_filename)
        with open(ram_filepath, "wb") as f:
            f.write(image_bytes)
        print(f"[RAMDISK] Image saved for ML: {ram_filepath}")

        # --- 5. STEP 1: VALIDATE TRAP EXISTS ---
        # Using maybe_single() to avoid PGRST116 error if trap isn't in DB
        trap_check = supabase.table("traps").select("id").eq(
            "trap_id", trap_id).maybe_single().execute()
        if not trap_check.data:
            return jsonify({"success": False, "error": f"Trap not found: {trap_id}"}), 404

        # --- 6. STEP 2: STORAGE UPLOAD (Cloud Backup) ---
        image_path = f"{trap_id}/{image_filename}"
        supabase.storage.from_("trap-images").upload(
            path=image_path,
            file=image_bytes,
            file_options={"content-type": "image/jpeg", "upsert": "true"}
        )

        # --- 7. STEP 3: INSERT IMAGE_UPLOADS ---
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
            "image_filename": image_filename,
            "image_size_bytes": len(image_bytes),
            "content_type": "image/jpeg",
            "upload_status": "uploaded"
        }
# htt
        db_res = supabase.table("image_uploads").insert(upload_data).execute()
        if not db_res.data:
            raise Exception("Failed to insert image_uploads record")

        image_upload_id = db_res.data[0]['id']

        # --- 8. STEP 4: UPDATE TRAPS TABLE WITH LATEST VOLTAGE READINGS ---
        # Sync battery_voltage and solar_voltage to the traps table for real-time monitoring
        trap_update_data = {
            "battery_voltage": float(battery_voltage) if battery_voltage else None,
            "solar_voltage": float(solar_voltage) if solar_voltage else None,
            "last_voltage_update": datetime.utcnow().isoformat()
        }
        traps_update = supabase.table("traps").update(trap_update_data).eq(
            "trap_id", trap_id).execute()
        print(f"[TRAPS UPDATE] Voltage data synced for trap {trap_id}")

        # --- 9. STEP 5: INSERT DETECTION_RESULTS PLACEHOLDER ---
        detection_res = supabase.table("detection_results").insert({
            "image_upload_id": image_upload_id,
            "beetle_count": 0,
            "male_count": 0,
            "female_count": 0,
            "unknown_count": 0,
            "remarks": "Pending processing (RPi Bridge)"
        }).execute()

        return jsonify({
            "success": True,
            "message": "Image synced to RAM and Cloud",
            "ram_path": ram_filepath,
            "image_upload_id": image_upload_id,
            "computed_crc32": computed_crc_hex
        }), 200

    except Exception as e:
        print(f"Unexpected error: {e}")
        return jsonify({"success": False, "error": str(e)}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
