import requests
import time

while True:

    response = requests.get(
        f"http://127.0.0.1:8000/result/{job_id}"
    )

    data = response.json()

    print(data["status"])

    if data["status"] == "finished":

        print(data["result"])
        break

    time.sleep(1)