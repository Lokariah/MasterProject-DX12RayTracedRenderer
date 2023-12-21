#include "Timer.h"
#include <Windows.h>

Timer::Timer() : mSecondsPerCount(0.0), mFrameTime(-1.0), mBaseTime(0), mStopTime(0), mPausedTime(0), mPrevTime(0), mCurrTime(0), mPaused(false){
	__int64 countsPerSecond;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSecond);
	mSecondsPerCount = 1.0 / double(countsPerSecond);
}

void Timer::Tick()
{
	if (mPaused) {
		mFrameTime = 0.0;
		return;
	}

	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	mCurrTime = currTime;
	mFrameTime = (mCurrTime - mPrevTime) * mSecondsPerCount;
	mPrevTime = mCurrTime;
	if (mFrameTime < 0.0) mFrameTime = 0.0;
}

void Timer::Reset()
{
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	mBaseTime = currTime;
	mPrevTime = currTime;
	mStopTime = 0;
	mPaused = false;
}

void Timer::Start()
{
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
	if (mPaused) {
		mPausedTime += (startTime - mStopTime);
		mPrevTime = startTime;
		mStopTime = 0;
		mPaused = false;
	}
}

void Timer::Stop()
{
	if (!mPaused) {
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		mStopTime = currTime;
		mPaused = true;
	}
}

float Timer::TotalTime()const
{
	if (mPaused) return (float(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount));
	else return (float(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount));
}

float Timer::FrameTime()const
{
	return float(mFrameTime);
}
