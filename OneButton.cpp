// -----
// OneButton.cpp - Library for detecting button clicks, doubleclicks and long press pattern on a single button.
// This class is implemented for use with the Arduino environment.
// Copyright (c) by Matthias Hertel, http://www.mathertel.de
// This work is licensed under a BSD style license. See http://www.mathertel.de/License.aspx
// More information on: http://www.mathertel.de/Arduino
// -----
// Changelog: see OneButton.h
// -----

#include "OneButton.h"

// ----- Initialization and Default Values -----

OneButton::OneButton(int pin, int activeLow)
{
  pinMode(pin, INPUT);      // sets the MenuPin as input
  _pin = pin;

  _timeoutTicks = 300;        // number of millisec that have to pass by before a pattern end is detected.
  _longPressTicks = 700;       // number of millisec that have to pass by before a long button press is detected.
 
  _state = 0; // starting with state 0: waiting for button to be pressed
  _isLongPressed = false;  // Keep track of long press state

  if (activeLow) {
    // button connects ground to the pin when pressed.
    _buttonReleased = HIGH; // notPressed
    _buttonPressed = LOW;
    digitalWrite(pin, HIGH);   // turn on pullUp resistor

  } else {
    // button connects VCC to the pin when pressed.
    _buttonReleased = LOW;
    _buttonPressed = HIGH;
  } // if

  _clickFunc = NULL;
  _doubleClickFunc = NULL;
  _pressFunc = NULL;
  _longPressStartFunc = NULL;
  _longPressStopFunc = NULL;
  _duringLongPressFunc = NULL;
  _duringLongPressFunc = NULL;
  _patternStartFunc = NULL;
  _patternEndFunc = NULL;
} // OneButton


// explicitly set the number of millisec that have to pass by before a click timeout is detected.
void OneButton::setClickTicks(int ticks) { 
  _timeoutTicks = ticks;
} // setClickTicks


// explicitly set the number of millisec that have to pass by before a long button press is detected.
void OneButton::setPressTicks(int ticks) {
  _longPressTicks = ticks;
} // setPressTicks


// save function for click event
void OneButton::attachClick(callbackFunction newFunction)
{
  _clickFunc = newFunction;
} // attachClick


// save function for doubleClick event
void OneButton::attachDoubleClick(callbackFunction newFunction)
{
  _doubleClickFunc = newFunction;
} // attachDoubleClick


// save function for press event
// DEPRECATED, is replaced by attachLongPressStart, attachLongPressStop, attachDuringLongPress, 
void OneButton::attachPress(callbackFunction newFunction)
{
  _pressFunc = newFunction;
} // attachPress

// save function for longPressStart event
void OneButton::attachLongPressStart(callbackFunction newFunction)
{
  _longPressStartFunc = newFunction;
} // attachLongPressStart

// save function for longPressStop event
void OneButton::attachLongPressStop(callbackFunction newFunction)
{
  _longPressStopFunc = newFunction;
} // attachLongPressStop

// save function for during longPress event
void OneButton::attachDuringLongPress(callbackFunction newFunction)
{
  _longPressReTicks = 0;
  _duringLongPressFunc = newFunction;
} // attachDuringLongPress

// save function for during longPress event w/ repeat time
void OneButton::attachDuringLongPress(callbackFunction newFunction, int repeatMS)
{
  _longPressReTicks = repeatMS;
  _duringLongPressFunc = newFunction;
} // attachDuringLongPress

// save function for during patternStart event
void OneButton::attachPatternStart(callbackFunction newFunction)
{
  _patternStartFunc = newFunction;
} // attachPatternStart

// save function for during patternEnd event
void OneButton::attachPatternEnd(callbackFunction newFunction)
{
  _patternEndFunc = newFunction;
} // attachPatternEnd

// function to get the current long pressed state
bool OneButton::isLongPressed(){
  return _isLongPressed;
}

// function to get the current current pattern length
int OneButton::getPatternLength(){
  return _patternLength;
}

// function to get the current current pattern string. C = Click, L = LongPress
String OneButton::getPattern(){
  return _pattern;
}

void OneButton::tick(void)
{
  // Detect the input information 
  int buttonLevel = digitalRead(_pin); // current button signal.
  unsigned long now = millis(); // current (relative) time in msecs.

  // Implementation of the state machine
  if (_state == 0) { // waiting for button pin being pressed.
    if (buttonLevel == _buttonPressed) {
      _state = 1; // step to state 1
      _startTime = now; // remember starting time
    } // if
    
  } else if (_state == 1) { // waiting for debounce
    if ((buttonLevel == _buttonPressed) && (now > _startTime + _debounceTicks)) {
      if (_pressFunc) _pressFunc();
      _state = 2;
 
    } else if (buttonLevel == _buttonReleased) {
      // button was released to quickly so I assume some debouncing.
      _state = 0; // restart
    }
    
  } else if (_state == 2) { // waiting for button pin being released.
    if (buttonLevel == _buttonReleased) {
      _releaseTime = now;
      _pattern = _pattern + "C";
      _patternLength += 1;
      
      if (_patternLength == 1) { // check for pattern start
        if (_patternStartFunc) _patternStartFunc();
      }
      if (_pattern == "CC") { // check for double click
        if (_doubleClickFunc) _doubleClickFunc();
      }
      if (_clickFunc) _clickFunc();
      _state = 3;

    } else if (now > _startTime + _longPressTicks) {
      _isLongPressed = true;  // Keep track of long press state
      _pattern = _pattern + "L";
      _patternLength += 1;
      
      if (_patternLength == 1) { // check for pattern start
        if (_patternStartFunc) _patternStartFunc();
      }
      if (_longPressStartFunc) _longPressStartFunc();
      _state = 4;      
    } // if

  } else if (_state == 3) { // waiting for button pin being pressed again or timeout.
    if (now > _releaseTime + _timeoutTicks) {
      // pattern timeout
      if (_patternEndFunc) _patternEndFunc();
      
      _pattern = "";
      _patternLength = 0;
      _state = 0; // restart.

    } else if (buttonLevel == _buttonPressed) {
      _state = 0; // restart
    } // if

  } else if (_state == 4) { // waiting for button pin being release after long press.
    if (buttonLevel == _buttonReleased) {
      _releaseTime = now;
      _isLongPressed = false;  // Keep track of long press state
      if(_longPressStopFunc) _longPressStopFunc();
      _state = 3; // wait for timeout.
      
    } else if ((now > _longPressLastCall + _longPressReTicks)) {
      _longPressLastCall = now;
      if (_duringLongPressFunc) _duringLongPressFunc();
    } // if  

  } // if  
} // OneButton.tick()


// end.
