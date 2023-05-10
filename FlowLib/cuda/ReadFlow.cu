#include "ReadFlow.h"

#include "nvcuvid.h"
//#include "opencv2/opencv_modules.hpp"
#include "opencv2/cudev/common.hpp"

using namespace std;
using namespace cv;

#include <iostream>
#include <cmath>

using namespace std;

#define CUDART_PI_F 3.141592654f

// void cartesian_to_polar(float x, float y, float& magnitude, float& angle_degrees) {
//     magnitude = sqrtf(x * x + y * y);
//     angle_degrees = atanf(y / x) * 180.0f / CUDART_PI_F;
//     if (x < 0) {
//         angle_degrees += 180.0f;
//     }
//     else if (y < 0) {
//         angle_degrees += 360.0f;
//     }
// }


// int main() {
//     int lastDirection = 0;
//     int direction = 0;
//     float lastAngle = 0;
    
//     for(float x=-3.0; x<3.0; x+=0.2f) {
//         for(float y=-3.0; y<3.0; y+=0.2f) {
            
//             if(x < 0.05 && x > -0.05) {
//                 x = 0.0f;
//             }
            
//             if(y < 0.05 && y > -0.05) {
//                 y = 0.0f;
//             }
            
//             float magnitude;
//             float angle_degrees;
            
//             cartesian_to_polar(x, y, magnitude, angle_degrees);
            
//             int newDirection = 0;
//             float diff = lastAngle - angle_degrees;
//             lastAngle = angle_degrees;
            
//             if(diff < 0) {
//                 newDirection = -1;
//             }
//             if(diff > 0) {
//                 newDirection = 1;
//             }
            
//             if(newDirection != lastDirection) {
//                 lastDirection = newDirection;
//                 cout << "Dir change!" << std::endl;
//                 cout << "Angle: " << angle_degrees << " X: " << x << " Y: " << y << " Mag: " << magnitude << std::endl;
//             }
//         }
        
//     }
//     return 0;
// }

#define CUDART_PI_F 3.141592654f

__device__
void cartesian_to_polar(float x, float y, float& magnitude, float& angle_degrees) {
    magnitude = sqrtf(x * x + y * y);
    angle_degrees = atanf(y / x) * 180.0f / CUDART_PI_F;
    if (x < 0) {
        angle_degrees += 180.0f;
    }
    else if (y < 0) {
        angle_degrees += 360.0f;
    }
}

__global__ void MAT_POOL(
    int16_t* flowPtr,
    size_t flowPitch,
    int32_t* output,
    uint8_t pools,
    float threshold
){
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    const size_t mFlowPitch = flowPitch >> 2;

    int flowPtrAddr = (y * mFlowPitch) + (x * 2);

    float flow_x_val = (float)flowPtr[flowPtrAddr];
    float flow_y_val = (float)flowPtr[flowPtrAddr + 1];

    float magnitude;
    float angle;

    cartesian_to_polar(flow_x_val, flow_y_val, magnitude, angle);
    angle = angle / 360.0f;

    if (magnitude < threshold) {
        return;
    }

    uint8_t pool =  round(angle * (float)pools);
    
    if (pool < 0)
        pool = 0;
    
    if (pool >= pools)
        pool = pools - 1;

    ::atomicAdd((int*)output + pool, 1);
}

void runMatPool(cv::cuda::GpuMat flow, cv::cuda::GpuMat output, uint8_t pools, float threshold)
{
    assert(flow.channels() == 2);
    assert(output.rows == 1 && output.cols == pools);

    dim3 block(8, 8);
    dim3 grid(divUp(flow.cols, block.x), divUp(flow.rows, block.y));

    MAT_POOL << <grid, block >> > (
        flow.ptr<int16_t>(),
        flow.step,
        output.ptr<int32_t>(),
        pools,
        threshold
    );

    CV_CUDEV_SAFE_CALL(cudaGetLastError());
}