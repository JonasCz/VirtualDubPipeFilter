#include "PipeFilter.h"
#include "resource.h"
#include <commctrl.h>
#include <algorithm>

///////////////////////////////////////////////////////////////////////////////
// FrameQueue
///////////////////////////////////////////////////////////////////////////////

void FrameQueue::Push(std::vector<uint8_t>&& frame) {
    std::lock_guard<std::mutex> lock(mMutex);
    mQueue.push(std::move(frame));
    mCond.notify_one();
}

std::vector<uint8_t> FrameQueue::Pop() {
    std::unique_lock<std::mutex> lock(mMutex);
    mCond.wait(lock, [&] { return !mQueue.empty() || mDone; });
    if (mQueue.empty())
        return {};
    auto frame = std::move(mQueue.front());
    mQueue.pop();
    return frame;
}

void FrameQueue::Shutdown() {
    std::lock_guard<std::mutex> lock(mMutex);
    mDone = true;
    mCond.notify_all();
}

void FrameQueue::Reset() {
    std::lock_guard<std::mutex> lock(mMutex);
    std::queue<std::vector<uint8_t>> empty;
    mQueue.swap(empty);
    mDone = false;
}

///////////////////////////////////////////////////////////////////////////////
// PipeFilterDialog
///////////////////////////////////////////////////////////////////////////////

bool PipeFilterDialog::Show(HWND parent) {
    return 0 != VDXVideoFilterDialog::Show(NULL, MAKEINTRESOURCE(IDD_FILTER_PIPECONFIG), parent);
}

