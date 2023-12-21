#include "Utility.h"

std::string wstring_2_string(const std::wstring& ws)
{
	int count = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), ws.length(), NULL, 0, NULL, NULL);
	std::string s(count, 0);
	WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], count, NULL, NULL);
	return s;
}