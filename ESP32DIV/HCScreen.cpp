/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/ESP32-DIV
   ________________________________________ */

#include "Adafruit_ST7735.h"
#include "Arduino.h"
#include "HCScreen.h"
#include "FS.h"
#include "SD.h"
#define CHAR_WIDTH 6

//This is a mapping table for special characters
const byte ANSI[128] = {
  69,32,44,159,34,242,197,206, //80
  32,32,83,60,79,32,90,32, //88
  32,239,239,34,34,250,45,196, //90
  126,32,115,62,111,32,122,89,  //98
  32,173,189,156,207,190,221,245, //A0
  249,184,166,174,170,32,169,238, //A8
  248,241,253,252,239,230,244,250, //B0
  44,251,248,175,172,171,243,168, //B8
  183,181,182,65,142,65,146,128, //C0
  212,144,210,211,222,214,215,216, //C8
  209,165,227,224,226,229,153,158, //D0
  157,235,233,234,154,237,231,224, //D8
  133,160,131,198,132,134,145,135, //E0
  138,130,136,137,141,161,140,139, //E8
  208,164,149,162,147,228,148,246, //F0
  155,151,163,150,129,236,232,152 //F8
};

//keyboard 0 to 51 = lower case 52 to 103 = uppercase
const byte KEYS[104] = {
  '1','2','3','4','5','6','7','8','9','0',223,'#',17,
  'q','w','e','r','z','u','i','o','p',252,'+',27,26,
  'a','s','d','f','g','h','j','k','l',246,228,'@',25,
  30,'<','y','c','v','b',' ','n','m',',','.','-',30,
  '!','"','§','$','%','&','/','(',')','=','?',96,17,
  'Q','W','E','R','Z','U','I','O','P',220,'*',27,26,
  'A','S','D','F','G','H','J','K','L',214,196,'|',25,
  31,'>','Y','C','V','B',' ','N','M',';',':','_',31
};


//constructor prefill private members
HCScreen::HCScreen(Adafruit_ST7735& tft): _tft(tft) {
  init();
  _selectedLine = -1; //selected line -1 = none
  _fontColor = convertColor(HC_BLACK);
  _bgColor = convertColor(HC_ORANGE);
  _selFontColor = convertColor(HC_ORANGE);
  _selBgColor = convertColor(HC_BLACK);
  _titleFontColor = convertColor(HC_BLACK);
  _titleBgColor = convertColor(HC_ORANGE);
  _gridSelColor = convertColor(HC_RED);
  _gridBgColor = convertColor(HC_BLACK);
  _title = "";
  _startLine = 0;
  _contentLines = 0;
  _showTitle = 0;
  _tft.fillScreen(_bgColor);
  _lineLength = 8;
  _keyboard = 0;
  _keyResult = "";
  _keyBgColor = 0xFFFF;
  _keyFntColor = 0;
  _keySelColor = 0x1F;
  _screenMode = HC_NONE;
}

//Initialize screen geometry this function needs to be called whenever
//orientation of the TFT display was changed
void HCScreen::init() {
  uint8_t swidth = _tft.width();
  uint8_t sheight = _tft.height();
  _lineLength = swidth / CHAR_WIDTH; //we need 6 pixels/character
  _leftMargin = (swidth-(_lineLength*CHAR_WIDTH))/2;
  _rightMargin = swidth-(_lineLength*CHAR_WIDTH)-_leftMargin;
  _screenLines = (sheight + 1 - _lineHeight) / _lineHeight; //we need 10 pixels/line
  _topMargin = (sheight-(_screenLines*_lineHeight))/2;
  _bottomMargin = sheight-(_screenLines*_lineHeight)-_topMargin;
  _tft.fillScreen(_bgColor);
}

