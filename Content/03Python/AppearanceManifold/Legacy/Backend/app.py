from fastapi import FastAPI
from redis import Redis
from rq import Queue
from rq.job import Job
from tasks.texture_task import generate_weathering_texture_task

app = FastAPI()

redis_conn = Redis(host="127.0.0.1",port=6379)
texture_queue = Queue("weathering", connection=redis_conn)

@app.get("/health")
def health_check():
    return {"status": "ok"}

@app.post("/generate_weathering")
def generate_weathering(payload: dict):
    job = texture_queue.enqueue(generate_weathering_texture_task,payload,job_timeout="2h")
    return {"job_id": job.id}

@app.get("/result/{job_id}")
def get_result(job_id: str):

    job = Job.fetch( job_id,connection=redis_conn)

    return {"status": job.get_status(),"result": job.result}