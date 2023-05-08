#pragma once

void videoDecPostProcessFrameRGB(CUdeviceptr decodedFrame, size_t pitch, CUdeviceptr outFrame, size_t outPitch, int width, int height, cudaStream_t stream);
void videoDecPostProcessFrameGREY(CUdeviceptr decodedFrame, size_t pitch, CUdeviceptr outFrame, size_t outPitch, int width, int height, cudaStream_t stream);