/*
* Copyright 2017-2021 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

#pragma once

#include <assert.h>
#include <stdint.h>
#include <mutex>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <string.h>
#include "driver_types.h"
#include "nvcuvid.h"
#include <map>
#include <deque>

#include <opencv2/core/cuda.hpp>

/**
* @brief Utility class to measure elapsed time in seconds between the block of executed code
*/
class StopWatch {
public:
    void Start() {
        t0 = std::chrono::high_resolution_clock::now();
    }
    double Stop() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch() - t0.time_since_epoch()).count() / 1.0e9;
    }

private:
    std::chrono::high_resolution_clock::time_point t0;
};

/**
* @brief Exception class for error reporting from the decode API.
*/
class NVDECException : public std::exception
{
public:
    NVDECException(const std::string& errorStr, const CUresult errorCode)
        : m_errorString(errorStr), m_errorCode(errorCode) {}

    virtual ~NVDECException() throw() {}
    virtual const char* what() const throw() { return m_errorString.c_str(); }
    CUresult  getErrorCode() const { return m_errorCode; }
    const std::string& getErrorString() const { return m_errorString; }
    static NVDECException makeNVDECException(const std::string& errorStr, const CUresult errorCode,
        const std::string& functionName, const std::string& fileName, int lineNo);
private:
    std::string m_errorString;
    CUresult m_errorCode;
};

inline NVDECException NVDECException::makeNVDECException(const std::string& errorStr, const CUresult errorCode, const std::string& functionName,
    const std::string& fileName, int lineNo)
{
    std::ostringstream errorLog;
    errorLog << functionName << " : " << errorStr << " at " << fileName << ":" << lineNo << std::endl;
    NVDECException exception(errorLog.str(), errorCode);
    return exception;
}

#define NVDEC_THROW_ERROR( errorStr, errorCode )                                                         \
    do                                                                                                   \
    {                                                                                                    \
        throw NVDECException::makeNVDECException(errorStr, errorCode, __FUNCTION__, __FILE__, __LINE__); \
    } while (0)


#define NVDEC_API_CALL( cuvidAPI )                                                                                 \
    do                                                                                                             \
    {                                                                                                              \
        CUresult errorCode = cuvidAPI;                                                                             \
        if( errorCode != CUDA_SUCCESS)                                                                             \
        {                                                                                                          \
            std::ostringstream errorLog;                                                                           \
            errorLog << #cuvidAPI << " returned error " << errorCode;                                              \
            throw NVDECException::makeNVDECException(errorLog.str(), errorCode, __FUNCTION__, __FILE__, __LINE__); \
        }                                                                                                          \
    } while (0)

struct Rect {
    int l, t, r, b;
};

struct Dim {
    int w, h;
};

/**
* @brief Base class for decoder interface.
*/
class NvDecoder {

public:
    /**
    *  @brief This function is used to initialize the decoder session.
    *  Application must call this function to initialize the decoder, before
    *  starting to decode any frames.
    */
    NvDecoder(bool bUseDeviceFrame, cudaVideoCodec eCodec, bool bLowLatency = false,
              bool bDeviceFramePitched = false, const Rect *pCropRect = NULL, const Dim *pResizeDim = NULL,
              int maxWidth = 0, int maxHeight = 0, unsigned int clkRate = 1000, bool force_zero_latency = false);
    ~NvDecoder();

    void Flush();

    /**
    *  @brief  This function is used to get the output frame width.
    *  NV12/P016 output format width is 2 byte aligned because of U and V interleave
    */
    int GetWidth() { assert(m_nWidth); return (m_eOutputFormat == cudaVideoSurfaceFormat_NV12 || m_eOutputFormat == cudaVideoSurfaceFormat_P016) 
                                                ? (m_nWidth + 1) & ~1 : m_nWidth; }

    /**
    *  @brief  This function is used to get the actual decode width
    */
    int GetDecodeWidth() { assert(m_nWidth); return m_nWidth; }

    /**
    *  @brief  This function is used to get the output frame height (Luma height).
    */
    int GetHeight() { assert(m_nLumaHeight); return m_nLumaHeight; }

    /**
    *  @brief  This function is used to get the current chroma height.
    */
    int GetChromaHeight() { assert(m_nChromaHeight); return m_nChromaHeight; }

    /**
    *  @brief  This function is used to get the number of chroma planes.
    */
    int GetNumChromaPlanes() { assert(m_nNumChromaPlanes); return m_nNumChromaPlanes; }
    
