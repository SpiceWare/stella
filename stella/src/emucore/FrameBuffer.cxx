//============================================================================
//
//   SSSS    tt          lll  lll       
//  SS  SS   tt           ll   ll        
//  SS     tttttt  eeee   ll   ll   aaaa 
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-1998 by Bradford W. Mott
//
// See the file "license" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//
// $Id: FrameBuffer.cxx,v 1.8 2004-04-27 00:50:51 stephena Exp $
//============================================================================

#include <sstream>

#include "bspf.hxx"
#include "Console.hxx"
#include "Event.hxx"
#include "EventHandler.hxx"
#include "StellaEvent.hxx"
#include "Settings.hxx"
#include "MediaSrc.hxx"
#include "FrameBuffer.hxx"

// Eventually, these may become variables
#define FONTWIDTH  8
#define FONTHEIGHT 8

#define LINEOFFSET 10  // FONTHEIGHT + 1 pixel on top and bottom
#define XBOXOFFSET 8   // 4 pixels to the left and right of text
#define YBOXOFFSET 8   // 4 pixels to the top and bottom of text

#define UPARROW    24  // Indicates more lines above
#define DOWNARROW  25  // Indicates more lines below
#define LEFTARROW  26  // Left arrow for indicating current line
#define RIGHTARROW 27  // Left arrow for indicating current line

