#ifndef IDLE_TRACKER_HPP
#define IDLE_TRACKER_HPP

#include <windows.h>

class IdleTracker {
private:
    DWORD dwThreshold; // Boşta sayılma eşiği (milisaniye cinsinden)

public:
    // Constructor: Saniye cinsinden eşik değer alır
    IdleTracker(DWORD secondsThreshold);

    // Kullanıcının boşta olup olmadığını kontrol eder
    bool isUserIdle();

    // Ne kadar süredir (milisaniye) boşta olduğunu döndürür
    ULONGLONG getIdleTimeMillis();
};

#endif