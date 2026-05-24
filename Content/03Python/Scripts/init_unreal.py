import subprocess
import requests
import os


BACKEND_ROOT = \
    "/home/pgh/2026CapstoneDesign_08_01/Content/03Python/backend"

PYTHON_EXE = \
    "/home/pgh/miniforge3/envs/rapids-25.04/bin/python"


ENABLE_BACKEND = True


# --------------------------------------------------
# HEALTH CHECK
# --------------------------------------------------

def is_backend_running():

    try:

        requests.get(
            "http://127.0.0.1:8000/health",
            timeout=1
        )

        return True

    except:

        return False


# --------------------------------------------------
# START BACKEND
# --------------------------------------------------

def start_backend():

    if is_backend_running():
        return

    # FastAPI
    subprocess.Popen([
        PYTHON_EXE,
        "-m",
        "uvicorn",
        "app:app",
        "--host",
        "127.0.0.1",
        "--port",
        "8000"
    ], cwd=BACKEND_ROOT)

    # RQ Worker
    subprocess.Popen([
        PYTHON_EXE,
        "rq_worker.py"
    ], cwd=BACKEND_ROOT)


if ENABLE_BACKEND:
    start_backend()