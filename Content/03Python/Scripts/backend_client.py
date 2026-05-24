import requests

payload = {
    "fx_path": "/home/pgh/feature.npy",
    "sampling_size": 128,
    "knn_k": 8,
    "max_step": 30
}

response = requests.post(
    "http://127.0.0.1:8000/generate_weathering",
    json=payload
)

job_id = response.json()["job_id"]

print(job_id)