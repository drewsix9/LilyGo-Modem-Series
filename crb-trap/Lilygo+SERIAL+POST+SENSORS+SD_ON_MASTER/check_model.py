#!/usr/bin/env python3
"""
NCNN Model File Checker
Diagnoses NCNN model setup issues
"""

import os
import sys

# Model configuration
MODEL_PATH = os.getenv(
    "YOLO_MODEL_PATH", "~/crb-backend/best_ncnn_model/model.ncnn.param")
MODEL_PATH = os.path.expanduser(MODEL_PATH)


def check_ncnn_model():
    """Check if NCNN model files are properly set up"""

    print("\n=== NCNN Model Diagnostic ===\n")
    print(
        f"YOLO_MODEL_PATH env var: {os.getenv('YOLO_MODEL_PATH', 'NOT SET (using default)')}")
    print(f"Expanded model path: {MODEL_PATH}\n")

    # Check .param file
    if MODEL_PATH.endswith('.param'):
        param_file = MODEL_PATH
        bin_file = MODEL_PATH.replace('.param', '.bin')
    else:
        param_file = MODEL_PATH + '.param'
        bin_file = MODEL_PATH + '.bin'

    model_dir = os.path.dirname(param_file)

    print(f"Looking in directory: {model_dir}")

    # Check if directory exists
    if not os.path.exists(model_dir):
        print(f"[FAIL] Directory does not exist: {model_dir}")
        return False

    print(f"[OK] Directory exists\n")

    # List files in directory
    print(f"Files in {model_dir}:")
    try:
        files = os.listdir(model_dir)
        if not files:
            print("  (empty directory)")
        else:
            for f in sorted(files):
                file_path = os.path.join(model_dir, f)
                size_mb = os.path.getsize(file_path) / (1024 * 1024)
                print(f"  {f} ({size_mb:.1f} MB)")
    except Exception as e:
        print(f"  [ERROR] Could not list files: {e}")
        return False

    print("\n")

    # Check for required files
    param_exists = os.path.exists(param_file)
    bin_exists = os.path.exists(bin_file)

    print(
        f".param file ({os.path.basename(param_file)}): {'[OK]' if param_exists else '[MISSING]'}")
    print(
        f".bin file ({os.path.basename(bin_file)}): {'[OK]' if bin_exists else '[MISSING]'}")

    if not param_exists or not bin_exists:
        print("\n[FAIL] NCNN model files incomplete!")
        print("\nRequired files for NCNN models:")
        print(f"  - {param_file}")
        print(f"  - {bin_file}")
        print("\nTo export a model to NCNN format:")
        print("  yolo export model=path/to/best.pt format=ncnn imgsz=640")
        return False

    print("\n[SUCCESS] NCNN model files are complete!")
    print(f"Ready to use model from: {model_dir}")
    return True


if __name__ == "__main__":
    success = check_ncnn_model()
    sys.exit(0 if success else 1)
