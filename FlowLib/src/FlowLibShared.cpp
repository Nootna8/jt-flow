// #include <opencv2/core/utils/logger.hpp>
#include "FlowLibShared.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
#include <string>
#include <Python.h>
#include "numpy/arrayobject.h"

LoggingCallback logger = nullptr;
std::string lastError;

int loadarr()
{
    if(PyArray_API == NULL) {
        import_array1(-1);
    }
    return 0;
}

class ResourceHolder {
public:
    ResourceHolder() {
        Py_Initialize();
        loadarr();

        PyRun_SimpleString("import sys");
        PyObject *sys_path = PySys_GetObject("path");
        PyList_Append(sys_path, PyUnicode_FromString("."));
        // PyList_Append(sys_path, PyUnicode_FromString("/usr/local/src/model"));
    }

    ~ResourceHolder() {
        Py_Finalize();
    }
};

ResourceHolder theHolder;

char* FlowLastError()
{
    return (char*)lastError.c_str();
}

FlowHandle FlowCreateHandle(const char* videoPath, FlowProperties* config)
{
    try {
        FlowLibShared* handle = CreateFlowLib(videoPath, config);
        MY_LOG("[FlowLib] handle created");
        return (FlowHandle)handle;
    } catch (std::exception& e) {
        lastError = e.what();
        MY_LOG(cv::format("[FlowLib] handle creation failed: %s", e.what()).c_str());
        return nullptr;
    }
}

bool FlowDestroyHandle(FlowHandle handlePtr)
{
    try {
        FlowLibShared* handle = (FlowLibShared*)handlePtr;
        delete handle;
        MY_LOG("[FlowLib] handle destroyed");
        return true;
    } catch (std::exception& e) {
        lastError = e.what();
        MY_LOG(cv::format("[FlowLib] handle destruction failed: %s", e.what()).c_str());
        return false;
    }
}

FrameNumber FlowGetLength(FlowHandle handlePtr)
{
    try {
        if(handlePtr == nullptr) {
            throw std::runtime_error("Invalid handle");
        }
        FlowLibShared* handle = (FlowLibShared*)handlePtr;
        return handle->GetNumFrames();
    } catch (std::exception& e) {
        lastError = e.what();
        MY_LOG(cv::format("[FlowLib] get length failed: %s", e.what()).c_str());
        return 0;
    }
}

FrameNumber FlowGetLengthMs(FlowHandle handlePtr)
{
    try {
        if(handlePtr == nullptr) {
            throw std::runtime_error("Invalid handle");
        }
        FlowLibShared* handle = (FlowLibShared*)handlePtr;
        return handle->GetNumMs();
    } catch (std::exception& e) {
        lastError = e.what();
        MY_LOG(cv::format("[FlowLib] get length ms failed: %s", e.what()).c_str());
        return 0;
    }
}

bool FlowDrawRange(FlowHandle handlePtr, FrameRange range, DrawCallback callback, void* userData)
{
    // Runner* runner = (Runner*)handle;
    // runner->FlowDrawRange(range, callback, userData);'
    return true;
}

bool FlowCalcWave(FlowHandle handlePtr, FrameRange range, DrawCallback callback, void* userData)
{
    PyObject* m_PyModule = NULL;
    PyObject* processFunc = NULL;
    PyObject* pyMat = NULL;
    PyObject* result = NULL;
    PyObject* resultArr = NULL;
    bool ret = true;

    try {
        FlowLibShared* handle = (FlowLibShared*)handlePtr;
        cv::Mat mat = handle->GetMat();

        if(range.fromFrame < 0 || range.toFrame > mat.rows) {
            throw std::runtime_error("Invalid range");
        }

        PyRun_SimpleString("import sys");
        PyObject *sys_path = PySys_GetObject("path");
        PyList_Append(sys_path, PyUnicode_FromString("."));
        PyList_Append(sys_path, PyUnicode_FromString("/usr/local/src/model"));
        
        m_PyModule = PyImport_ImportModule("jtmodel"); 

        if(!m_PyModule) {
            PyErr_Print();
            throw std::runtime_error("Failed to load jtmodel.py");
        }

        PyObject* processFunc = PyObject_GetAttrString(m_PyModule, "process");
        if (!PyCallable_Check(processFunc)) {
            PyErr_Print();
            throw std::runtime_error("Cannot find function 'process'");
        }
        
        npy_intp mdim[] = { mat.rows, mat.cols };

        PyObject* pyMat = PyArray_SimpleNewFromData(2, mdim, NPY_INT32, mat.ptr<int>(0));
        PyObject* result = PyObject_CallFunctionObjArgs(processFunc, pyMat, NULL);

        if (result == NULL) {
            PyErr_Print();
            throw std::runtime_error("Call failed");
        }

        PyArrayObject *resultArr = reinterpret_cast<PyArrayObject*>(result);

        if(PyArray_TYPE(resultArr) != NPY_INT32) {
            throw std::runtime_error("Invalid result type");
        }

        int nDims = PyArray_NDIM(resultArr);
        npy_intp* dims = PyArray_SHAPE(resultArr);

        if(nDims != 2 || dims[1] != 2) {
            throw std::runtime_error("Invalid result dimensions");
        }

        int* data = (int*)PyArray_DATA(resultArr);
        callback(data, dims[0], dims[1], userData);

        printf("Python model call success\n");
    } catch (std::exception& e) {
        MY_LOG(cv::format("[FlowLib] calc wave failed: %s", e.what()).c_str());
        lastError = e.what();
        ret = false;
    }
    
    if(m_PyModule != NULL) Py_DECREF(m_PyModule);
    if(processFunc != NULL) Py_DECREF(processFunc);
    if(pyMat != NULL) Py_DECREF(pyMat);
    if(result != NULL) Py_DECREF(result);
    if(resultArr != NULL) Py_DECREF(resultArr);
    
    return ret;
}