//show all visible lines out of the content array beginning with _startLine.
//this allows to display more text which can be scrolled by modifying
//_startLine. Currently the number of lines is limited to 100
void HCScreen::showContent()
{
  //lines
  uint8_t line = _startLine; //content Line
  uint8_t dLine = 0; //display line
  if (_showTitle > 0){
    showLine(dLine,_title,_titleFontColor,_titleBgColor);
    dLine++;
  }
  String txt = "";
  String txt1 = "";
  uint8_t idx = 0;
  while (dLine < _screenLines) {
    txt = "";
    if (line < _contentLines) {
      txt = _content[line];
      if (_selectedLine == line) {
        showLine(dLine,txt,_selFontColor,_selBgColor);
      } else {
        showLine(dLine,txt,_fontColor,_bgColor);
      }
      dLine ++;
      line ++;
    } else {
      showLine(dLine,txt,_fontColor,_bgColor);
      dLine ++;
      line ++;
    }
  }
  if (_keyboard) initKeyboard(_keyResult);
}

//display one line. Special characters will be mapped from the ANSI code
//to the ASCII code of the display
void HCScreen::showLine(uint8_t lin, String txt, uint16_t font, uint16_t bg) {
  uint8_t i;
  uint8_t pos;
  unsigned long color;
  uint16_t curbg; //current background
  uint16_t curft; //current font color
  uint8_t y = lin * _lineHeight + _topMargin;
  uint8_t tIndex = 0;
  uint8_t tLen = txt.length();
  byte c = 0;
  _tft.setCursor(_leftMargin,y+1);
  _tft.setTextColor(font,bg);
  curbg = bg;
  curft = font;
  for (i = 0; i<_lineLength; i++) {
    if (tIndex < tLen) {
      c = txt[tIndex];
      if (c == 195) { //UNI-Code
        c=64 + txt[++tIndex];
      }
      if (c>127) c=ANSI[c-128];
      if (c > 31) {
        _tft.print(char(c));
      } else {
        //we have control characters for markup
        switch (c) {
          case 1: //reset markup
            curft = font;
            curbg = bg;
            _tft.setTextColor(curft,curbg);
            break;
          case 2: //font color
            color=txt[++tIndex];
            color = (color * 256) + txt[++tIndex];
            color = (color * 256) + txt[++tIndex];
            curft = convertColor(color);
            _tft.setTextColor(curft);
            break;
          case 3: //background color
            color=txt[++tIndex];
            color = (color * 256) + txt[++tIndex];
            color = (color * 256) + txt[++tIndex];
            curbg = convertColor(color);
            _tft.setTextColor(curft,curbg);
            break;
          case 4: //position
            pos = txt[++tIndex] * CHAR_WIDTH;
            _tft.setCursor(_leftMargin+pos,y+1);
            break;
        }
      }
      tIndex++;
    } else {
      _tft.print(" ");
    }
  }

}

//shows all available characters ordered by code number
//16 characters in a line and 16 lines
void HCScreen::showCodeset() {
  _screenMode = HC_STATIC;
  _showTitle = 0;
  _selectedLine = -1;
  _startLine = 0;
  _tft.fillScreen(_bgColor);
  uint8_t lin =0;
  uint8_t pos = 0;
  char c = 0;
  for (lin = 0; lin<16; lin++) {
    _tft.setCursor(_leftMargin,(lin * 8));
    for (pos = 0; pos < 16; pos++) {
      _tft.print(c);
      c++;
    }
  }

}
//show a 31x31 pixel icon on position calculated from index
//the bitmap is a rgb bitmap with 8-bit R, G and B
void HCScreen::showIcon(uint8_t index, const HCIcon *icon){
  if (index >= _icons) index = _icons-1;
  uint8_t row  = index / _columns;
  uint8_t column = index % _columns;
  showIcon(column,row,icon);
}