INT_PTR PipeFilterDialog::DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        mOldConfig = mConfig;
        SetDlgItemTextA(mhdlg, IDC_COMMAND, mConfig.command.c_str());
        SendDlgItemMessage(mhdlg, IDC_LAG_SPIN, UDM_SETRANGE32, 1, 120);
        SetDlgItemInt(mhdlg, IDC_LAG, mConfig.lag, FALSE);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            char buf[4096] = {};
            GetDlgItemTextA(mhdlg, IDC_COMMAND, buf, sizeof(buf));
            mConfig.command = buf;
            mConfig.lag = (std::max)(1, (int)GetDlgItemInt(mhdlg, IDC_LAG, NULL, FALSE));
            EndDialog(mhdlg, TRUE);
            return TRUE;
        }
        case IDCANCEL:
            mConfig = mOldConfig;
            EndDialog(mhdlg, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
// PipeFilter
///////////////////////////////////////////////////////////////////////////////

PipeFilter::PipeFilter() {}
PipeFilter::PipeFilter(const PipeFilter& other) : mConfig(other.mConfig) {}
PipeFilter::~PipeFilter() {}

VDXVF_BEGIN_SCRIPT_METHODS(PipeFilter)
VDXVF_DEFINE_SCRIPT_METHOD(PipeFilter, ScriptConfig, "si")
VDXVF_END_SCRIPT_METHODS()

uint32 PipeFilter::GetParams() {
    extern int g_VFVAPIVersion;
    if (g_VFVAPIVersion >= 12) {
        switch (fa->src.mpPixmapLayout->format) {
        case nsVDXPixmap::kPixFormat_XRGB8888:
            break;
        default:
            return FILTERPARAM_NOT_SUPPORTED;
        }
    }
    fa->dst.offset = fa->src.offset;
    return FILTERPARAM_SWAP_BUFFERS | FILTERPARAM_SUPPORTS_ALTFORMATS | FILTERPARAM_HAS_LAG(mConfig.lag);
}

void PipeFilter::Start() {
    mWidth = fa->src.w;
    mHeight = fa->src.h;
    mFramesFed = 0;
    mFrameQueue.Reset();

    std::string cmdline = SubstituteCommand(mWidth, mHeight, fa->src.mFrameRateHi, fa->src.mFrameRateLo);
    LaunchProcess(cmdline, mWidth, mHeight);
}

void PipeFilter::Run() {
    const int w = mWidth;
    const int h = mHeight;
    const int rowBytes = w * 4;

    WriteFrame(fa->src.data, fa->src.pitch, w, h);
    mFramesFed++;

    if (mFramesFed <= mConfig.lag) {
        // External process is still buffering; output a black frame.
        uint8_t *dst = (uint8_t *)fa->dst.data;
        ptrdiff_t dstPitch = fa->dst.pitch;
        for (int y = 0; y < h; y++) {
            memset(dst + (ptrdiff_t)y * dstPitch, 0, rowBytes);
        }
        return;
    }

    std::vector<uint8_t> frame = mFrameQueue.Pop();
    if (frame.empty()) {
        ff->Except("Pipe filter: external process closed stdout prematurely.");
        return;
    }

    uint8_t *dst = (uint8_t *)fa->dst.data;
    ptrdiff_t dstPitch = fa->dst.pitch;
    // Frame data is top-down; VDX bitmap data ptr is bottom-left with pitch going up.
    for (int y = 0; y < h; y++) {
        memcpy(dst + (ptrdiff_t)(h - 1 - y) * dstPitch, frame.data() + (size_t)y * rowBytes, rowBytes);
    }
}

void PipeFilter::End() {
    StopProcess();
}

bool PipeFilter::Configure(VDXHWND hwnd) {
    PipeFilterDialog dlg(mConfig);
    return dlg.Show((HWND)hwnd);
}

void PipeFilter::GetSettingString(char *buf, int maxlen) {
    if (mConfig.command.empty()) {
        SafePrintf(buf, maxlen, " (no command)");
    } else {
        std::string display = mConfig.command;
        if (display.size() > 40)
            display = display.substr(0, 37) + "...";
        SafePrintf(buf, maxlen, " (%s, lag=%d)", display.c_str(), mConfig.lag);
    }
}

void PipeFilter::GetScriptString(char *buf, int maxlen) {
    // Escape backslashes and quotes for the script string.
    std::string escaped;
    for (char c : mConfig.command) {
        if (c == '\\' || c == '"')
            escaped += '\\';
        escaped += c;
    }
    SafePrintf(buf, maxlen, "Config(\"%s\", %d)", escaped.c_str(), mConfig.lag);
}

void PipeFilter::ScriptConfig(IVDXScriptInterpreter *isi, const VDXScriptValue *argv, int argc) {
    mConfig.command = *argv[0].asString();
    mConfig.lag = (std::max)(1, argv[1].asInt());
}

///////////////////////////////////////////////////////////////////////////////
// Process / pipe management
///////////////////////////////////////////////////////////////////////////////

static void ReplaceAll(std::string& s, const char* token, const std::string& value) {
    size_t len = strlen(token);
    for (size_t pos = 0; (pos = s.find(token, pos)) != std::string::npos;)
        s.replace(pos, len, value);
}

std::string PipeFilter::SubstituteCommand(int w, int h, uint32 fpsNum, uint32 fpsDen) const {
    std::string cmd = mConfig.command;

    ReplaceAll(cmd, "%(width)",  std::to_string(w));
    ReplaceAll(cmd, "%(height)", std::to_string(h));
    ReplaceAll(cmd, "%(fpsnum)", std::to_string(fpsNum));
    ReplaceAll(cmd, "%(fpsden)", std::to_string(fpsDen));

    if (cmd.find("%(fps)") != std::string::npos) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.6g", fpsDen ? (double)fpsNum / fpsDen : 0.0);
        ReplaceAll(cmd, "%(fps)", buf);
    }

    return cmd;
}

