"""Run only with the Vulkan loader explicitly restricted to a software ICD."""

import subprocess
import sys

result = subprocess.run([sys.argv[1], "--device", "hardware"],
                        capture_output=True, text=True, timeout=30)
assert result.returncode != 0, "software-only setup silently fulfilled a hardware request"
assert "no compatible device for requested class" in result.stderr, result.stderr
print("software-only runtime correctly rejected --device hardware")
