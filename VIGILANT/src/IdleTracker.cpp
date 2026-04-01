#include "IdleTracker.hpp"

IdleTracker::IdleTracker(DWORD secondsThreshold) {
    // Saniyeyi milisaniyeye çevirip saklıyoruz
    this->dwThreshold = secondsThreshold * 1000;
}

ULONGLONG IdleTracker::getIdleTimeMillis() {
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO); // Windows'a yapının boyutunu bildirmek şart

    // GetLastInputInfo başarılı olursa hesaplama yap
    if (GetLastInputInfo(&lii)) {
        ULONGLONG currentTime = GetTickCount64();
        ULONGLONG lastInputTime = (ULONGLONG)lii.dwTime;

        // Sistem çalışma süresi dwTime'dan (32-bit) büyük olduğu için farkı buluyoruz
        if (currentTime > lastInputTime) {
            return currentTime - lastInputTime;
        }
    }
    return 0;
}

bool IdleTracker::isUserIdle() {
    // Geçen boşta kalma süresi eşik değerden büyükse true döner
    return getIdleTimeMillis() >= dwThreshold;
}