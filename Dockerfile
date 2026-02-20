# ESP-IDF Docker Image for Matter Development
#
# SHARED IMAGE: This Dockerfile is used by multiple ESP-Matter projects
# Image name: esp-matter-dev:latest
# All projects using this base configuration share the same image to save disk space

FROM espressif/esp-matter:release-v1.5_idf_v5.4.1

# Install additional dependencies needed for Matter and pairing scripts
RUN apt-get update && apt-get install -y \
    git \
    python3-pip \
    python3-venv \
    libfreetype6-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Python dependencies for pairing code generation into the IDF venv.
# The IDF entrypoint activates /opt/espressif/tools/python_env/idf5.4_py3.12_env/,
# so system-level pip installs are invisible at runtime — use the venv pip directly.
RUN /opt/espressif/tools/python_env/idf5.4_py3.12_env/bin/pip install qrcode pillow

# Set working directory (matches docker-compose.yml)
WORKDIR /project

# Default command
CMD ["/bin/bash"]
