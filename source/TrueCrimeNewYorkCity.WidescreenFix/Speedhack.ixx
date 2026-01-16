void SynchronizeTimeBase(float newMultiplier)
{
    gtcLock.lock();
    qpcLock.lock();
    tgtLock.lock();

    float oldMultiplier = lastMultiplier;

    // Snapshot current real times
    DWORD now32 = shGetTickCount ? shGetTickCount.unsafe_stdcall<DWORD>() : GetTickCount();
    ULONGLONG now64 = shGetTickCount64 ? shGetTickCount64.unsafe_stdcall<ULONGLONG>() : GetTickCount64();
    LARGE_INTEGER qpcNow{};
    if (shQueryPerformanceCounter)
        shQueryPerformanceCounter.unsafe_stdcall<BOOL>(&qpcNow);
    else
        QueryPerformanceCounter(&qpcNow);
    DWORD tgtNow = shTimeGetTime ? shTimeGetTime.unsafe_stdcall<DWORD>() : now32;

    // Calculate elapsed game time with the old multiplier and bake it into the offset
    initialOffset += DWORD((now32 - initialTime) * oldMultiplier);
    initialOffset64 += ULONGLONG((now64 - initialTime64) * oldMultiplier);
    initialOffsetQPC += LONGLONG((qpcNow.QuadPart - initialTimeQPC) * oldMultiplier);
    initialOffsetTGT += DWORD((tgtNow - initialTimeTGT) * oldMultiplier);

    // Reset the start time anchor to "now"
    initialTime = now32;
    initialTime64 = now64;
    initialTimeQPC = qpcNow.QuadPart;
    initialTimeTGT = tgtNow;

    lastMultiplier = newMultiplier;

    tgtLock.unlock();
    qpcLock.unlock();
    gtcLock.unlock();
}

export float GetSpeedhackMultiplier()
{
    bool loadingActive = false;
    bool cutsceneActive = false;
    if (b60FpsCutscenes && bCutscene)
        cutsceneActive = *bCutscene;
    if (b60FpsCutscenes && nLoading)
    bool loadingActive = (nLoading && *nLoading);

    speedMultiplier = (loadingActive || cutsceneActive)
        ? fCutsceneSpeedFactor
        : fGameSpeedFactor;

    if (speedMultiplier != lastMultiplier)
        SynchronizeTimeBase(speedMultiplier);

    return speedMultiplier;
}
