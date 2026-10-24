#pragma once
// TCore logging and crash logs (Sections 18, 19): leveled log lines, a bounded
// ring of recent lines for crash reports, an optional file sink, and a
// fatal-signal / std::terminate handler that dumps the ring to disk.
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace lt::core {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal };

constexpr std::string_view levelName(LogLevel l) noexcept {
    switch (l) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    }
    return "?";
}

class Logger {
public:
    explicit Logger(std::size_t ringCapacity = 256) : capacity_(ringCapacity ? ringCapacity : 1) {}
    ~Logger() { closeFile(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void setMinLevel(LogLevel l) noexcept { min_ = l; }

    bool openFile(const std::filesystem::path& p) {
        std::lock_guard<std::mutex> lk(mu_);
        if (file_) std::fclose(file_);
        file_ = std::fopen(p.string().c_str(), "ab");
        return file_ != nullptr;
    }

    void closeFile() {
        std::lock_guard<std::mutex> lk(mu_);
        if (file_) std::fclose(file_);
        file_ = nullptr;
    }

    void write(LogLevel l, std::string_view msg) {
        if (l < min_) return;
        std::string line;
        line.reserve(msg.size() + 8);
        line += '[';
        line += levelName(l);
        line += "] ";
        line += msg;
        std::lock_guard<std::mutex> lk(mu_);
        if (file_) {
            std::fwrite(line.data(), 1, line.size(), file_);
            std::fputc('\n', file_);
            if (l >= LogLevel::Error) std::fflush(file_);
        }
        ring_.push_back(std::move(line));
        if (ring_.size() > capacity_) ring_.pop_front();
        ++total_;
    }

    void trace(std::string_view m) { write(LogLevel::Trace, m); }
    void debug(std::string_view m) { write(LogLevel::Debug, m); }
    void info(std::string_view m) { write(LogLevel::Info, m); }
    void warn(std::string_view m) { write(LogLevel::Warn, m); }
    void error(std::string_view m) { write(LogLevel::Error, m); }
    void fatal(std::string_view m) { write(LogLevel::Fatal, m); }

    std::vector<std::string> recent() const {
        std::lock_guard<std::mutex> lk(mu_);
        return std::vector<std::string>(ring_.begin(), ring_.end());
    }

    std::uint64_t totalLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return total_;
    }

    // Never blocks: a crash may happen while another thread holds the lock.
    bool writeCrashReport(const std::filesystem::path& p, std::string_view reason) const {
        std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return false;
        std::fputs("ThaumtMourge crash report\nreason: ", f);
        std::fwrite(reason.data(), 1, reason.size(), f);
        std::fputs("\nrecent log:\n", f);
        for (const auto& line : ring_) {
            std::fwrite(line.data(), 1, line.size(), f);
            std::fputc('\n', f);
        }
        if (file_) std::fflush(file_);
        return std::fclose(f) == 0;
    }

private:
    std::size_t capacity_;
    LogLevel min_ = LogLevel::Info;
    mutable std::mutex mu_;
    std::deque<std::string> ring_;
    std::uint64_t total_ = 0;
    std::FILE* file_ = nullptr;
};

namespace detail {
inline Logger* g_crashLogger = nullptr;
inline std::filesystem::path g_crashPath;
inline std::terminate_handler g_prevTerminate = nullptr;

inline const char* signalName(int sig) noexcept {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    default: return "signal";
    }
}

inline void onFatalSignal(int sig) {
    if (Logger* log = g_crashLogger) {
        g_crashLogger = nullptr;
        log->writeCrashReport(g_crashPath, signalName(sig));
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

inline void onTerminate() {
    if (Logger* log = g_crashLogger) {
        g_crashLogger = nullptr;
        log->writeCrashReport(g_crashPath, "std::terminate");
    }
    std::abort();
}
} // namespace detail

inline void installCrashHandler(Logger& log, const std::filesystem::path& crashFile) {
    detail::g_crashLogger = &log;
    detail::g_crashPath = crashFile;
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(sig, detail::onFatalSignal);
    detail::g_prevTerminate = std::set_terminate(detail::onTerminate);
}

inline bool crashHandlerInstalled() noexcept { return detail::g_crashLogger != nullptr; }

inline void uninstallCrashHandler() {
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(sig, SIG_DFL);
    std::set_terminate(detail::g_prevTerminate);
    detail::g_crashLogger = nullptr;
}

} // namespace lt::core
