FROM node:lts-bullseye

ARG DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install -y \
    build-essential \
    cmake \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libopencv-dev \
    ocl-icd-opencl-dev \
    opencl-c-headers \
    opencl-clhpp-headers

ADD FlowLib /usr/local/src/FlowLib
WORKDIR /usr/local/src/FlowLib/build
RUN cmake .. -DSKIP_BUILD_CUDA=ON && make && make install

RUN npm install nodemon -g

COPY Server/index.mjs /usr/local/src/FlowLibServer/index.mjs
COPY Server/package.json /usr/local/src/FlowLibServer/package.json
WORKDIR /usr/local/src/FlowLibServer
RUN npm install

EXPOSE 3000

ENTRYPOINT [ "/usr/local/bin/nodemon", "-L", "index.mjs" ]
# ENTRYPOINT [ "/bin/bash" ]