bool FlowSetLogger(LoggingCallback callback)
{
    logger = callback;
    MY_LOG("[FlowLib] logger attached");
    return true;
}

bool FlowSave(FlowHandle handlePtr, const char* path)
{
    // FlowLibShared* handle = (FlowLibShared*)handlePtr;
    // cv::Mat oMat = handle->GetMat();
    // cv::imwrite(path, oMat);
    // return true;

    PyObject* m_PyModule = NULL;
    PyObject* processFunc = NULL;
    PyObject* pyMat = NULL;
    PyObject* result = NULL;
    PyObject* resultArr = NULL;
    bool ret = true;

    try {
        FlowLibShared* handle = (FlowLibShared*)handlePtr;
        cv::Mat oMat = handle->GetMat();
        cv::Mat mat = oMat.clone();
        cv::Mat outputMat;
        
        m_PyModule = PyImport_ImportModule("jtmodel");
        m_PyModule = PyImport_ReloadModule(m_PyModule);

        if(!m_PyModule) {
            PyErr_Print();
            throw std::runtime_error("Failed to load jtmodel.py");
        }

        PyObject* processFunc = PyObject_GetAttrString(m_PyModule, "normalize");
        if (!PyCallable_Check(processFunc)) {
            PyErr_Print();
            throw std::runtime_error("Cannot find function 'normalize'");
        }
        
        npy_intp mdim[] = { mat.rows, mat.cols };

        PyObject* pyMat = PyArray_SimpleNewFromData(2, mdim, NPY_INT32, mat.ptr<int32_t>(0));
        PyObject* result = PyObject_CallFunctionObjArgs(processFunc, pyMat, NULL);

        if (result == NULL) {
            PyErr_Print();
            throw std::runtime_error("Call failed");
        }

        PyArrayObject *resultArr = reinterpret_cast<PyArrayObject*>(result);

        if(PyArray_TYPE(resultArr) != NPY_UINT8) {
            throw std::runtime_error("Invalid result type");
        }

        int nDims = PyArray_NDIM(resultArr);
        npy_intp* dims = PyArray_SHAPE(resultArr);

        outputMat = cv::Mat(dims[0], dims[1], CV_8UC1, PyArray_DATA(resultArr));
        cv::imwrite(path, outputMat);

        printf("Python model call success\n");
    } catch (std::exception& e) {
        MY_LOG(cv::format("[FlowLib] save flow failed: %s", e.what()).c_str());
        lastError = e.what();
        ret = false;
    } catch(...) {
        MY_LOG("[FlowLib] save flow failed: unknown error");
        lastError = "Unknown error";
        ret = false;
    }

    if(m_PyModule != NULL) Py_DECREF(m_PyModule);
    if(processFunc != NULL) Py_DECREF(processFunc);
    if(pyMat != NULL) Py_DECREF(pyMat);
    if(result != NULL) Py_DECREF(result);
    if(resultArr != NULL) Py_DECREF(resultArr);

    return ret;
}

float FlowProgress(FlowHandle handlePtr)
{
    FlowLibShared* handle = (FlowLibShared*)handlePtr;
    return handle->CurrentFrame() / handle->GetNumFrames();
}

bool FlowRun(FlowHandle handlePtr, FlowRunCallback callback, int callbackInterval)
{
    try {
        FlowLibShared* handle = (FlowLibShared*)handlePtr;
        handle->Run(callback, callbackInterval);
        return true;
    } catch (std::exception& e) {
        lastError = e.what();
        MY_LOG(cv::format("[FlowLib] run failed: %s", e.what()).c_str());
        return false;
    }
}