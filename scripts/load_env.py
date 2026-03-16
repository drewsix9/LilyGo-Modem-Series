import os
from pathlib import Path
from SCons.Script import Import

Import("env")

# Read .env file directly without external dependencies
env_file = Path(os.getcwd()) / ".env"
if env_file.exists():
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            # Skip comments and empty lines
            if line and not line.startswith("#"):
                if "=" in line:
                    key, value = line.split("=", 1)
                    key = key.strip()
                    value = value.strip()
                    # Add as C preprocessor define with proper string escaping
                    # Use raw string format to preserve backslashes
                    env.Append(CPPDEFINES=[f'{key}=\\"{value}\\"'])
                    print(f"[LOAD_ENV] Loaded {key}")
else:
    print(f"[LOAD_ENV] Warning: .env file not found")