    /**
    *   @brief  This function is used to get the current frame size based on pixel format.
    */
    int GetFrameSize() { assert(m_nWidth); return GetWidth() * (m_nLumaHeight + (m_nChromaHeight * m_nNumChromaPlanes)) * m_nBPP; }

    /**
    *   @brief  This function is used to get the current frame Luma plane size.
    */
    int GetLumaPlaneSize() { assert(m_nWidth); return GetWidth() * m_nLumaHeight * m_nBPP; }

    /**
    *   @brief  This function is used to get the current frame chroma plane size.
    */
    int GetChromaPlaneSize() { assert(m_nWidth); return GetWidth() *  (m_nChromaHeight * m_nNumChromaPlanes) * m_nBPP; }

    /**
    *  @brief  This function is used to get the pitch of the device buffer holding the decoded frame.
    */
    int GetDeviceFramePitch() { assert(m_nWidth); return m_nDeviceFramePitch ? (int)m_nDeviceFramePitch : GetWidth() * m_nBPP; }

    /**
    *   @brief  This function is used to get the bit depth associated with the pixel format.
    */
    int GetBitDepth() { assert(m_nWidth); return m_nBitDepthMinus8 + 8; }

    /**
    *   @brief  This function is used to get the bytes used per pixel.
    */
    int GetBPP() { assert(m_nWidth); return m_nBPP; }

    /**
    *   @brief  This function is used to get the YUV chroma format
    */
    cudaVideoSurfaceFormat GetOutputFormat() { return m_eOutputFormat; }

    /**
    *   @brief  This function is used to get information about the video stream (codec, display parameters etc)
    */
    CUVIDEOFORMAT GetVideoFormatInfo() { assert(m_nWidth); return m_videoFormat; }

    /**
    *   @brief  This function is used to get codec string from codec id
    */
    const char *GetCodecString(cudaVideoCodec eCodec);

    /**
    *   @brief  This function is used to print information about the video stream
    */
    std::string GetVideoInfo() const { return m_videoInfo.str(); }

    /**
    *   @brief  This function decodes a frame and returns the number of frames that are available for
    *   display. All frames that are available for display should be read before making a subsequent decode call.
    *   @param  pData - pointer to the data buffer that is to be decoded
    *   @param  nSize - size of the data buffer in bytes
    *   @param  nFlags - CUvideopacketflags for setting decode options
    *   @param  nTimestamp - presentation timestamp
    */
    int Decode(const uint8_t *pData, int nSize, int nFlags = 0, int64_t nTimestamp = 0, cudaStream_t stream = nullptr);

    /**
    *   @brief  This function returns a decoded frame and timestamp. This function should be called in a loop for
    *   fetching all the frames that are available for display.
    */
    cv::cuda::GpuMat GetFrame(int64_t* pTimestamp = nullptr);
    int64_t PeekTimestamp();

    /**
    *   @brief  This function allows app to set decoder reconfig params
    *   @param  pCropRect - cropping rectangle coordinates
    *   @param  pResizeDim - width and height of resized output
    */
    int setReconfigParams(const Rect * pCropRect, const Dim * pResizeDim);

    /**
    *   @brief  This function allows app to set operating point for AV1 SVC clips
    *   @param  opPoint - operating point of an AV1 scalable bitstream
    *   @param  bDispAllLayers - Output all decoded frames of an AV1 scalable bitstream
    */
    void SetOperatingPoint(const uint32_t opPoint, const bool bDispAllLayers) { m_nOperatingPoint = opPoint; m_bDispAllLayers = bDispAllLayers; }

    // start a timer
    void   startTimer() { m_stDecode_time.Start(); }

    // stop the timer
    double stopTimer() { return m_stDecode_time.Stop(); }

    void setDecoderSessionID(int sessionID) { decoderSessionID = sessionID; }
    int getDecoderSessionID() { return decoderSessionID; }

    // Session overhead refers to decoder initialization and deinitialization time
    static void addDecoderSessionOverHead(int sessionID, int64_t duration) { sessionOverHead[sessionID] += duration; }
    static int64_t getDecoderSessionOverHead(int sessionID) { return sessionOverHead[sessionID]; }

    int NumFrames();

private:
    int decoderSessionID; // Decoder session identifier. Used to gather session level stats.
    static std::map<int, int64_t> sessionOverHead; // Records session overhead of initialization+deinitialization time. Format is (thread id, duration)

    /**
    *   @brief  Callback function to be registered for getting a callback when decoding of sequence starts
    */
    static int CUDAAPI HandleVideoSequenceProc(void *pUserData, CUVIDEOFORMAT *pVideoFormat) { return ((NvDecoder *)pUserData)->HandleVideoSequence(pVideoFormat); }