//show a 31x31 pixel icon on position x y
//the bitmap is a rgb bitmap with 8-bit R, G and B
void HCScreen::showIcon(uint8_t column, uint8_t row, const HCIcon *icon){
  if (icon) {

    uint8_t x = column * 32 +_gridX;
    uint8_t y = row * 32 + _gridY;
    if (x>(_tft.width()-32)) x = _tft.width()-32;
    if (y>(_tft.height()-32)) y = _tft.height()-32;
    uint8_t ix = 0;
    uint8_t iy = 0;
    uint16_t idx = 0;
    uint16_t color = 0;
    for (iy = 0; iy<icon->height; iy++){
      for (ix = 0; ix<icon->width; ix++) {
        color = _tft.Color565(icon->pixel_data[idx++], icon->pixel_data[idx++], icon->pixel_data[idx++]);
        _tft.drawPixel(x+ix+1,y+iy+1,color);
      }
    }
  }
}
//show a keyboard on bottom of the display
void HCScreen::initKeyboard(String result = ""){
  _screenMode = HC_KEYBOARD;
  _keyboard = 1;
  _keyShift = 52;
  _keyResult = "";
  uint8_t i = 0;
  char c;
  while (i<result.length()) {
    c = result[i++];
    if (c==195) c = 64+result[i++];
    _keyResult += c;
  }
  _keyCursor = _keyResult.length();
  _selectedLine = 0;
  _gridMode = 1;
  _rows = 4;
  _columns = 13;
  _selectedColumn = 0;
  uint8_t gwidth =  (13*12+1);
  _keyX = (_tft.width() - gwidth) /2;
  _keyY = (_topMargin + (_screenLines-4)*_lineHeight);
  //clear background
  _tft.fillRect(0,_keyY- _lineHeight,_tft.width(),_lineHeight*5,_keyBgColor);
  for (uint8_t i = 0;i<5;i++){
    _tft.drawLine(_keyX,_keyY + i*_lineHeight,_keyX + gwidth-1, _keyY+ i*_lineHeight, _keyFntColor );
  };
  for (uint8_t i = 0;i<=13;i++){
    _tft.drawLine(_keyX + i* 12,_keyY,_keyX + i* 12,_keyY + 4*_lineHeight, _keyFntColor );
  };
  showKeyboard(0);
  showKeySelection(_keySelColor);
}

//Rebuild the result and all key labels
//used to update result and Cursor
//key labels are updated only if shift state has changed
void HCScreen::showKeyboard(uint8_t shift){
  uint8_t x = 0;
  uint8_t y = 0;
  uint8_t gwidth =  (13*12+1);
  byte cc = 0;
  _tft.setTextColor(_keyFntColor,_keyBgColor);
  if (shift != _keyShift) {
    _keyShift = shift;
    for (uint8_t r = 0; r<4; r++){
      for (uint8_t c = 0; c < 13; c++) {
        x= _keyX + c*12 +4;
        y= r * _lineHeight + _keyY +2;
        cc = KEYS[_keyShift+(r*13 + c)];
        if (cc > 127) cc = ANSI[cc-128];
        _tft.setCursor(x,y);
        _tft.print(char(cc));
      }
    }
  }
  x= _leftMargin+_keyCursor*CHAR_WIDTH-1;
  y= _topMargin+(_screenLines-5)*_lineHeight;
  //this fill is done to remove old cursor
  _tft.fillRect(0,y,_tft.width(),_lineHeight,_keyBgColor);
  showLine(_screenLines-5,_keyResult,_keyFntColor,_keyBgColor);
  _tft.drawLine(x,y,x,y+_lineHeight-1,_keySelColor);


}

//initialize joystick need the pins where x, y and the button is connected
//the supplied callback function will be called if the joy button
//is clicked and some conditions depending on mode are valid
void HCScreen::initJoy(uint8_t xPin, uint8_t yPin, uint8_t btnPin, TJoyCallback fn){
  _callback = fn;
  _joyX = xPin;
  _joyY = yPin;
  _joyBtn = btnPin;
  pinMode(_joyX,INPUT_PULLUP);
  pinMode(_joyY,INPUT_PULLUP);
  pinMode(_joyBtn,INPUT_PULLUP);
  while (digitalRead(_joyBtn)==0) delay(100);
}

