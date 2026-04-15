#!/usr/bin/env python3
"""
Simple YOLOv8 Inference Script
Quick inference runner for testing beetle detection on individual images
Usage: python inference.py <image_path> [output_path]
"""

import sys
import os
import time
from pathlib import Path
import cv2
from ultralytics import YOLO

# Model configuration
MODEL_PATH = os.getenv(
    "YOLO_MODEL_PATH", "~/crb-backend/best_ncnn_model")
MODEL_PATH = os.path.expanduser(MODEL_PATH)


def validate_ncnn_model(model_path):
    """Validate that both .param and .bin files exist for NCNN model"""
    if model_path.endswith('.param'):
        bin_path = model_path.replace('.param', '.bin')
    else:
        bin_path = model_path + '.bin'

    param_exists = os.path.exists(model_path)
    bin_exists = os.path.exists(bin_path)

    if not param_exists:
        print(f"[ERROR] NCNN .param file not found: {model_path}")
        return False

    if not bin_exists:
        print(f"[ERROR] NCNN .bin file not found: {bin_path}")
        print(
            "[INFO] NCNN models require both .param and .bin files in the same directory")
        return False

    return True


def run_inference(image_path, output_path=None):
    """
    Run YOLOv8 inference on a single image

    Args:
        image_path: Path to input image
        output_path: Optional path to save annotated image

    Returns:
        dict: Detection counts {"male": int, "female": int, "unknown": int}
    """

    # Validate input
    if not os.path.exists(image_path):
        print(f"[ERROR] Image not found: {image_path}")
        return None

    # Load model
    print(f"[INFO] Loading model from: {MODEL_PATH}")
    try:
        # Validate NCNN model files
        if MODEL_PATH.endswith('.param'):
            if not validate_ncnn_model(MODEL_PATH):
                return None

        model = YOLO(MODEL_PATH, task='detect')
        print("[OK] Model loaded successfully")
    except Exception as e:
        print(f"[FAIL] Failed to load model: {e}")
        print("\n[INFO] Troubleshooting tips:")
        print("  - For NCNN models: ensure both .param and .bin files exist in the same directory")
        print("  - Check YOLO_MODEL_PATH environment variable is correctly set")
        print("  - Try alternative formats: ONNX, PyTorch, or TensorFlow Lite")
        return None

    # Load and validate image
    print(f"[INFO] Loading image: {image_path}")
    img = cv2.imread(image_path)
    if img is None:
        print(f"[ERROR] Failed to load image: {image_path}")
        return None
    print(f"[OK] Image loaded: {img.shape}")

    # Run inference
    print("[INFO] Running inference...")
    inference_start = time.time()
    try:
        results = model(img, conf=0.5, verbose=False)[0]
        inference_time_ms = int((time.time() - inference_start) * 1000)
        print(f"[OK] Inference complete: {inference_time_ms}ms")
    except Exception as e:
        print(f"[FAIL] Inference error: {e}")
        return None

    # Count detections by class
    counts = {"male": 0, "female": 0, "unknown": 0}

    if results.boxes is not None and len(results.boxes) > 0:
        for box in results.boxes:
            try:
                cls_id = int(box.cls[0])
                confidence = float(box.conf[0])
                label = results.names[cls_id].lower(
                ) if cls_id in results.names else "unknown"

                if label in counts:
                    counts[label] += 1
                    print(
                        f"  Detected: {label} (confidence: {confidence:.2f})")
            except Exception as e:
                print(f"  Warning parsing detection: {e}")
                continue

    total = sum(counts.values())
    print(f"[OK] Detection results:")
    print(f"  Male: {counts['male']}")
    print(f"  Female: {counts['female']}")
    print(f"  Unknown: {counts['unknown']}")
    print(f"  Total: {total}")

    # Save annotated image if requested
    if output_path:
        try:
            annotated_img = results.plot()
            cv2.imwrite(output_path, annotated_img)
            print(f"[OK] Annotated image saved: {output_path}")
        except Exception as e:
            print(f"[FAIL] Failed to save annotated image: {e}")

    return counts


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python inference.py <image_path> [output_image_path]")
        print("Example: python inference.py /path/to/image.jpg /tmp/annotated.jpg")
        sys.exit(1)

    image_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    print("\n=== YOLOv8 Beetle Detection Inference ===\n")
    counts = run_inference(image_path, output_path)

    if counts:
        print("\n[SUCCESS] Inference completed\n")
        sys.exit(0)
    else:
        print("\n[FAIL] Inference failed\n")
        sys.exit(1)
