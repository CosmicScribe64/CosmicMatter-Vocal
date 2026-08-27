FROM python:3.12-slim

COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt

WORKDIR /workspace
ENTRYPOINT ["python", "/workspace/scripts/analyze_ab_deep.py"]
