from redis import Redis
from rq import Worker, Queue

listen = ["weathering"]
redis_conn = Redis(host="127.0.0.1",port=6379)
worker = Worker([Queue(name, connection=redis_conn) for name in listen])
worker.work()
