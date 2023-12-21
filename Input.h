#pragma once
#include "Utility.h"

#ifndef _Input_H_Defined_
#define _Input_H_Defined_


namespace Dx12MasterProject {
	enum class KeyState {
		Neutral,
		Pressed,
		Held
	};

	enum class KeyValue {
		MouseLeftButton = 0x01,
		MouseRightButton = 0x02,
		MouseMiddleButton = 0x04,
		MouseX1Button = 0x05,
		MouseX2Button = 0x06,
		KeyBackspace = 0x08,

		KeyTab = 0x09,
		KeyClear = 0x0C,
		KeyEnter = 0x0D,
		KeyShift = 0x10,
		KeyCtrl = 0x11,
		KeyAlt = 0x12,
		KeyPause = 0x13,
		KeyCapsLock = 0x14,
		KeyEscape = 0x1B,
		KeySpaceBar = 0x20,
		KeyPageUp = 0x21,
		KeyPageDown = 0x22,
		KeyEnd = 0x23,
		KeyHome = 0x24,
		KeyArrowLeft = 0x25,
		KeyArrowUp = 0x26,
		KeyArrowRight = 0x27,
		KeyArrowDown = 0x28,
		KeySelect = 0x29,
		KeyPrint = 0x2A,
		KeyExecute = 0x2B,
		KeyPrintScreen = 0x2C,
		KeyInsert = 0x2D,
		KeyDelete = 0x2E,
		KeyHelp = 0x2F,

		Key0 = 0x30,
		Key1 = 0x31,
		Key2 = 0x32,
		Key3 = 0x33,
		Key4 = 0x34,
		Key5 = 0x35,
		Key6 = 0x36,
		Key7 = 0x37,
		Key8 = 0x38,
		Key9 = 0x39,

		KeyA = 0x41,
		KeyB = 0x42,
		KeyC = 0x43,
		KeyD = 0x44,
		KeyE = 0x45,
		KeyF = 0x46,
		KeyG = 0x47,
		KeyH = 0x48,
		KeyI = 0x49,
		KeyJ = 0x4A,
		KeyK = 0x4B,
		KeyL = 0x4C,
		KeyM = 0x4D,
		KeyN = 0x4E,
		KeyO = 0x4F,
		KeyP = 0x50,
		KeyQ = 0x51,
		KeyR = 0x52,
		KeyS = 0x53,
		KeyT = 0x54,
		KeyU = 0x55,
		KeyV = 0x56,
		KeyW = 0x57,
		KeyX = 0x58,
		KeyY = 0x59,
		KeyZ = 0x5A,

		KeyLeftWin = 0x5B,
		KeyRightWin = 0x5C,
		KeyApp = 0x5D,
		KeySleep = 0x5F,

		KeyNum0 = 0x60,
		KeyNum1 = 0x61,
		KeyNum2 = 0x62,
		KeyNum3 = 0x63,
		KeyNum4 = 0x64,
		KeyNum5 = 0x65,
		KeyNum6 = 0x66,
		KeyNum7 = 0x67,
		KeyNum8 = 0x68,
		KeyNum9 = 0x69,
		KeyMultiply = 0x6A,
		KeyAdd = 0x6B,
		KeySeperator = 0x6C,
		KeySubtract = 0x6D,
		KeyDecimal = 0x6E,
		KeyDivide = 0x6F,

		KeyF1 = 0x70,
		KeyF2 = 0x71,
		KeyF3 = 0x72,
		KeyF4 = 0x73,
		KeyF5 = 0x74,
		KeyF6 = 0x75,
		KeyF7 = 0x76,
		KeyF8 = 0x77,
		KeyF9 = 0x78,
		KeyF10 = 0x79,
		KeyF11 = 0x7A,
		KeyF12 = 0x7B,
		KeyF13 = 0x7C,
		KeyF14 = 0x7D,
		KeyF15 = 0x7E,
		KeyF16 = 0x7F,
		KeyF17 = 0x80,
		KeyF18 = 0x81,
		KeyF19 = 0x82,
		KeyF20 = 0x83,
		KeyF21 = 0x84,
		KeyF22 = 0x85,
		KeyF23 = 0x86,
		KeyF24 = 0x87,

		KeyNumLock = 0x90,
		KeyScrollLock = 0x91,
		KeyShiftLeft = 0xA0,
		KeyShiftRight = 0xA1,
		KeyCtrlLeft = 0xA2,
		KeyCtrlRight = 0xA3,
		KeyAltLeft = 0xA4,
		KeyAltRight = 0xA5,

		MaxKeyAmount = 0xFF
	};

	KeyState gKeyStates[int(KeyValue::MaxKeyAmount)];
	int gMouseX, gMouseY;

	//Functions
	void InitInput() {
		gMouseX = gMouseY = 0;

		for (int i = 0; i < int(KeyValue::MaxKeyAmount); i++) {
			gKeyStates[i] = KeyState::Neutral;
		}
	}
	
	//Windows event handling
	void KeyPressedEvent(KeyValue key) {
		if (gKeyStates[int(key)] == KeyState::Neutral) gKeyStates[int(key)] = KeyState::Pressed;
		else gKeyStates[int(key)] = KeyState::Held;
	}
	void KeyReleasedEvent(KeyValue key) { gKeyStates[int(key)] = KeyState::Neutral; }
	void MouseMovementEvent(int x, int y) {
		gMouseX = x; 
		gMouseY = y;
	}

	//Input functions 
	bool KeyPressed(KeyValue key) {
		if (gKeyStates[int(key)] == KeyState::Neutral) {
			gKeyStates[int(key)] = KeyState::Held;
			return true;
		}
		else return false;
	}
	bool KeyHeld(KeyValue key) {
		if (gKeyStates[int(key)] == KeyState::Neutral) return false;
		else {
			gKeyStates[int(key)] = KeyState::Held;
			return true;
		}
	}
	int GetMouseX() { return gMouseX; }
	int GetMouseY() { return gMouseY; }

}
#endif // !_Input_H_Defined_