#define LEFTMARKER  17 // Indicates item being remapped
#define RIGHTMARKER 16 // Indicates item being remapped

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FrameBuffer::FrameBuffer()
   :  myConsole(0),
      myMediaSource(0),
      myWidth(0),
      myHeight(0),
      theRedrawEntireFrameIndicator(true),
      myFGColor(10),
      myBGColor(0),
      myFrameRate(0),
      myPauseStatus(false),
      myCurrentWidget(W_NONE),
      myRemapEventSelectedFlag(false),
      mySelectedEvent(Event::NoType),
      myMenuMode(false),
      theMenuChangedIndicator(false),
      myMaxRows(0),
      myMaxColumns(0),
      myMainMenuIndex(0),
      myMainMenuItems(sizeof(ourMainMenu)/sizeof(MainMenuItem)),
      myRemapMenuIndex(0),
      myRemapMenuLowIndex(0),
      myRemapMenuHighIndex(0),
      myRemapMenuItems(sizeof(ourRemapMenu)/sizeof(RemapMenuItem)),
      myRemapMenuMaxLines(0),
      myMessageTime(0),
      myMessageText(""),
      myInfoMenuWidth(0)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FrameBuffer::~FrameBuffer(void)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::initDisplay(Console* console, MediaSource* mediasrc)
{
  myConsole     = console;
  myMediaSource = mediasrc;

  // Fill the properties info array with game information
  ourPropertiesInfo[0] = myConsole->properties().get("Cartridge.Name");
  ourPropertiesInfo[1] = "";
  ourPropertiesInfo[2] = "Manufacturer: " + myConsole->properties().get("Cartridge.Manufacturer");
  ourPropertiesInfo[3] = "Model:        " + myConsole->properties().get("Cartridge.ModelNo");
  ourPropertiesInfo[4] = "Rarity:       " + myConsole->properties().get("Cartridge.Rarity");
  ourPropertiesInfo[5] = "Type:         " + myConsole->properties().get("Cartridge.Type");
  ourPropertiesInfo[6] = "";
  ourPropertiesInfo[7] = "MD5SUM:";
  ourPropertiesInfo[8] = myConsole->properties().get("Cartridge.MD5");

  // Get the arrays containing key and joystick mappings
  myConsole->eventHandler().getKeymapArray(&myKeyTable, &myKeyTableSize);
  myConsole->eventHandler().getJoymapArray(&myJoyTable, &myJoyTableSize);

  myFrameRate = myConsole->settings().getInt("framerate");

  // Now initialize the derived class
  init();

  // The following has to be done after the initialization of the derived class,
  //  since we need the exact width and height of the display

  // Determine the maximum number of characters that can be onscreen
  myMaxColumns = myWidth / FONTWIDTH - 3;
  myMaxRows    = myHeight / LINEOFFSET - 2;

  // Set up the correct bounds for the remap menu
  myRemapMenuMaxLines  = myRemapMenuItems > myMaxRows ? myMaxRows : myRemapMenuItems;
  myRemapMenuLowIndex  = 0;
  myRemapMenuHighIndex = myRemapMenuMaxLines;

  // Figure out the longest properties string,
  // and cut any string that is wider than the display
  for(uInt8 i = 0; i < 9; i++)
  {
    if(ourPropertiesInfo[i].length() > (uInt32) myInfoMenuWidth)
    {
      myInfoMenuWidth = ourPropertiesInfo[i].length();
      if(myInfoMenuWidth > myMaxColumns)
      {
        myInfoMenuWidth = myMaxColumns;
        string s = ourPropertiesInfo[i];
        ourPropertiesInfo[i] = s.substr(0, myMaxColumns - 3) + "...";
      }
    }
  }

  // Finally, load the remap menu with strings,
  // clipping any strings which are wider than the display
  loadRemapMenu();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::update()
{
  // Do any pre-frame stuff
  preFrameUpdate();

  // Determine which mode we are in (normal or menu mode)
  // In normal mode, only the mediasource or messages are shown,
  //  and they are shown per-frame
  // In menu mode, any of the menus are shown, but the mediasource
  //  is not updated, and all updates depend on whether the screen is dirty
  if(!myMenuMode)
  {
    // Draw changes to the mediasource
    if(!myPauseStatus)
      myMediaSource->update();

    // We always draw the screen, even if the core is paused
    drawMediaSource();

    if(!myPauseStatus)
    {
      // Draw any pending messages
      if(myMessageTime > 0)
      {
        uInt32 width  = myMessageText.length()*FONTWIDTH + FONTWIDTH;
        uInt32 height = LINEOFFSET + FONTHEIGHT;
        uInt32 x = (myWidth >> 1) - (width >> 1);
        uInt32 y = myHeight - height - LINEOFFSET/2;

        // Draw the bounded box and text
        drawBoundedBox(x, y+1, width, height-2);
        drawText(x + XBOXOFFSET/2, LINEOFFSET/2 + y, myMessageText);
        myMessageTime--;

        // Erase this message on next update
        if(myMessageTime == 0)
          theRedrawEntireFrameIndicator = true;
      }
    }
  }
  else   // we are in MENU_MODE
  {
    // Only update the screen if it's been invalidated
    // or the menus have changed  
    if(theMenuChangedIndicator || theRedrawEntireFrameIndicator)
    {
      drawMediaSource();

      // Then overlay any menu items
      switch(myCurrentWidget)
      {
        case W_NONE:
          break;

        case MAIN_MENU:
          drawMainMenu();
          break;

        case REMAP_MENU:
          drawRemapMenu();
          break;

        case INFO_MENU:
          drawInfoMenu();
          break;

        default:
          break;
      }

      // Now the screen is up to date
      theMenuChangedIndicator = theRedrawEntireFrameIndicator = false;
    }
  }

  // Do any post-frame stuff
  postFrameUpdate();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::showMenu(bool show)
{
  myMenuMode = show;

  myCurrentWidget = show ? MAIN_MENU : W_NONE;
  myRemapEventSelectedFlag = false;
  mySelectedEvent = Event::NoType;
  theRedrawEntireFrameIndicator = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::showMessage(const string& message)
{
  myMessageText = message;
  myMessageTime = myFrameRate << 1;   // Show message for 2 seconds
  theRedrawEntireFrameIndicator = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void FrameBuffer::drawMainMenu()
{
  uInt32 x, y, width, height, i, xpos, ypos;

  width  = 16*FONTWIDTH + (FONTWIDTH << 1);
  height = myMainMenuItems*LINEOFFSET + (FONTHEIGHT << 1);
  x = (myWidth >> 1) - (width >> 1);
  y = (myHeight >> 1) - (height >> 1);

  // Draw the bounded box and text, leaving a little room for arrows
  xpos = x + XBOXOFFSET;
  drawBoundedBox(x-2, y-2, width+3, height+3);
  for(i = 0; i < myMainMenuItems; i++)
    drawText(xpos, LINEOFFSET*i + y + YBOXOFFSET, ourMainMenu[i].action);

  // Now draw the selection arrow around the currently selected item
  ypos = LINEOFFSET*myMainMenuIndex + y + YBOXOFFSET;
  drawChar(x, ypos, LEFTARROW);
  drawChar(x + width - FONTWIDTH, ypos, RIGHTARROW);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void FrameBuffer::drawRemapMenu()
{
  uInt32 x, y, width, height, xpos, ypos;

  width  = (myWidth >> 3) * FONTWIDTH - (FONTWIDTH << 1);
  height = myMaxRows*LINEOFFSET + (FONTHEIGHT << 1);
  x = (myWidth >> 1) - (width >> 1);
  y = (myHeight >> 1) - (height >> 1);

  // Draw the bounded box and text, leaving a little room for arrows
  drawBoundedBox(x-2, y-2, width+3, height+3);
  for(Int32 i = myRemapMenuLowIndex; i < myRemapMenuHighIndex; i++)
  {
    ypos = LINEOFFSET*(i-myRemapMenuLowIndex) + y + YBOXOFFSET;
    drawText(x + XBOXOFFSET, ypos, ourRemapMenu[i].action);

    xpos = width - ourRemapMenu[i].key.length() * FONTWIDTH;
    drawText(xpos, ypos, ourRemapMenu[i].key);
  }

  // Normally draw an arrow indicating the current line,
  // otherwise highlight the currently selected item for remapping
  if(!myRemapEventSelectedFlag)
  {
    ypos = LINEOFFSET*(myRemapMenuIndex-myRemapMenuLowIndex) + y + YBOXOFFSET;
    drawChar(x, ypos, LEFTARROW);
    drawChar(x + width - FONTWIDTH, ypos, RIGHTARROW);
  }
  else
  {
    ypos = LINEOFFSET*(myRemapMenuIndex-myRemapMenuLowIndex) + y + YBOXOFFSET;

    // Left marker is at the beginning of event name text
    xpos = width - ourRemapMenu[myRemapMenuIndex].key.length() * FONTWIDTH - FONTWIDTH;
    drawChar(xpos, ypos, LEFTMARKER);

    // Right marker is at the end of the line
    drawChar(x + width - FONTWIDTH, ypos, RIGHTMARKER);
  }

  // Finally, indicate that there are more items to the top or bottom
  xpos = (width >> 1) - (FONTWIDTH >> 1);
  if(myRemapMenuHighIndex - myMaxRows > 0)
    drawChar(xpos, y, UPARROW);

  if(myRemapMenuLowIndex + myMaxRows < myRemapMenuItems)
    drawChar(xpos, height - (FONTWIDTH >> 1), DOWNARROW);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void FrameBuffer::drawInfoMenu()
{
  uInt32 x, y, width, height, i, xpos;

  width  = myInfoMenuWidth*FONTWIDTH + (FONTWIDTH << 1);
  height = 9*LINEOFFSET + (FONTHEIGHT << 1);
  x = (myWidth >> 1) - (width >> 1);
  y = (myHeight >> 1) - (height >> 1);

  // Draw the bounded box and text
  xpos = x + XBOXOFFSET;
  drawBoundedBox(x, y, width, height);
  for(i = 0; i < 9; i++)
    drawText(xpos, LINEOFFSET*i + y + YBOXOFFSET, ourPropertiesInfo[i]);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::sendKeyEvent(StellaEvent::KeyCode key, Int32 state)
{
  if(myCurrentWidget == W_NONE || state != 1)
    return;

  // Redraw the menus whenever a key event is received
  theMenuChangedIndicator = true;

  // Check which type of widget is pending
  switch(myCurrentWidget)
  {
    case MAIN_MENU:
      if(key == StellaEvent::KCODE_RETURN)
        myCurrentWidget = currentSelectedWidget();
      else if(key == StellaEvent::KCODE_UP)
        moveCursorUp(1);
      else if(key == StellaEvent::KCODE_DOWN)
        moveCursorDown(1);

      break;  // MAIN_MENU

    case REMAP_MENU:
      if(myRemapEventSelectedFlag)
      {
        if(key == StellaEvent::KCODE_ESCAPE)
          deleteBinding(mySelectedEvent);
        else
          addKeyBinding(mySelectedEvent, key);

        myRemapEventSelectedFlag = false;
      }
      else if(key == StellaEvent::KCODE_RETURN)
      {
        mySelectedEvent = currentSelectedEvent();
        myRemapEventSelectedFlag = true;
      }
      else if(key == StellaEvent::KCODE_UP)
        moveCursorUp(1);
      else if(key == StellaEvent::KCODE_DOWN)
        moveCursorDown(1);
      else if(key == StellaEvent::KCODE_PAGEUP)
        moveCursorUp(4);
      else if(key == StellaEvent::KCODE_PAGEDOWN)
        moveCursorDown(4);
      else if(key == StellaEvent::KCODE_ESCAPE)
      {
        myCurrentWidget = MAIN_MENU;
        theRedrawEntireFrameIndicator = true;
      }

      break;  // REMAP_MENU

    case INFO_MENU:
      if(key == StellaEvent::KCODE_ESCAPE)
      {
        myCurrentWidget = MAIN_MENU;
        theRedrawEntireFrameIndicator = true;
      }

      break;  // INFO_MENU

    default:
      break;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::sendJoyEvent(StellaEvent::JoyStick stick,
     StellaEvent::JoyCode code, Int32 state)
{
  if(myCurrentWidget == W_NONE || state != 1)
    return;

  // Redraw the menus whenever a joy event is received
  theMenuChangedIndicator = true;

  // Check which type of widget is pending
  switch(myCurrentWidget)
  {
    case MAIN_MENU:
//      if(key == StellaEvent::KCODE_RETURN)
//        myCurrentWidget = currentSelectedWidget();
      if(code == StellaEvent::JAXIS_UP)
        moveCursorUp(1);
      else if(code == StellaEvent::JAXIS_DOWN)
        moveCursorDown(1);

      break;  // MAIN_MENU

    case REMAP_MENU:
      if(myRemapEventSelectedFlag)
      {
        addJoyBinding(mySelectedEvent, stick, code);
        myRemapEventSelectedFlag = false;
      }
      else if(code == StellaEvent::JAXIS_UP)
        moveCursorUp(1);
      else if(code == StellaEvent::JAXIS_DOWN)
        moveCursorDown(1);
//      else if(key == StellaEvent::KCODE_PAGEUP)
//        movePageUp();
//      else if(key == StellaEvent::KCODE_PAGEDOWN)
//        movePageDown();
//      else if(key == StellaEvent::KCODE_ESCAPE)
//        myCurrentWidget = MAIN_MENU;

      break;  // REMAP_MENU

    default:
      break;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::pause(bool status)
{
  myPauseStatus = status;

  // Now notify the child object, in case it wants to do something
  // special when pause is received
  pauseEvent(myPauseStatus); 
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FrameBuffer::Widget FrameBuffer::currentSelectedWidget()
{
  if(myMainMenuIndex >= 0 && myMainMenuIndex < myMainMenuItems)
    return ourMainMenu[myMainMenuIndex].widget;
  else
    return W_NONE;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Event::Type FrameBuffer::currentSelectedEvent()
{
  if(myRemapMenuIndex >= 0 && myRemapMenuIndex < myRemapMenuItems)
    return ourRemapMenu[myRemapMenuIndex].event;
  else
    return Event::NoType;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::moveCursorUp(uInt32 amt)
{
  switch(myCurrentWidget)
  {
    case MAIN_MENU:
      if(myMainMenuIndex > 0)
        myMainMenuIndex--;

      break;

    case REMAP_MENU:
      // First move cursor up by the given amt
      myRemapMenuIndex -= amt;

      // Move up the boundaries
      if(myRemapMenuIndex < myRemapMenuLowIndex)
      {
        Int32 x = myRemapMenuLowIndex - myRemapMenuIndex;
        myRemapMenuLowIndex -= x;
        myRemapMenuHighIndex -= x;
      }

      // Then scale back down, if necessary
      if(myRemapMenuLowIndex < 0)
      {
        Int32 x = 0 - myRemapMenuLowIndex;
        myRemapMenuIndex += x;
        myRemapMenuLowIndex += x;
        myRemapMenuHighIndex += x;
      }

      break;

    default:  // This should never happen
      break;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::moveCursorDown(uInt32 amt)
{
  switch(myCurrentWidget)
  {
    case MAIN_MENU:
      if(myMainMenuIndex < myMainMenuItems - 1)
        myMainMenuIndex++;

      break;

    case REMAP_MENU:
      // First move cursor down by the given amount
      myRemapMenuIndex += amt;

      // Move down the boundaries
      if(myRemapMenuIndex >= myRemapMenuHighIndex)
      {
        Int32 x = myRemapMenuIndex - myRemapMenuHighIndex + 1;

        myRemapMenuLowIndex += x;
        myRemapMenuHighIndex += x;
      }

      // Then scale back up, if necessary
      if(myRemapMenuHighIndex >= myRemapMenuItems)
      {
        Int32 x = myRemapMenuHighIndex - myRemapMenuItems;
        myRemapMenuIndex -= x;
        myRemapMenuLowIndex -= x;
        myRemapMenuHighIndex -= x;
      }

      break;

    default:  // This should never happen
      break;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::loadRemapMenu()
{
  // Fill the remap menu with the current key and joystick mappings
  for(Int32 i = 0; i < myRemapMenuItems; ++i)
  {
    Event::Type event = ourRemapMenu[i].event;
    ourRemapMenu[i].key = "None";
    string key = "";
    for(uInt32 j = 0; j < myKeyTableSize; ++j)
    {
      if(myKeyTable[j] == event)
      {
        if(key == "")
          key = key + ourEventName[j];
        else
          key = key + ", " + ourEventName[j];
      }
    }
    for(uInt32 j = 0; j < myJoyTableSize; ++j)
    {
      if(myJoyTable[j] == event)
      {
        ostringstream joyevent;
        uInt32 stick  = j / StellaEvent::LastJCODE;
        uInt32 button = j % StellaEvent::LastJCODE;

        switch(button)
        {
          case StellaEvent::JAXIS_UP:
            joyevent << "J" << stick << " UP";
            break;

          case StellaEvent::JAXIS_DOWN:
            joyevent << "J" << stick << " DOWN";
            break;

          case StellaEvent::JAXIS_LEFT:
            joyevent << "J" << stick << " LEFT";
            break;

          case StellaEvent::JAXIS_RIGHT:
            joyevent << "J" << stick << " RIGHT";
            break;

          default:
            joyevent << "J" << stick << " B" << (button-4);
            break;
        }

        if(key == "")
          key = key + joyevent.str();
        else
          key = key + ", " + joyevent.str();
      }
    }

    if(key != "")
    {
      // 19 is the max size of the event names, and 2 is for the space in between
      // (this could probably be cleaner ...)
      uInt32 len = myMaxColumns - 19 - 2;
      if(key.length() > len)
      {
        ourRemapMenu[i].key = key.substr(0, len - 3) + "...";
      }
      else
        ourRemapMenu[i].key = key;
    }
  }

  // Save the new bindings
  ostringstream keybuf, joybuf;

  // Iterate through the keymap table and create a colon-separated list
  for(uInt32 i = 0; i < StellaEvent::LastKCODE; ++i)
    keybuf << myKeyTable[i] << ":";
  myConsole->settings().setString("keymap", keybuf.str());

  // Iterate through the joymap table and create a colon-separated list
  for(uInt32 i = 0; i < StellaEvent::LastJSTICK*StellaEvent::LastJCODE; ++i)
    joybuf << myJoyTable[i] << ":";
  myConsole->settings().setString("joymap", joybuf.str());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::addKeyBinding(Event::Type event, StellaEvent::KeyCode key)
{
  myKeyTable[key] = event;

  loadRemapMenu();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::addJoyBinding(Event::Type event,
       StellaEvent::JoyStick stick, StellaEvent::JoyCode code)
{
  myJoyTable[stick * StellaEvent::LastJCODE + code] = event;

  loadRemapMenu();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::deleteBinding(Event::Type event)
{
  for(uInt32 i = 0; i < myKeyTableSize; ++i)
    if(myKeyTable[i] == event)
      myKeyTable[i] = Event::NoType;

  for(uInt32 j = 0; j < myJoyTableSize; ++j)
    if(myJoyTable[j] == event)
      myJoyTable[j] = Event::NoType;

  loadRemapMenu();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const uInt8 FrameBuffer::ourFontData[2048] = {
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7e,0x81,0xa5,0x81,0xbd,0x99,0x81,0x7e,0x7e,0xff,0xdb,0xff,0xc3,0xe7,0xff,0x7e,0x36,0x7f,0x7f,0x7f,0x3e,0x1c,0x08,0x00,0x08,0x1c,0x3e,0x7f,0x3e,0x1c,0x08,0x00,0x1c,0x3e,0x1c,0x7f,0x7f,0x3e,0x1c,0x3e,0x08,0x08,0x1c,0x3e,0x7f,0x3e,0x1c,0x3e,0x00,0x00,0x18,0x3c,0x3c,0x18,0x00,0x00,0xff,0xff,0xe7,0xc3,0xc3,0xe7,0xff,0xff,0x00,0x3c,0x66,0x42,0x42,0x66,0x3c,0x00,0xff,0xc3,0x99,0xbd,0xbd,0x99,0xc3,0xff,0xf0,0xe0,0xf0,0xbe,0x33,0x33,0x33,0x1e,0x3c,0x66,0x66,0x66,0x3c,0x18,0x7e,0x18,0xfc,0xcc,0xfc,0x0c,0x0c,0x0e,0x0f,0x07,0xfe,0xc6,0xfe,0xc6,0xc6,0xe6,0x67,0x03,0x99,0x5a,0x3c,0xe7,0xe7,0x3c,0x5a,0x99,0x01,0x07,0x1f,0x7f,0x1f,0x07,0x01,0x00,0x40,0x70,0x7c,0x7f,0x7c,0x70,0x40,0x00,0x18,0x3c,0x7e,0x18,0x18,0x7e,0x3c,0x18,0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x00,0xfe,0xdb,0xdb,0xde,0xd8,0xd8,0xd8,0x00,0x7c,0xc6,0x1c,0x36,0x36,0x1c,0x33,0x1e,0x00,0x00,0x00,0x00,0x7e,0x7e,0x7e,0x00,0x18,0x3c,0x7e,0x18,0x7e,0x3c,0x18,0xff,0x18,0x3c,0x7e,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x7e,0x3c,0x18,0x00,0x00,0x18,0x30,0x7f,0x30,0x18,0x00,0x00,0x00,0x0c,0x06,0x7f,0x06,0x0c,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x7f,0x00,0x00,0x00,0x24,0x66,0xff,0x66,0x24,0x00,0x00,0x00,0x18,0x3c,0x7e,0xff,0xff,0x00,0x00,0x00,0xff,0xff,0x7e,0x3c,0x18,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x1e,0x1e,0x0c,0x0c,0x00,0x0c,0x00,0x36,0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x36,0x36,0x7f,0x36,0x7f,0x36,0x36,0x00,0x0c,0x3e,0x03,0x1e,0x30,0x1f,0x0c,0x00,0x00,0x63,0x33,0x18,0x0c,0x66,0x63,0x00,0x1c,0x36,0x1c,0x6e,0x3b,0x33,0x6e,0x00,0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00,0x18,0x0c,0x06,0x06,0x06,0x0c,0x18,0x00,0x06,0x0c,0x18,0x18,0x18,0x0c,0x06,0x00,0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00,0x00,0x0c,0x0c,0x3f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x06,0x00,0x00,0x00,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x00,0x60,0x30,0x18,0x0c,0x06,0x03,0x01,0x00,0x3e,0x63,0x73,0x7b,0x6f,0x67,0x3e,0x00,0x0c,0x0e,0x0c,0x0c,0x0c,0x0c,0x3f,0x00,0x1e,0x33,0x30,0x1c,0x06,0x33,0x3f,0x00,0x1e,0x33,0x30,0x1c,0x30,0x33,0x1e,0x00,0x38,0x3c,0x36,0x33,0x7f,0x30,0x78,0x00,0x3f,0x03,0x1f,0x30,0x30,0x33,0x1e,0x00,0x1c,0x06,0x03,0x1f,0x33,0x33,0x1e,0x00,0x3f,0x33,0x30,0x18,0x0c,0x0c,0x0c,0x00,0x1e,0x33,0x33,0x1e,0x33,0x33,0x1e,0x00,0x1e,0x33,0x33,0x3e,0x30,0x18,0x0e,0x00,0x00,0x0c,0x0c,0x00,0x00,0x0c,0x0c,0x00,0x00,0x0c,0x0c,0x00,0x00,0x0c,0x0c,0x06,0x18,0x0c,0x06,0x03,0x06,0x0c,0x18,0x00,0x00,0x00,0x3f,0x00,0x00,0x3f,0x00,0x00,0x06,0x0c,0x18,0x30,0x18,0x0c,0x06,0x00,0x1e,0x33,0x30,0x18,0x0c,0x00,0x0c,0x00,
0x3e,0x63,0x7b,0x7b,0x7b,0x03,0x1e,0x00,0x0c,0x1e,0x33,0x33,0x3f,0x33,0x33,0x00,0x3f,0x66,0x66,0x3e,0x66,0x66,0x3f,0x00,0x3c,0x66,0x03,0x03,0x03,0x66,0x3c,0x00,0x1f,0x36,0x66,0x66,0x66,0x36,0x1f,0x00,0x7f,0x46,0x16,0x1e,0x16,0x46,0x7f,0x00,0x7f,0x46,0x16,0x1e,0x16,0x06,0x0f,0x00,0x3c,0x66,0x03,0x03,0x73,0x66,0x7c,0x00,0x33,0x33,0x33,0x3f,0x33,0x33,0x33,0x00,0x1e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0x00,0x78,0x30,0x30,0x30,0x33,0x33,0x1e,0x00,0x67,0x66,0x36,0x1e,0x36,0x66,0x67,0x00,0x0f,0x06,0x06,0x06,0x46,0x66,0x7f,0x00,0x63,0x77,0x7f,0x7f,0x6b,0x63,0x63,0x00,0x63,0x67,0x6f,0x7b,0x73,0x63,0x63,0x00,0x1c,0x36,0x63,0x63,0x63,0x36,0x1c,0x00,0x3f,0x66,0x66,0x3e,0x06,0x06,0x0f,0x00,0x1e,0x33,0x33,0x33,0x3b,0x1e,0x38,0x00,0x3f,0x66,0x66,0x3e,0x36,0x66,0x67,0x00,0x1e,0x33,0x07,0x0e,0x38,0x33,0x1e,0x00,0x3f,0x2d,0x0c,0x0c,0x0c,0x0c,0x1e,0x00,0x33,0x33,0x33,0x33,0x33,0x33,0x3f,0x00,0x33,0x33,0x33,0x33,0x33,0x1e,0x0c,0x00,0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00,0x63,0x63,0x36,0x1c,0x1c,0x36,0x63,0x00,0x33,0x33,0x33,0x1e,0x0c,0x0c,0x1e,0x00,0x7f,0x63,0x31,0x18,0x4c,0x66,0x7f,0x00,0x1e,0x06,0x06,0x06,0x06,0x06,0x1e,0x00,0x03,0x06,0x0c,0x18,0x30,0x60,0x40,0x00,0x1e,0x18,0x18,0x18,0x18,0x18,0x1e,0x00,0x08,0x1c,0x36,0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,
0x0c,0x0c,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1e,0x30,0x3e,0x33,0x6e,0x00,0x07,0x06,0x06,0x3e,0x66,0x66,0x3b,0x00,0x00,0x00,0x1e,0x33,0x03,0x33,0x1e,0x00,0x38,0x30,0x30,0x3e,0x33,0x33,0x6e,0x00,0x00,0x00,0x1e,0x33,0x3f,0x03,0x1e,0x00,0x1c,0x36,0x06,0x0f,0x06,0x06,0x0f,0x00,0x00,0x00,0x6e,0x33,0x33,0x3e,0x30,0x1f,0x07,0x06,0x36,0x6e,0x66,0x66,0x67,0x00,0x0c,0x00,0x0e,0x0c,0x0c,0x0c,0x1e,0x00,0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1e,0x07,0x06,0x66,0x36,0x1e,0x36,0x67,0x00,0x0e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0x00,0x00,0x00,0x33,0x7f,0x7f,0x6b,0x63,0x00,0x00,0x00,0x1f,0x33,0x33,0x33,0x33,0x00,0x00,0x00,0x1e,0x33,0x33,0x33,0x1e,0x00,0x00,0x00,0x3b,0x66,0x66,0x3e,0x06,0x0f,0x00,0x00,0x6e,0x33,0x33,0x3e,0x30,0x78,0x00,0x00,0x3b,0x6e,0x66,0x06,0x0f,0x00,0x00,0x00,0x3e,0x03,0x1e,0x30,0x1f,0x00,0x08,0x0c,0x3e,0x0c,0x0c,0x2c,0x18,0x00,0x00,0x00,0x33,0x33,0x33,0x33,0x6e,0x00,0x00,0x00,0x33,0x33,0x33,0x1e,0x0c,0x00,0x00,0x00,0x63,0x6b,0x7f,0x7f,0x36,0x00,0x00,0x00,0x63,0x36,0x1c,0x36,0x63,0x00,0x00,0x00,0x33,0x33,0x33,0x3e,0x30,0x1f,0x00,0x00,0x3f,0x19,0x0c,0x26,0x3f,0x00,0x38,0x0c,0x0c,0x07,0x0c,0x0c,0x38,0x00,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00,0x07,0x0c,0x0c,0x38,0x0c,0x0c,0x07,0x00,0x6e,0x3b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x1c,0x36,0x63,0x63,0x7f,0x00,
0x1e,0x33,0x03,0x33,0x1e,0x18,0x30,0x1e,0x00,0x33,0x00,0x33,0x33,0x33,0x7e,0x00,0x38,0x00,0x1e,0x33,0x3f,0x03,0x1e,0x00,0x7e,0xc3,0x3c,0x60,0x7c,0x66,0xfc,0x00,0x33,0x00,0x1e,0x30,0x3e,0x33,0x7e,0x00,0x07,0x00,0x1e,0x30,0x3e,0x33,0x7e,0x00,0x0c,0x0c,0x1e,0x30,0x3e,0x33,0x7e,0x00,0x00,0x00,0x1e,0x03,0x03,0x1e,0x30,0x1c,0x7e,0xc3,0x3c,0x66,0x7e,0x06,0x3c,0x00,0x33,0x00,0x1e,0x33,0x3f,0x03,0x1e,0x00,0x07,0x00,0x1e,0x33,0x3f,0x03,0x1e,0x00,0x33,0x00,0x0e,0x0c,0x0c,0x0c,0x1e,0x00,0x3e,0x63,0x1c,0x18,0x18,0x18,0x3c,0x00,0x07,0x00,0x0e,0x0c,0x0c,0x0c,0x1e,0x00,0x63,0x1c,0x36,0x63,0x7f,0x63,0x63,0x00,0x0c,0x0c,0x00,0x1e,0x33,0x3f,0x33,0x00,0x38,0x00,0x3f,0x06,0x1e,0x06,0x3f,0x00,0x00,0x00,0xfe,0x30,0xfe,0x33,0xfe,0x00,0x7c,0x36,0x33,0x7f,0x33,0x33,0x73,0x00,0x1e,0x33,0x00,0x1e,0x33,0x33,0x1e,0x00,0x00,0x33,0x00,0x1e,0x33,0x33,0x1e,0x00,0x00,0x07,0x00,0x1e,0x33,0x33,0x1e,0x00,0x1e,0x33,0x00,0x33,0x33,0x33,0x7e,0x00,0x00,0x07,0x00,0x33,0x33,0x33,0x7e,0x00,0x00,0x33,0x00,0x33,0x33,0x3e,0x30,0x1f,0xc3,0x18,0x3c,0x66,0x66,0x3c,0x18,0x00,0x33,0x00,0x33,0x33,0x33,0x33,0x1e,0x00,0x18,0x18,0x7e,0x03,0x03,0x7e,0x18,0x18,0x1c,0x36,0x26,0x0f,0x06,0x67,0x3f,0x00,0x33,0x33,0x1e,0x3f,0x0c,0x3f,0x0c,0x0c,0x1f,0x33,0x33,0x5f,0x63,0xf3,0x63,0xe3,0x70,0xd8,0x18,0x3c,0x18,0x18,0x1b,0x0e,
0x38,0x00,0x1e,0x30,0x3e,0x33,0x7e,0x00,0x1c,0x00,0x0e,0x0c,0x0c,0x0c,0x1e,0x00,0x00,0x38,0x00,0x1e,0x33,0x33,0x1e,0x00,0x00,0x38,0x00,0x33,0x33,0x33,0x7e,0x00,0x00,0x1f,0x00,0x1f,0x33,0x33,0x33,0x00,0x3f,0x00,0x33,0x37,0x3f,0x3b,0x33,0x00,0x3c,0x36,0x36,0x7c,0x00,0x7e,0x00,0x00,0x1c,0x36,0x36,0x1c,0x00,0x3e,0x00,0x00,0x0c,0x00,0x0c,0x06,0x03,0x33,0x1e,0x00,0x00,0x00,0x00,0x3f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x3f,0x30,0x30,0x00,0x00,0xc3,0x63,0x33,0x7b,0xcc,0x66,0x33,0xf0,0xc3,0x63,0x33,0xdb,0xec,0xf6,0xf3,0xc0,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x00,0x00,0xcc,0x66,0x33,0x66,0xcc,0x00,0x00,0x00,0x33,0x66,0xcc,0x66,0x33,0x00,0x00,0x44,0x11,0x44,0x11,0x44,0x11,0x44,0x11,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xdb,0xee,0xdb,0x77,0xdb,0xee,0xdb,0x77,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1f,0x18,0x18,0x18,0x18,0x18,0x1f,0x18,0x1f,0x18,0x18,0x18,0x6c,0x6c,0x6c,0x6c,0x6f,0x6c,0x6c,0x6c,0x00,0x00,0x00,0x00,0x7f,0x6c,0x6c,0x6c,0x00,0x00,0x1f,0x18,0x1f,0x18,0x18,0x18,0x6c,0x6c,0x6f,0x60,0x6f,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x00,0x00,0x7f,0x60,0x6f,0x6c,0x6c,0x6c,0x6c,0x6c,0x6f,0x60,0x7f,0x00,0x00,0x00,0x6c,0x6c,0x6c,0x6c,0x7f,0x00,0x00,0x00,0x18,0x18,0x1f,0x18,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1f,0x18,0x18,0x18,
0x18,0x18,0x18,0x18,0xf8,0x00,0x00,0x00,0x18,0x18,0x18,0x18,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xf8,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0xff,0x00,0x00,0x00,0x18,0x18,0x18,0x18,0xff,0x18,0x18,0x18,0x18,0x18,0xf8,0x18,0xf8,0x18,0x18,0x18,0x6c,0x6c,0x6c,0x6c,0xec,0x6c,0x6c,0x6c,0x6c,0x6c,0xec,0x0c,0xfc,0x00,0x00,0x00,0x00,0x00,0xfc,0x0c,0xec,0x6c,0x6c,0x6c,0x6c,0x6c,0xef,0x00,0xff,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0xef,0x6c,0x6c,0x6c,0x6c,0x6c,0xec,0x0c,0xec,0x6c,0x6c,0x6c,0x00,0x00,0xff,0x00,0xff,0x00,0x00,0x00,0x6c,0x6c,0xef,0x00,0xef,0x6c,0x6c,0x6c,0x18,0x18,0xff,0x00,0xff,0x00,0x00,0x00,0x6c,0x6c,0x6c,0x6c,0xff,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0xff,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0xff,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0xfc,0x00,0x00,0x00,0x18,0x18,0xf8,0x18,0xf8,0x00,0x00,0x00,0x00,0x00,0xf8,0x18,0xf8,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0xfc,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0x6c,0xff,0x6c,0x6c,0x6c,0x18,0x18,0xff,0x18,0xff,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xf8,0x18,0x18,0x18,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
0x00,0x00,0x6e,0x3b,0x13,0x3b,0x6e,0x00,0x00,0x1e,0x33,0x1f,0x33,0x1f,0x03,0x03,0x00,0x3f,0x33,0x03,0x03,0x03,0x03,0x00,0x00,0x7f,0x36,0x36,0x36,0x36,0x36,0x00,0x3f,0x33,0x06,0x0c,0x06,0x33,0x3f,0x00,0x00,0x00,0x7e,0x1b,0x1b,0x1b,0x0e,0x00,0x00,0x66,0x66,0x66,0x66,0x3e,0x06,0x03,0x00,0x6e,0x3b,0x18,0x18,0x18,0x18,0x00,0x3f,0x0c,0x1e,0x33,0x33,0x1e,0x0c,0x3f,0x1c,0x36,0x63,0x7f,0x63,0x36,0x1c,0x00,0x1c,0x36,0x63,0x63,0x36,0x36,0x77,0x00,0x38,0x0c,0x18,0x3e,0x33,0x33,0x1e,0x00,0x00,0x00,0x7e,0xdb,0xdb,0x7e,0x00,0x00,0x60,0x30,0x7e,0xdb,0xdb,0x7e,0x06,0x03,0x1c,0x06,0x03,0x1f,0x03,0x06,0x1c,0x00,0x1e,0x33,0x33,0x33,0x33,0x33,0x33,0x00,0x00,0x3f,0x00,0x3f,0x00,0x3f,0x00,0x00,0x0c,0x0c,0x3f,0x0c,0x0c,0x00,0x3f,0x00,0x06,0x0c,0x18,0x0c,0x06,0x00,0x3f,0x00,0x18,0x0c,0x06,0x0c,0x18,0x00,0x3f,0x00,0x70,0xd8,0xd8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1b,0x1b,0x0e,0x0c,0x0c,0x00,0x3f,0x00,0x0c,0x0c,0x00,0x00,0x6e,0x3b,0x00,0x6e,0x3b,0x00,0x00,0x1c,0x36,0x36,0x1c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0xf0,0x30,0x30,0x30,0x37,0x36,0x3c,0x38,0x1e,0x36,0x36,0x36,0x36,0x00,0x00,0x00,0x0e,0x18,0x0c,0x06,0x1e,0x00,0x00,0x00,0x00,0x00,0x3c,0x3c,0x3c,0x3c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FrameBuffer::MainMenuItem FrameBuffer::ourMainMenu[2] = {
  { REMAP_MENU,  "Event Remapping"  },
  { INFO_MENU,   "Game Information" }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FrameBuffer::RemapMenuItem FrameBuffer::ourRemapMenu[57] = {
  { Event::ConsoleSelect,           "Select",               "" },
  { Event::ConsoleReset,            "Reset",                "" },
  { Event::ConsoleColor,            "Color TV",             "" },
  { Event::ConsoleBlackWhite,       "B/W TV",               "" },
  { Event::ConsoleLeftDifficultyB,  "Left Diff. B",         "" },
  { Event::ConsoleLeftDifficultyA,  "Left Diff. A",         "" },
  { Event::ConsoleRightDifficultyB, "Right Diff. B",        "" },
  { Event::ConsoleRightDifficultyA, "Right Diff. A",        "" },
  { Event::SaveState,               "Save State",           "" },
  { Event::ChangeState,             "Change State",         "" },
  { Event::LoadState,               "Load State",           "" },
  { Event::TakeSnapshot,            "Snapshot",             "" },
  { Event::Pause,                   "Pause",                "" },

  { Event::JoystickZeroUp,          "Left-Joy Up",          "" },
  { Event::JoystickZeroDown,        "Left-Joy Down",        "" },
  { Event::JoystickZeroLeft,        "Left-Joy Left",        "" },
  { Event::JoystickZeroRight,       "Left-Joy Right",       "" },
  { Event::JoystickZeroFire,        "Left-Joy Fire",        "" },

  { Event::JoystickOneUp,           "Right-Joy Up",         "" },
  { Event::JoystickOneDown,         "Right-Joy Down",       "" },
  { Event::JoystickOneLeft,         "Right-Joy Left",       "" },
  { Event::JoystickOneRight,        "Right-Joy Right",      "" },
  { Event::JoystickOneFire,         "Right-Joy Fire",       "" },

  { Event::BoosterGripZeroTrigger,  "Left-BGrip Trigger",   "" },
  { Event::BoosterGripZeroBooster,  "Left-BGrip Booster",   "" },

  { Event::BoosterGripOneTrigger,   "Right-BGrip Trigger",  "" },
  { Event::BoosterGripOneBooster,   "Right-BGrip Booster",  "" },

  { Event::DrivingZeroCounterClockwise, "Left-Driving Left",  "" },
  { Event::DrivingZeroClockwise,        "Left-Driving Right", "" },
  { Event::DrivingZeroFire,             "Left-Driving Fire",  "" },

  { Event::DrivingOneCounterClockwise, "Right-Driving Left",  "" },
  { Event::DrivingOneClockwise,        "Right-Driving Right", "" },
  { Event::DrivingOneFire,             "Right-Driving Fire",  "" },

  { Event::KeyboardZero1,           "Left-Pad 1",           "" },
  { Event::KeyboardZero2,           "Left-Pad 2",           "" },
  { Event::KeyboardZero3,           "Left-Pad 3",           "" },
  { Event::KeyboardZero4,           "Left-Pad 4",           "" },
  { Event::KeyboardZero5,           "Left-Pad 5",           "" },
  { Event::KeyboardZero6,           "Left-Pad 6",           "" },
  { Event::KeyboardZero7,           "Left-Pad 7",           "" },
  { Event::KeyboardZero8,           "Left-Pad 8",           "" },
  { Event::KeyboardZero9,           "Left-Pad 9",           "" },
  { Event::KeyboardZeroStar,        "Left-Pad *",           "" },
  { Event::KeyboardZero0,           "Left-Pad 0",           "" },
  { Event::KeyboardZeroPound,       "Left-Pad #",           "" },

  { Event::KeyboardOne1,            "Right-Pad 1",          "" },
  { Event::KeyboardOne2,            "Right-Pad 2",          "" },
  { Event::KeyboardOne3,            "Right-Pad 3",          "" },
  { Event::KeyboardOne4,            "Right-Pad 4",          "" },
  { Event::KeyboardOne5,            "Right-Pad 5",          "" },
  { Event::KeyboardOne6,            "Right-Pad 6",          "" },
  { Event::KeyboardOne7,            "Right-Pad 7",          "" },
  { Event::KeyboardOne8,            "Right-Pad 8",          "" },
  { Event::KeyboardOne9,            "Right-Pad 9",          "" },
  { Event::KeyboardOneStar,         "Right-Pad *",          "" },
  { Event::KeyboardOne0,            "Right-Pad 0",          "" },
  { Event::KeyboardOnePound,        "Right-Pad #",          "" }
};

/**
  This array must be initialized in a specific order, matching
  their initialization in StellaEvent::KeyCode.

  The other option would be to create an array of structures
  (in StellaEvent.hxx) containing event/string pairs.
  This would eliminate the use of enumerations and slow down
  lookups.  So I do it this way instead.
 */
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const char* FrameBuffer::ourEventName[StellaEvent::LastKCODE] = {
  "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
  "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",

  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",

  "KP 0", "KP 1", "KP 2", "KP 3", "KP 4", "KP 5", "KP 6", "KP 7", "KP 8",
  "KP 9", "KP .", "KP /", "KP *", "KP -", "KP +", "KP ENTER", "KP =",

  "BACKSP", "TAB", "CLEAR", "ENTER", "ESC", "SPACE", ",", "-", ".",
  "/", "\\", ";", "=", "\"", "`", "[", "]",

  "PRT SCRN", "SCR LOCK", "PAUSE", "INS", "HOME", "PGUP",
  "DEL", "END", "PGDN",

  "LCTRL", "RCTRL", "LALT", "RALT", "LWIN", "RWIN", "MENU",
  "UP", "DOWN", "LEFT", "RIGHT",

  "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
  "F11", "F12", "F13", "F14", "F15",
};
