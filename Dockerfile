FROM thecanadianroot/opencv-cuda

ARG DEBIAN_FRONTEND=noninteractive
ARG NODE_VERSION=16

RUN wget -O - https://deb.nodesource.com/setup_$NODE_VERSION.x | bash -

RUN apt update && apt install -y --no-install-recommends \
    ocl-icd-opencl-dev \
    opencl-c-headers \
    opencl-clhpp-headers \
    nodejs

ADD FlowLib /usr/local/src/FlowLib
WORKDIR /usr/local/src/FlowLib/build
RUN cmake .. -DSKIP_BUILD_CUDA=ON -GNinja && ninja && ninja install

RUN npm install nodemon -g

COPY Server/index.mjs /usr/local/src/FlowLibServer/index.mjs
COPY Server/package.json /usr/local/src/FlowLibServer/package.json
WORKDIR /usr/local/src/FlowLibServer
RUN npm install

EXPOSE 3000
ENTRYPOINT [ "nodemon", "-L", "index.mjs" ]

# ENTRYPOINT [ "/bin/bash" ]