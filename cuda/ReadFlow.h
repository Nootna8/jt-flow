#pragma once

#include <stdint.h>

namespace cv { namespace cuda {
    class GpuMat;
} ; } ;

void runMatPool(cv::cuda::GpuMat flow, cv::cuda::GpuMat output, uint8_t pools, float threshold);