void PipeFilter::LaunchProcess(const std::string& cmdline, int w, int h) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdinRead = INVALID_HANDLE_VALUE;
    HANDLE hStdoutWrite = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&hStdinRead, &mChildStdinWrite, &sa, 0))
        throw std::runtime_error("Failed to create stdin pipe");
    SetHandleInformation(mChildStdinWrite, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&mChildStdoutRead, &hStdoutWrite, &sa, 0)) {
        CloseHandle(hStdinRead);
        CloseHandle(mChildStdinWrite);
        mChildStdinWrite = INVALID_HANDLE_VALUE;
        throw std::runtime_error("Failed to create stdout pipe");
    }
    SetHandleInformation(mChildStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    std::vector<char> cmdBuf(cmdline.begin(), cmdline.end());
    cmdBuf.push_back(0);

    BOOL ok = CreateProcessA(
        NULL, cmdBuf.data(), NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &mProcInfo);

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);

    if (!ok) {
        CloseHandle(mChildStdinWrite);
        CloseHandle(mChildStdoutRead);
        mChildStdinWrite = INVALID_HANDLE_VALUE;
        mChildStdoutRead = INVALID_HANDLE_VALUE;
        throw std::runtime_error("Failed to launch external process");
    }

    mReaderThread = CreateThread(NULL, 0, ReaderThreadProc, this, 0, NULL);
    if (!mReaderThread)
        throw std::runtime_error("Failed to create reader thread");
}

void PipeFilter::StopProcess() {
    if (mChildStdinWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(mChildStdinWrite);
        mChildStdinWrite = INVALID_HANDLE_VALUE;
    }

    mFrameQueue.Shutdown();

    if (mReaderThread) {
        WaitForSingleObject(mReaderThread, 5000);
        CloseHandle(mReaderThread);
        mReaderThread = NULL;
    }

    if (mChildStdoutRead != INVALID_HANDLE_VALUE) {
        CloseHandle(mChildStdoutRead);
        mChildStdoutRead = INVALID_HANDLE_VALUE;
    }

    if (mProcInfo.hProcess) {
        WaitForSingleObject(mProcInfo.hProcess, 3000);
        TerminateProcess(mProcInfo.hProcess, 1);
        CloseHandle(mProcInfo.hProcess);
        CloseHandle(mProcInfo.hThread);
        mProcInfo = {};
    }
}

void PipeFilter::WriteFrame(const void *data, ptrdiff_t pitch, int w, int h) {
    const int rowBytes = w * 4;
    const uint8_t *src = (const uint8_t *)data;

    // VDX bitmap: data ptr at bottom-left, pitch positive upward.
    // Send top-down: iterate from top row (y = h-1) to bottom row (y = 0).
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t *row = src + (ptrdiff_t)y * pitch;
        DWORD written = 0;
        DWORD toWrite = (DWORD)rowBytes;
        const uint8_t *p = row;
        while (toWrite > 0) {
            if (!WriteFile(mChildStdinWrite, p, toWrite, &written, NULL))
                return;
            p += written;
            toWrite -= written;
        }
    }
}

DWORD WINAPI PipeFilter::ReaderThreadProc(LPVOID param) {
    PipeFilter *self = (PipeFilter *)param;
    self->ReaderLoop(self->mWidth, self->mHeight);
    return 0;
}

void PipeFilter::ReaderLoop(int w, int h) {
    const size_t frameSize = (size_t)w * h * 4;
    std::vector<uint8_t> frameBuf(frameSize);

    while (true) {
        size_t totalRead = 0;
        while (totalRead < frameSize) {
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(mChildStdoutRead, frameBuf.data() + totalRead,
                               (DWORD)(frameSize - totalRead), &bytesRead, NULL);
            if (!ok || bytesRead == 0)
                return;
            totalRead += bytesRead;
        }
        mFrameQueue.Push(std::vector<uint8_t>(frameBuf.begin(), frameBuf.end()));
    }
}

///////////////////////////////////////////////////////////////////////////////
// Filter definition
///////////////////////////////////////////////////////////////////////////////

extern VDXFilterDefinition filterDef_pipeFilter =
    VDXVideoFilterDefinition<PipeFilter>(
        "Jonas Czech",
        "External pipe",
        "Sends video to an external command via stdin and receives processed video via stdout.");
