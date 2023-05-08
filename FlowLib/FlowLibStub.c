#include <stdbool.h>
#include "FlowLib.h"

FlowHandle FlowCreateHandle(const char* videoPath, FlowProperties* properties) {};
void FlowDestroyHandle(FlowHandle handle) {};
void FlowSetLogger(LoggingCallback callback) {};
void FlowRun(FlowHandle handle, FlowRunCallback callback) {};
FrameNumber FlowGetLength(FlowHandle handle) {};
void FlowDrawRange(FlowHandle handle, FrameRange range, DrawCallback callback, void* userData) {};
void FlowCalcWave(FlowHandle handle, FrameRange range, DrawCallback callback, void* userData) {};
float FlowProgress(FlowHandle handle) {};
void FlowSave(FlowHandle handle, const char* path) {};
