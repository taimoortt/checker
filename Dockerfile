FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive \
    MPLCONFIGDIR=/tmp/matplotlib \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       ca-certificates \
       gcc-10 \
       g++-10 \
       libjsoncpp-dev \
       python3 \
       python3-pip \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 100 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/radioninja
COPY requirements.txt .
RUN python3 -m pip install --no-cache-dir pip==24.0 setuptools==69.5.1 wheel==0.43.0 \
    && python3 -m pip install --no-cache-dir -r requirements.txt

COPY . .
RUN python3 artifact_pipeline.py build \
    && python3 -m unittest discover -s tests -v

ENTRYPOINT ["python3", "artifact_pipeline.py"]
CMD ["--help"]
