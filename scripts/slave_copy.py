#!/usr/bin/env python3
"""
Pre-build script for ESP32CAM-Slave environment
Creates a symlink to the slave .ino at root level so PlatformIO finds it as the main sketch
"""

import os
import shutil

# Construction environment
env = DefaultEnvironment()

# Get the source directory
src_dir = "crb-trap/Lilygo+SERIAL+POST+SENSORS+SD"

master_ino = os.path.join(src_dir, "Lilygo+CamESPNOW.ino")
slave_ino = os.path.join(src_dir, "ESP32CAM_Slave", "ESP32CAM_Slave.ino")
master_hidden = os.path.join(src_dir, "Lilygo+CamESPNOW.ino.hidden")
slave_link = os.path.join(
    src_dir, "Lilygo+CamESPNOW.ino")  # Symlink target name

print("[PRE-BUILD] Checking for slave vs master environment...")

# Check if this is a slave build based on upload_port
upload_port = env.GetProjectOption("upload_port", "")
is_slave_build = "COM16" in upload_port

if is_slave_build:
    print(f"[SLAVE-BUILD] Detected slave environment (port: {upload_port})")

    # Hide/backup master .ino if not already hidden
    if os.path.exists(master_ino) and not os.path.exists(master_hidden):
        print(f"[SLAVE-BUILD] Backing up master .ino: {master_ino}")
        os.rename(master_ino, master_hidden)

    # Create symlink/copy slave .ino at root level
    if os.path.exists(slave_ino):
        if not os.path.exists(slave_link):
            print(f"[SLAVE-BUILD] Creating link: {slave_link} → {slave_ino}")
            # Use shutil.copy instead of symlink (more portable on Windows)
            shutil.copy(slave_ino, slave_link)
        print(f"[SLAVE-BUILD] Slave .ino ready for compilation")
    else:
        print(f"[SLAVE-BUILD] ERROR: Slave .ino not found: {slave_ino}")
else:
    # Restore master .ino if backed up
    if os.path.exists(master_hidden):
        print("[PRE-BUILD] Restoring master .ino from backup...")
        if os.path.exists(slave_link) and os.path.exists(master_hidden):
            os.remove(slave_link)
        os.rename(master_hidden, master_ino)
        print("[PRE-BUILD] Master .ino restored")
