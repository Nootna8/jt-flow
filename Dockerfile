FROM thecanadianroot/opencv-cuda

ARG DEBIAN_FRONTEND=noninteractive
ARG NODE_VERSION=16

RUN wget -O - https://deb.nodesource.com/setup_$NODE_VERSION.x | bash -

RUN apt update && apt install -y --no-install-recommends \
    ocl-icd-opencl-dev \
    opencl-c-headers \
    opencl-clhpp-headers \
    nodejs \
    python3-scipy


RUN npm install nodemon -g

COPY Server/package.json /app/Server/package.json
WORKDIR /app/Server
RUN npm install

ENV NVIDIA_VISIBLE_DEVICES all
ENV NVIDIA_DRIVER_CAPABILITIES compute,utility,video

WORKDIR /tmp
RUN wget https://developer.nvidia.com/downloads/video-codec-sdk-12016-interfaces -O nvcuvid.zip && unzip nvcuvid.zip && cp Video_Codec_Interface_12.0.16/Interface/*.h /usr/include

COPY FlowLib/cuda/libnvcuvid.so /usr/lib/x86_64-linux-gnu/libnvcuvid.so.1
RUN cp /usr/local/cuda/lib64/stubs/libcuda.so /usr/lib/x86_64-linux-gnu/libcuda.so.1

ADD FlowLib/cmake /app/FlowLib/cmake
ADD FlowLib/cuda /app/FlowLib/cuda
ADD FlowLib/lav /app/FlowLib/lav
ADD FlowLib/src /app/FlowLib/src
COPY FlowLib/CMakeLists.txt /app/FlowLib/

WORKDIR /app/FlowLib/build
RUN cmake .. -GNinja -DDOCKER=ON && ninja && ninja install

ADD Model /app/Model
COPY Server/index.mjs /app/Server/index.mjs
RUN ln -s /app/Model/vectorFrame.ocl /app/Server/vectorFrame.ocl
RUN ln -s /app/Model/jtmodel.py /app/Server/jtmodel.py

WORKDIR /app/Server
EXPOSE 3000
ENV LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libpython3.8.so
ENV PATH="${PATH}:/app/Model"
ENTRYPOINT [ "nodemon", "-L", "index.mjs" ]