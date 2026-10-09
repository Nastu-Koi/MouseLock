#include "../hold_exit.h"
#include <cassert>
#include <iostream>

int main()
{
    HoldToExit hold;
    assert(!hold.Update(true, 0, 2000));
    assert(!hold.Update(true, 500, 2000)); // keyboard auto-repeat
    assert(!hold.Update(true, 1999, 2000));
    assert(hold.Elapsed(1999, 2000) == 1999);
    assert(hold.Update(true, 2000, 2000));
    assert(!hold.Update(true, 9000, 2000)); // only one exit request
    assert(!hold.Update(false, 9100, 2000));
    assert(!hold.Holding());

    assert(!hold.Update(true, 10000, 2000));
    assert(!hold.Update(false, 11999, 2000)); // releasing cancels
    assert(!hold.Update(true, 12000, 2000));
    assert(!hold.Update(true, 13999, 2000)); // short holds never accumulate
    assert(hold.Update(true, 14000, 2000));

    hold.RequireRelease(); // editing a shortcut or returning from sleep
    assert(!hold.Update(true, 20000, 1000));
    assert(!hold.Update(true, 40000, 1000));
    assert(!hold.Holding());
    assert(!hold.Update(false, 41000, 1000));
    assert(!hold.Update(true, 42000, 1000));
    assert(hold.Update(true, 43000, 1000));

    hold.Update(false, 44000, 3000);
    assert(!hold.Update(true, 45000, 3000));
    assert(!hold.Update(true, 10, 3000)); // clock discontinuity
    assert(!hold.Update(true, 3009, 3000));
    assert(hold.Update(true, 3010, 3000));
    std::cout << "Hold-to-exit: all gesture tests passed\n";
}