//chek the state of the joy stick pins and react on changes
//this function should be called in the main loop
//if joystick control is required
void HCScreen::handleJoy(){
  float tmp;
  if (_wait ==0) {

    //read joystick X currently not used
    tmp = analogRead(_joyX);
    if (tmp > 3000) {
      //if value > 3000 we select the next entry of a menu
      //or scroll down if no selection active
      moveRight();
      _wait = 20;
    };
    if (tmp < 1000) {
      //if value < 1000 we select the previous entry of a menu
      //or scroll up if no selection active
      moveLeft();
      _wait = 20;
    }
    //read joystick y value is between 0 and 4096
    tmp = analogRead(_joyY);
    if (tmp > 3000) {
      //if value > 3000 we select the next entry of a menu
      //or scroll down if no selection active
      selectNext();
      _wait = 20;
    };
    if (tmp < 1000) {
      //if value < 1000 we select the previous entry of a menu
      //or scroll up if no selection active
      selectPrevious();
      _wait = 20;
    }
  }
  //select button of the joystick
  if (digitalRead(_joyBtn)==0) {
    //wait until button was released
    while (digitalRead(_joyBtn) == 0) delay(10);
    switch (_screenMode) {
      case HC_KEYBOARD:keyPressed();
        break;
      case HC_MENU: if (_callback) _callback(_screenMode);
        break;
      case HC_FILE: setDirectory(_lastPath,_sdCs);
        break;
      case HC_ICONS: if (_callback) _callback(_screenMode);
        break;
      case HC_DIRECTORY: handleDirectory();
        break;
      default : if (_callback) _callback(_screenMode);
        break;
    }
  }
  delay(10);
  if (_wait > 0) _wait--;
}

//this function handles a joystick click whie an SD-Card directory is displayed
//if a text file was clicked, this text file will be displayed
//if a directory was clicked the display changes to this directory
//if ".." was clicked the display changes to the parent directory
//or calls the callback function if root level was reached or an
//error occured
void HCScreen::handleDirectory() {
  String selection = getSelection();
  if (selection == ".." ) { //the go back entry was selected
    String path = _title;
    if ((path == "Error") || (path == "/")) {
      //if error or root menu go back to parent menu
      if (_callback) _callback(_screenMode);
    } else {
      //otherwise set path one step up
      uint8_t idx = path.lastIndexOf("/");
      if (idx == 0) {
        path = "/";
      } else {
        path.remove(idx);
      }
      setDirectory(path,_sdCs);
    }
  } else {
    //selected entry starts with an asteriks means it is a directory
    if (selection.startsWith("*")) {
      setDirectory(selection.substring(1),_sdCs);
    } else if (selection.endsWith(".txt") || selection.endsWith(".TXT")) {
      //if it is a text file we will display the content
      _lastPath = getTitle();
      setTextFile(_lastPath,selection);
    }
  }

}

//this function handles joy button clicks in keyboard mode
//delete button (left triangle) #17 the character left from cursor will be deleted
//arrow keys #26 and #27 cursor will be moved to the left or to the right
//enter key (down arrow) #25 the callback function will be called
//upper key (up triangle) #30 change the keyboard to shift mode
//lower key (down triangle) #31 change the keyboard to the lower case mode
//all other keys the value of the key will be added to the result string
//at cursor position
void HCScreen::keyPressed() {
  uint8_t shift = _keyShift;
  char key = KEYS[shift+_selectedLine * 13 + _selectedColumn];
  switch (key) {
    case 17: if (_keyCursor > 0) _keyResult.remove(-- _keyCursor, 1);
      break;
    case 25: if (_callback) _callback(_screenMode);
      break;
    case 27: if(_keyCursor > 0) _keyCursor--;
      break;
    case 26: if (_keyCursor < _keyResult.length()) _keyCursor++;
      break;
    case 30: shift=52;
      break;
    case 31: shift=0;
      break;
    default: if (_keyCursor == 0) {
        _keyResult = key+_keyResult;
      } else if (_keyCursor == _keyResult.length()) {
        _keyResult += key;
      } else {
        _keyResult = _keyResult.substring(0,_keyCursor)+key+_keyResult.substring(_keyCursor);
      }
      _keyCursor ++;
      break;
  }
  showKeyboard(shift);

}


//Allows to set a title bar on top of the display
void HCScreen::setTitle(String title){
  _showTitle = 1;
  _title = title;
  showContent();
}

//Allows to set a title bar on top of the display and define
//used font color and background color
//colors are in the 3-byte RGB format and will be converted
//to the 16-bit format used by the display
void HCScreen::setTitle(String title, uint16_t fontColor, uint16_t bgColor){
  setTitleColor(fontColor,bgColor);
  setTitle(title);
}

