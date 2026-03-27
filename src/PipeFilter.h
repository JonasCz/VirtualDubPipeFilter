#ifndef PIPEFILTER_H
#define PIPEFILTER_H

#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vd2/VDXFrame/VideoFilter.h>
#include <vd2/VDXFrame/VideoFilterDialog.h>

struct PipeFilterConfig {
    std::string command;
    int lag = 0;
};

class FrameQueue {
public:
    void Push(std::vector<uint8_t>&& frame);
    std::vector<uint8_t> Pop();
    void Shutdown();
    void Reset();

private:
    std::queue<std::vector<uint8_t>> mQueue;
    std::mutex mMutex;
    std::condition_variable mCond;
    bool mDone = false;
};

class PipeFilterDialog : public VDXVideoFilterDialog {
public:
    PipeFilterDialog(PipeFilterConfig& config) : mConfig(config) {}
    bool Show(HWND parent);
    virtual INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    PipeFilterConfig& mConfig;
    PipeFilterConfig mOldConfig;
};

class PipeFilter : public VDXVideoFilter {
public:
    PipeFilter();
    PipeFilter(const PipeFilter& other);
    ~PipeFilter();

    virtual uint32 GetParams();
    virtual void Start();
    virtual void Run();
    virtual void End();
    virtual bool Configure(VDXHWND hwnd);
    virtual void GetSettingString(char *buf, int maxlen);
    virtual void GetScriptString(char *buf, int maxlen);

    VDXVF_DECLARE_SCRIPT_METHODS();

private:
    void ScriptConfig(IVDXScriptInterpreter *isi, const VDXScriptValue *argv, int argc);

    std::string SubstituteCommand(int w, int h, uint32 fpsNum, uint32 fpsDen) const;
    void LaunchProcess(const std::string& cmdline, int w, int h);
    void StopProcess();
    void WriteFrame(const void *data, ptrdiff_t pitch, int w, int h);
    static DWORD WINAPI ReaderThreadProc(LPVOID param);
    void ReaderLoop(int w, int h);

    PipeFilterConfig mConfig;

    HANDLE mChildStdinWrite = INVALID_HANDLE_VALUE;
    HANDLE mChildStdoutRead = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION mProcInfo = {};
    HANDLE mReaderThread = NULL;

    FrameQueue mFrameQueue;
    int mFramesFed = 0;
    int mWidth = 0;
    int mHeight = 0;
};

extern VDXFilterDefinition filterDef_pipeFilter;

#endif