    /**
    *   @brief  Callback function to be registered for getting a callback when a decoded frame is ready to be decoded
    */
    static int CUDAAPI HandlePictureDecodeProc(void *pUserData, CUVIDPICPARAMS *pPicParams) { return ((NvDecoder *)pUserData)->HandlePictureDecode(pPicParams); }

    /**
    *   @brief  Callback function to be registered for getting a callback when a decoded frame is available for display
    */
    static int CUDAAPI HandlePictureDisplayProc(void *pUserData, CUVIDPARSERDISPINFO *pDispInfo) { return ((NvDecoder *)pUserData)->HandlePictureDisplay(pDispInfo); }

    /**
    *   @brief  Callback function to be registered for getting a callback to get operating point when AV1 SVC sequence header start.
    */
    static int CUDAAPI HandleOperatingPointProc(void *pUserData, CUVIDOPERATINGPOINTINFO *pOPInfo) { return ((NvDecoder *)pUserData)->GetOperatingPoint(pOPInfo); }

    /**
    *   @brief  This function gets called when a sequence is ready to be decoded. The function also gets called
        when there is format change
    */
    int HandleVideoSequence(CUVIDEOFORMAT *pVideoFormat);

    /**
    *   @brief  This function gets called when a picture is ready to be decoded. cuvidDecodePicture is called from this function
    *   to decode the picture
    */
    int HandlePictureDecode(CUVIDPICPARAMS *pPicParams);

    /**
    *   @brief  This function gets called after a picture is decoded and available for display. Frames are fetched and stored in 
        internal buffer
    */
    int HandlePictureDisplay(CUVIDPARSERDISPINFO *pDispInfo);

    /**
    *   @brief  This function gets called when AV1 sequence encounter more than one operating points
    */
    int GetOperatingPoint(CUVIDOPERATINGPOINTINFO *pOPInfo);
    /**
    *   @brief  This function reconfigure decoder if there is a change in sequence params.
    */
    int ReconfigureDecoder(CUVIDEOFORMAT *pVideoFormat);

private:
    CUcontext m_cuContext = NULL;
    CUvideoctxlock m_ctxLock;
    CUvideoparser m_hParser = NULL;
    CUvideodecoder m_hDecoder = NULL;
    // dimension of the output
    unsigned int m_nWidth = 0, m_nLumaHeight = 0, m_nChromaHeight = 0;
    unsigned int m_nNumChromaPlanes = 0;
    // height of the mapped surface 
    int m_nSurfaceHeight = 0;
    int m_nSurfaceWidth = 0;
    cudaVideoCodec m_eCodec = cudaVideoCodec_NumCodecs;
    cudaVideoChromaFormat m_eChromaFormat = cudaVideoChromaFormat_420;
    cudaVideoSurfaceFormat m_eOutputFormat = cudaVideoSurfaceFormat_NV12;
    int m_nBitDepthMinus8 = 0;
    int m_nBPP = 1;
    CUVIDEOFORMAT m_videoFormat = {};
    ::Rect m_displayRect = {};

    struct gpuFrameStruct
    {
        cv::cuda::GpuMat frame;
        int64_t timeStamp;
    };

    std::mutex frameMtx;
    std::deque<gpuFrameStruct> frameQueue;
    
    int m_nDecodePicCnt = 0, m_nPicNumInDecodeOrder[32];
    bool m_bEndDecodeDone = false;
    
    int m_nFrameAlloc = 0;
    cudaStream_t m_cuvidStream = nullptr;
    bool m_bDeviceFramePitched = false;
    size_t m_nDeviceFramePitch = 0;
    ::Rect m_cropRect = {};
    Dim m_resizeDim = {};

    std::ostringstream m_videoInfo;
    unsigned int m_nMaxWidth = 0, m_nMaxHeight = 0;
    bool m_bReconfigExternal = false;
    bool m_bReconfigExtPPChange = false;
    StopWatch m_stDecode_time;

    unsigned int m_nOperatingPoint = 0;
    bool  m_bDispAllLayers = false;
    // In H.264, there is an inherent display latency for video contents
    // which do not have num_reorder_frames=0 in the VUI. This applies to
    // All-Intra and IPPP sequences as well. If the user wants zero display
    // latency for All-Intra and IPPP sequences, the below flag will enable
    // the display callback immediately after the decode callback.
    bool m_bForce_zero_latency = false;

    CUVIDPARSERPARAMS videoParserParameters = {};
};