//fill content with a list of entries and activate selection
void HCScreen::setMenu(String menu[],uint8_t entries){
  _screenMode = HC_MENU;
  _tft.fillScreen(_bgColor);
  _keyboard = 0;
  uint8_t i;
  for (i=0; i<entries; i++){
    _content[i]=menu[i];
  }
  _tft.fillScreen(_bgColor);
  _selectedLine = 0;
  _contentLines = entries;
  _startLine = 0;
  _gridMode = 0;
  showContent();
}

//fill content with directory SDcs is the pin number used for
//card readers chip select
//if SD card cant be mounted, error will be displayed
//otherwise title bar shows the curent path
void HCScreen::setDirectory(String path, uint8_t SDcs){
  _sdCs = SDcs;
  _screenMode = HC_DIRECTORY;
  _tft.fillScreen(_bgColor);
  _keyboard = 0;
  uint8_t cnt = 1;
  if (_title == "") _title = "/";
  _content[0]=".."; //to exit current menu
  if(!SD.begin(SDcs)){
      _content[cnt]="No Card";
      _title = "Error";
      cnt++;
  } else {
    if (!path.startsWith("/") ) {
      if (_title.endsWith("/")) {
        path = _title + path;
      } else {
        path = _title + "/" + path;
      }
    }
    if (path == "") path = "/";
    _title = path;
    cnt = loadDir(SD,path,cnt);
  }
  _selectedLine = 0;
  _startLine = 0;
  _contentLines = cnt;
  _showTitle = 1;
  _gridMode = 0;
  showContent();
}

//Display the first 100 lines of a text file
//long lines will be splitted
//UTF8 codes will be converted
void HCScreen::setTextFile(String path, String fileName) {
  _screenMode = HC_FILE;
  _tft.fillScreen(_bgColor);
  _keyboard = 0;
  int8_t idx = 0;
  if (!path.endsWith("/")) path += "/";
  File dataFile = SD.open(path+fileName);
  _showTitle = 1;
  _title = fileName;
  _selectedLine = -1; //no line selection
  uint8_t lin = 0;
  String line = "";
  String txt = "";
  char c = 0;
  // if the file is available, write to it:
  if (dataFile) {
    while (dataFile.available() && (lin<100)) {
      line = "";
      do {
        c= dataFile.read();
        if (c > 31) {
          if (c==0xc3) {
            c=dataFile.read();
            c=c+64; //convert to ANSI Latin 1
          }
          if (c==0xc2) {
            c=dataFile.read();
          }
        line += String(c);
        }
      } while ((c != 13) && dataFile.available());
      do {
        if (line.length() >= _lineLength ) {
          idx = line.lastIndexOf(" ",_lineLength);
          if (idx >= 0) {
            txt=line.substring(0,idx);
            line = line.substring(++idx);
          } else {
            txt=line.substring(0,_lineLength);
            line = line.substring(_lineLength);
          }
        } else {
          txt = line;
          line = "";
        }
        _content[lin]=txt;
        lin++;
      } while ((line != "") && (lin < 100)) ;
    }
    dataFile.close();
    _contentLines = lin;
  }  else {
    _content[0] = "File not open";
    _contentLines = 1;
  }
  _startLine = 0;
  _gridMode = 0;
  showContent();
}


//initialize icon grid full screen calculate rows and columns
//clear background
void HCScreen::initIconGrid() {
  initIconGrid(0,0,_tft.width()/32,_tft.height()/32);
}

//initialize icon grid on part of the screen
void HCScreen::initIconGrid(uint8_t x, uint8_t y, uint8_t columns, uint8_t rows) {
  _screenMode = HC_ICONS;
  if (x>(_tft.width()-32)) x = _tft.width()-32;
  if (y>(_tft.height()-32)) y = _tft.height()-32;
  if (rows > ((_tft.height()-y)/32)) rows = (_tft.height() - y) /32;
  if (columns > ((_tft.width()-x)/32)) columns = (_tft.width() - x) /32;
  _columns = columns;
  _rows = rows;
  _gridX = x;
  _gridY = y;
  _icons = _columns * _rows;
  _tft.fillRect(x,y,columns*32,rows*32,_gridBgColor);
  _showTitle = 0;
  _gridMode = 1;
  _selectedLine = 0;
  _selectedColumn = 0;
  showGridSelection(_gridSelColor);
}

