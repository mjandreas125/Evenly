#pragma once
// The Rovno system effect: a post-mix audio processing object (endpoint effect) that runs the
// Rovno engine inside the Windows audio engine for one output device. Implements the APO
// interfaces directly (the SDK's base class needs ATL).
#include <windows.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>
#include <string>
#include "../dsp/Engine.h"

// {F9DCA572-B13F-43A6-B8EE-113925372DDC}
inline constexpr GUID kRovnoApoClsid = { 0xF9DCA572, 0xB13F, 0x43A6, { 0xB8, 0xEE, 0x11, 0x39, 0x25, 0x37, 0x2D, 0xDC } };
inline constexpr APO_FLAG kRovnoApoFlags = (APO_FLAG) (APO_FLAG_INPLACE | APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH);

class INonDelegatingUnknown
{
public:
    virtual HRESULT __stdcall NonDelegatingQueryInterface (const IID& iid, void** ppv) = 0;
    virtual ULONG __stdcall NonDelegatingAddRef() = 0;
    virtual ULONG __stdcall NonDelegatingRelease() = 0;
};

class RovnoApo : public IAudioProcessingObject, public IAudioProcessingObjectRT, public IAudioProcessingObjectConfiguration,
                 public IAudioSystemEffects, public INonDelegatingUnknown
{
public:
    explicit RovnoApo (IUnknown* outer);
    virtual ~RovnoApo();

    // IUnknown (delegating)
    HRESULT __stdcall QueryInterface (const IID& iid, void** ppv) override;
    ULONG __stdcall AddRef() override;
    ULONG __stdcall Release() override;

    // IAudioProcessingObject
    HRESULT __stdcall Reset() override;
    HRESULT __stdcall GetLatency (HNSTIME* pTime) override;
    HRESULT __stdcall GetRegistrationProperties (APO_REG_PROPERTIES** ppRegProps) override;
    HRESULT __stdcall Initialize (UINT32 cbDataSize, BYTE* pbyData) override;
    HRESULT __stdcall IsInputFormatSupported (IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported) override;
    HRESULT __stdcall IsOutputFormatSupported (IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported) override;
    HRESULT __stdcall GetInputChannelCount (UINT32* pu32ChannelCount) override;

    // IAudioProcessingObjectRT
    void __stdcall APOProcess (UINT32 nIn, APO_CONNECTION_PROPERTY** in, UINT32 nOut, APO_CONNECTION_PROPERTY** out) override;
    UINT32 __stdcall CalcInputFrames (UINT32 outputFrames) override { return outputFrames; }
    UINT32 __stdcall CalcOutputFrames (UINT32 inputFrames) override { return inputFrames; }

    // IAudioProcessingObjectConfiguration
    HRESULT __stdcall LockForProcess (UINT32 nIn, APO_CONNECTION_DESCRIPTOR** in, UINT32 nOut, APO_CONNECTION_DESCRIPTOR** out) override;
    HRESULT __stdcall UnlockForProcess() override;

    // INonDelegatingUnknown
    HRESULT __stdcall NonDelegatingQueryInterface (const IID& iid, void** ppv) override;
    ULONG __stdcall NonDelegatingAddRef() override;
    ULONG __stdcall NonDelegatingRelease() override;

    static long instances;
    static APO_REG_PROPERTIES registration();

private:
    HRESULT checkFormat (IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported);
    void openShared();
    void loadSavedParams();

    long refCount = 1;
    IUnknown* outer = nullptr;
    bool initialized = false, locked = false;
    std::wstring endpoint;                 // endpoint GUID, e.g. {C9E05C26-...}
    HANDLE mapping = nullptr;
    rv::SharedBlock* shared = nullptr;
    rv::Telemetry localTelemetry {};       // used when the shared block could not be opened
    rv::EngineParams params {};
    uint32_t paramsSource = 0;
    rv::Engine* engine = nullptr;          // created in LockForProcess
    UINT32 channels = 2;
    float frameRate = 48000;
};
