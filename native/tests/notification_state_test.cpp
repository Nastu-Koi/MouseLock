#include "../notification_state.h"
#include <cassert>
#include <iostream>

int main()
{
    LockNoticeState paused{LockNoticeStatus::Paused, false, L"DISPLAY1", L""};
    for (int tick=0; tick<1000; ++tick) assert(NoticeChange(paused,paused)==LockNoticeChange::None);
    auto locked=paused; locked.status=LockNoticeStatus::Locked;
    assert(NoticeChange(paused,locked)==LockNoticeChange::Status);
    assert(NoticeChange(locked,paused)==LockNoticeChange::Status);
    auto temporary=locked; temporary.status=LockNoticeStatus::Temporary;
    assert(NoticeChange(locked,temporary)==LockNoticeChange::Status);
    assert(NoticeChange(temporary,locked)==LockNoticeChange::Status);
    auto screen=locked; screen.monitor=L"DISPLAY2";
    assert(NoticeChange(locked,screen)==LockNoticeChange::Monitor);
    auto game=paused; game.gameOnly=true; game.status=LockNoticeStatus::Waiting;
    assert(NoticeChange(paused,game)==LockNoticeChange::Mode);
    auto playing=game; playing.status=LockNoticeStatus::Locked;
    assert(NoticeChange(game,playing)==LockNoticeChange::Status);
    assert(NoticeChange(playing,game)==LockNoticeChange::Status);
    auto failed=playing; failed.status=LockNoticeStatus::Error; failed.error=L"display disconnected";
    assert(NoticeChange(playing,failed)==LockNoticeChange::Status);
    assert(NoticeChange(failed,failed)==LockNoticeChange::None);
    auto denied=failed; denied.error=L"access denied";
    assert(NoticeChange(failed,denied)==LockNoticeChange::Status);
    std::cout << "Notifications: transitions, game focus, target changes and deduplication passed\n";
}