//set font color and background color for normal text
//colors are in the 3-byte RGB format and will be converted
//to the 16-bit format used by the display
void HCScreen::setBaseColor(unsigned long font_color, unsigned long bg_color) {
  _fontColor = convertColor(font_color);
  _bgColor = convertColor(bg_color);
  _tft.fillScreen(_bgColor);
  showContent();
}

//set font color and background color for the selected line
//colors are in the 3-byte RGB format and will be converted
//to the 16-bit format used by the display
void HCScreen::setSelectionColor(unsigned long font_color, unsigned long bg_color) {
  _selFontColor = convertColor(font_color);
  _selBgColor = convertColor(bg_color);
  showContent();
}

//set the font color for key labels and grid lines, the background color and
//the selection color for rectangle around selected key and for cursor
//colors are in the 3-byte RGB format and will be converted
//to the 16-bit format used by the display

void HCScreen::setKeyboardColor(unsigned long font_color, unsigned long bg_color, unsigned long sel_color) {
  _keyFntColor = convertColor(font_color);
  _keyBgColor = convertColor(bg_color);
  _keySelColor = convertColor(sel_color);
}


//set font color and background color for the title bar
//colors are in the 3-byte RGB format and will be converted
//to the 16-bit format used by the display
void HCScreen::setTitleColor(unsigned long font_color, unsigned long bg_color) {
  _titleFontColor = convertColor(font_color);
  _titleBgColor = convertColor(bg_color);
  showContent();
}

//set selection color and background color for icon grid
//colors are in the 3-byte RGB format and will be converted
//to the 16-bit format used by the display
void HCScreen::setGridColor(unsigned long font_color, unsigned long bg_color) {
  _gridSelColor = convertColor(font_color);
  _gridBgColor = convertColor(bg_color);
  showContent();
}

//this private function converts colors from 3-byte RGB format
//to the 16-bit format (5-bit R, 6-bit G and 5-bit B)
//which is used in the display
uint16_t HCScreen::convertColor(unsigned long webColor) {
  unsigned long r = (webColor & 0xF80000) >> 8;
  unsigned long g = (webColor & 0xFC00) >> 5;
  unsigned long b = (webColor & 0xF8) >> 3;
  uint16_t tftColor = r | g | b;
  return tftColor;
}

//moves the selected line downwards if the bottom is reached and there
//are more lines in the content array, the display will be scrolled up
void HCScreen::selectNext() {
  uint8_t lin;
  if (_gridMode) {
    if (_selectedLine < (_rows-1)) {
      if (_keyboard){
        showKeySelection(_keyFntColor);
        _selectedLine++;
        showKeySelection(_keySelColor);
      } else {
        showGridSelection(_gridBgColor);
        _selectedLine++;
        showGridSelection(_gridSelColor);
      }
    }
  } else if (_selectedLine<0) { //selection is not activated
      if ((_startLine + _screenLines - _showTitle) < (_contentLines-1)) {
        _startLine++;
        showContent();
      }
  } else {
    if (_selectedLine < (_contentLines - 1)) {
      showOneLine(_selectedLine,_fontColor,_bgColor);
      _selectedLine++;
      if ((_selectedLine - _startLine + _showTitle) >= _screenLines) {
        _startLine++;
        showContent();
      } else {
        showOneLine(_selectedLine,_selFontColor,_selBgColor);
      }
    }
  }
}

void HCScreen::showOneLine(uint8_t lin, uint16_t fnt, uint16_t bg) {
  showLine(lin-_startLine+_showTitle,_content[lin],fnt,bg);
}

