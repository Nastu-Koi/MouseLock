#pragma once
#include <string>

enum class LockNoticeStatus { Paused, Locked, Temporary, Waiting, Error };
enum class LockNoticeChange { None, Status, Mode, Monitor };

struct LockNoticeState
{
    LockNoticeStatus status = LockNoticeStatus::Paused;
    bool gameOnly = false;
    std::wstring monitor;
    std::wstring error;
};

// Timer-based lock validation is frequent; only actual transitions produce a toast.
inline LockNoticeChange NoticeChange(const LockNoticeState& before, const LockNoticeState& after)
{
    if (after.status == LockNoticeStatus::Error &&
        (before.status != after.status || before.error != after.error)) return LockNoticeChange::Status;
    if (before.gameOnly != after.gameOnly) return LockNoticeChange::Mode;
    if (before.status != after.status) return LockNoticeChange::Status;
    if (before.monitor != after.monitor) return LockNoticeChange::Monitor;
    return LockNoticeChange::None;
}
