#pragma once
class Timer
{
public:
	Timer();

	void Tick();
	void Reset();
	void Start();
	void Stop();

	float TotalTime()const;
	float FrameTime()const;

private:
	bool mPaused;
	double mSecondsPerCount;
	double mFrameTime;
	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mStopTime;
	__int64 mPrevTime;
	__int64 mCurrTime;

};