//moves the selected line upwards if the top is reached and there
//are more lines in the content array, the display will be scrolled up
void HCScreen::selectPrevious() {
  if (_gridMode) {
    if (_selectedLine > 0) {
      if (_keyboard) {
        showKeySelection(_keyFntColor);
        _selectedLine--;
        showKeySelection(_keySelColor);
      }else {
        showGridSelection(_gridBgColor);
        _selectedLine--;
        showGridSelection(_gridSelColor);
      }
    }
  } else if (_selectedLine<0) { //selection is not activated
      if (_startLine  > 0) {
        _startLine--;
        showContent();
      }
  } else {
    if (_selectedLine > 0) {
      showOneLine(_selectedLine,_fontColor,_bgColor);
      _selectedLine--;
      if (_selectedLine < _startLine) {
        _startLine--;
        showContent();
      } else {
        showOneLine(_selectedLine,_selFontColor,_selBgColor);
      }
    }
  }
}

//moves the selection to the right
void HCScreen::moveRight() {
  if (_gridMode) {
    if (_keyboard) {
      if (_selectedColumn < (_columns-1)) {
        showKeySelection(_keyFntColor);
        _selectedColumn++;
        showKeySelection(_keySelColor);
      }
    } else {
      if (_selectedColumn < (_columns-1)) {
        showGridSelection(_gridBgColor);
        _selectedColumn++;
        showGridSelection(_gridSelColor);
      }
    }
  }
}


//moves the selection to the left
void HCScreen::moveLeft() {
  if (_gridMode) {
    if (_keyboard) {
      if (_selectedColumn > 0) {
        showKeySelection(_keyFntColor);
        _selectedColumn--;
        showKeySelection(_keySelColor);
      }
    } else {
      if (_selectedColumn > 0) {
        showGridSelection(_gridBgColor);
        _selectedColumn--;
        showGridSelection(_gridSelColor);
      }
    }
  }
}

//return the currently selected line if selection is activated
//otherwise an empty string will be returned
String HCScreen::getSelection() {
  if ((_selectedLine < 0) || (_gridMode)) {
    return "";
  } else {
    return _content[_selectedLine];
  }
}
//return the result entered by keyboard
String HCScreen::getResult() {
  String unicode = "";
  char c;
  for (uint8_t i = 0; i<_keyResult.length();i++){
    c = _keyResult[i];
    if (c > 127) {
      unicode += char(195);
      unicode += char(c-64);
    } else {
      unicode += c;
    }
  }
  return unicode;
}

//return the title
String HCScreen::getTitle() {
  return _title;
}

//return the index inside content array of the currently selected line
//or -1 if selection is not activated
int8_t HCScreen::getSelectionIndex() {
  if (_gridMode) {
    return _selectedLine * _columns + _selectedColumn;
  } else {
    return _selectedLine;
  }
}

//change the used line height
//do not set smaller then 8
//10 looks the best
void HCScreen::setLineHeight(uint8_t height) {
  _lineHeight = height;
  //after setting the line height it is required to reinitialize
  //and redisplay the content
  init();
  showContent();
}


//load the first 100 entries from a given path
//if an entry is a directory an asteriks will be shown in front
uint8_t HCScreen::loadDir(fs::FS &fs, String path, uint8_t cnt){
    uint8_t fcnt = cnt;
    File root = fs.open(path);
    if(!root){
        _content[cnt]="Failed to open directory";
        fcnt++;
    }
    else if(!root.isDirectory()){
        _content[cnt]="No directory";
        fcnt++;
    }
    else
    {
      File file = root.openNextFile();
      while(file && (fcnt<100)){
          String name = file.name();
          //strip _path
          uint8_t e = name.lastIndexOf("/");
          name = name.substring(++e);
          if(file.isDirectory()){
              _content[fcnt]="*" + name;
          } else {
            _content[fcnt]=name;
          }
          file = root.openNextFile();
          fcnt++;
      }
    }

  return fcnt;
}

//draw rectangle around selected selected icon
void HCScreen::showGridSelection(uint16_t color) {
  uint8_t x = _selectedColumn * 32 + _gridX;
  uint8_t y = _selectedLine * 32 + _gridY;
  _tft.drawRect(x,y,33,33,color);
}

//draw rectangle around selected selected key
void HCScreen::showKeySelection(uint16_t color) {
  uint8_t x = _selectedColumn * 12 + _keyX;
  uint8_t y = _selectedLine * _lineHeight + _keyY;
  //_tft.drawRect(x,y,13,_lineHeight+1,color);
  _tft.setCursor(x, y);
  _tft.print(">");
}
