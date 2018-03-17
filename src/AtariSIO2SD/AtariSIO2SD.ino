// --------- Program code for the IOS2SD Atari 1050 disk drive emulator -----------
// The program should be compatible with most arduino boards, as well
// as with a bare-bone atmega mcu.
// On boards with the ATmega32U4, logging via the usb uplink is enabled,
// while the the serial communication to the SIO interface 
// is routed over the dedicated serial pins. 

#include <SPI.h>
#include <SD.h>
#include <TimerOne.h>
#include <EEPROM.h>

// serial port RX/TX to Atari is either Serial or Serial1, 
// and if available, use USB line for logging
#if defined(__AVR_ATmega32U4__)
    #define SIO Serial1
    #define PRINT Serial.print
    #define PRINTLN Serial.println
    #define PRINTBEGIN Serial.begin
#else
    #define SIO Serial
    #define PRINT(...)        1
    #define PRINTLN(...)      1
    #define PRINTBEGIN(...)   1
#endif

// ------------------------ the wiring up of all components ------------------------
// -- connection to Atari SIO port
                              // default RX - serial from ATARI
                              // default TX - serial from Atari
#define PIN_SIOCOMMAND  2     // CMD signal from Atari

// -- connection to SDCARD
                              // default MOSI
                              // default MSIO
                              // default SCK
#define PIN_CHIPSELECT  10    // enables SDCARD on SPI bus

// -- connection to LED display and mode selector
#define PIN_DIGIT0 A4         // enables LED digit 0 (right digit)
#define PIN_DIGIT1 3          // enables LED digit 1 (left digit)
// the digit segments are differently assigned for digit 0 and 1  
//           segment:    a   b   c   d   e   f   g   DP
byte pin_segments0[8] = {A1, A0, A2, 6,  A5, 4,  5,  A3}; 
byte pin_segments1[8] = {4,  5,  A5, A0, A1, A3, A2, 6};  
#define COMMON_ANODE_SELECTOR 9  // pull this input low to select common annode LED 

// -- connection to input buttons
#define PIN_BUTTON0  7      // up button   (connects to GND)
#define PIN_BUTTON1  8      // down button (connects to GND)


// ------------------ DISK SELECTOR (LED DIGITS AND BUTTONS) -----------
// because the two digits are displayed in a multiplexted fashion (to reduce number of needed pins),
// they will be displayed alternately. a timer interrupt  comes handy to do this.

#define EEPROM_ADDRESS  99

byte prevbuttonstate;
byte timesincebuttonchange;

volatile byte digit0value;  
volatile byte digit1value;  
volatile int activitylight;
int valuesavedelay;
int savelight;
byte activedigit; 

// various digits             gfedcba
byte digitpatterns[10] = 
{   B00111111,   // 0
    B00000110,   // 1
    B01011011,   // 2
    B01001111,   // 3
    B01100110,   // 4
    B01101101,   // 5
    B01111101,   // 6
    B00000111,   // 7
    B01111111,   // 8
    B01101111    // 9
};

void initdiskselector()
{
    byte i;

    // init input buttons
    pinMode(PIN_BUTTON0, INPUT_PULLUP);
    pinMode(PIN_BUTTON1, INPUT_PULLUP);
    pinMode(COMMON_ANODE_SELECTOR, INPUT_PULLUP);
    
    prevbuttonstate = 0;
    timesincebuttonchange = 0;
  
    // read selector value at startup
    byte s = EEPROM.read(EEPROM_ADDRESS);
    if (s>99) { s=0; }
  
    // init visible digit values and toggle register
    digit0value = s % 10;
    digit1value = s / 10;
    activedigit = 0;
    valuesavedelay = 0;
    savelight = 0;
    activitylight = 0;
  
    // configure and turn of digit selectors 
    pinMode(PIN_DIGIT0, OUTPUT);
    digitalWrite (PIN_DIGIT0, LOW);
    pinMode(PIN_DIGIT1, OUTPUT);
    digitalWrite (PIN_DIGIT1, LOW);  
    // configure and turn off individual digits
    for (i=0; i<8; i++)
    {   pinMode(pin_segments0[i], OUTPUT);
        digitalWrite (pin_segments0[i], LOW);
        pinMode(pin_segments1[i], OUTPUT);
        digitalWrite (pin_segments1[i], LOW);
    }  
    // start timer
    Timer1.initialize(5000);     // call with 200 Hz   
    Timer1.attachInterrupt(polldiskselector);    
}

int getdiskselectorvalue()
{
    return digit0value + digit1value*10;  
}

int setactivitylight()
{
    activitylight = 10;  
}

// interrupt service routine is called 200 times per second 
// (must not take much time)
// this handles user input and also drives the LED digits
void polldiskselector()   
{
    byte buttonstate = 0;
    byte commoncathode;
    
    // --- query current button states ---
    if (digitalRead(PIN_BUTTON0)==LOW) buttonstate =  buttonstate | (1<<0);
    if (digitalRead(PIN_BUTTON1)==LOW) buttonstate =  buttonstate | (1<<1);

    if (buttonstate!=prevbuttonstate)
    {   prevbuttonstate = buttonstate;
        timesincebuttonchange=0;
    }
    // cause buttons to modify digits (either at first press or on repeat)
    if (timesincebuttonchange==0 || timesincebuttonchange==200 && buttonstate!=0) 
    {   // modify digits without needing to do divisions (must be fast)
        if (buttonstate&1) 
        {   digit0value = digit0value<9 ? digit0value+1 : 0;
            if (digit0value==0) { digit1value = digit1value<9 ? digit1value+1 : 0; }
        }
        else if (buttonstate&2) 
        {   digit0value = digit0value>0 ? digit0value-1 : 9;            
            if (digit0value==9) { digit1value = digit1value>0 ? digit1value-1 : 9; }
        }
        valuesavedelay = 600; // 3 seconds before save
    }
    // measure the time the buttons are in current state (and use it for key-repeat)
    if (timesincebuttonchange<200) { timesincebuttonchange++; }
    else                           { timesincebuttonchange-=10; }
 
    // --- handling of delay before saving the current selected value
    if (valuesavedelay>0)
    {   valuesavedelay --;
        if (valuesavedelay==0)
        {   EEPROM.update(EEPROM_ADDRESS, digit0value + digit1value*10);
            savelight = 200;
        }
    }

    // --- count down activity light duration
    if (activitylight>0) {   activitylight--;  }

    // --- count down save light duration
    if (savelight>0) { savelight--; }
  
    // --- display digits alternatingly ----
    // switch to other digit
    activedigit = 1-activedigit;

    // determine if need to drive a common cathode or common anode LED
    commoncathode = digitalRead(COMMON_ANODE_SELECTOR);
  
    // turn on/off digit segments as needed (pin assignment is quite arbitrary)
    if (activedigit==0) 
    {   digitalWrite (PIN_DIGIT1, commoncathode ? HIGH : LOW);  // turn off digit 1
        byte i;
        byte v = digitpatterns[digit0value];
        for (i=0; i<7; i++) 
        {   if (v&(1<<i)) { digitalWrite (pin_segments0[i], commoncathode ? HIGH : LOW); }
            else          { digitalWrite (pin_segments0[i], commoncathode ? LOW : HIGH); }
        }
        // this DOT is used to show activity 
        if (activitylight>0) { digitalWrite (pin_segments0[7], commoncathode ? HIGH : LOW); }
        else                 { digitalWrite (pin_segments0[7], commoncathode ? LOW : HIGH); }
        digitalWrite (PIN_DIGIT0, commoncathode ? LOW : HIGH);  // turn on digit 0
    }
    else 
    {   digitalWrite (PIN_DIGIT0, commoncathode ? HIGH  : LOW);   // turn off digit 0
        byte i;
        byte v = digitpatterns[digit1value];
        for (i=0; i<7; i++) 
        {   if (v&(1<<i)) { digitalWrite (pin_segments1[i], commoncathode ? HIGH : LOW); }
            else          { digitalWrite (pin_segments1[i], commoncathode ? LOW : HIGH); }
        }
        // the DOT is used to show settings saving 
        if (savelight>0) { digitalWrite (pin_segments1[7], commoncathode ? HIGH : LOW); }  
        else             { digitalWrite (pin_segments1[7], commoncathode ? LOW : HIGH); }
        digitalWrite (PIN_DIGIT1, commoncathode ? LOW : HIGH);  // turn on digit 1
    }  
}


// ---------------------- DISK FILE HANDLING  -----------------------

bool didinitsd = false;

File diskfile;  
unsigned int disksize;         // size in sectors

void opendiskfile(byte drive, int index)
{  
    // check if desired file is already open - continue to use it
    if (diskfile && isrequesteddiskfile(diskfile,drive,index)) { return; } 
  
    // before switching disk file, close previous one if still open
    if (diskfile) { diskfile.close(); }
  
    // if not done, try to initialize the SD subsystem
    if (!didinitsd)
    {   pinMode(PIN_CHIPSELECT,OUTPUT);
        digitalWrite(PIN_CHIPSELECT,HIGH);
        if (!SD.begin(PIN_CHIPSELECT)) 
        {   PRINTLN("SDCard failed, or not present");
            return;
        }    
        didinitsd = true;
    }
        
    // try to scan the files in the ATARI directory on the SDCARD and 
    // locate the file name beginning with the correct index (2 digits).
    char fullname[100];
    fullname[0] = '\0';
  
    File root = SD.open(F("ATARI"));
    if (!root || !root.isDirectory())
    {   PRINTLN(F("Can not locate ATARI/ directory"));
        return;             
    }  
    while (fullname[0]=='\0')
    {   File entry = root.openNextFile();
        if (!entry)
        {   PRINT(F("Could not find file for disk "));
            PRINT(index);
            PRINT(F(" in drive "));
            PRINTLN(drive);
            root.close();
            return;
        }
        // check if name matches
        if (isrequesteddiskfile(entry,drive,index))
        { // fill data into the fullname variable (which terminates the loop)
            strcat (fullname, "ATARI/");
            strcat (fullname, entry.name());
        }
        entry.close();
    }      
    root.close();
  
    // try to open the file in read-write mode, and if this does not
    // succeed in read-only mode
    PRINT(F("Trying to open "));
    PRINTLN(fullname);      

    bool readonly = false;
    diskfile = SD.open(fullname, FILE_WRITE);
    if (diskfile)
    {   diskfile.seek(0);
    }
    else
    {   diskfile = SD.open(fullname, FILE_READ);
        readonly = true;
    }  
    // abort if not possible to open
    if (!diskfile)
    {   PRINT(F("Can not open disk file"));
        PRINTLN(fullname);
        return;
    }       
    // read the header
    byte header[16];
    int didread = diskfile.read(header,16);
    if (didread!=16)
    {   PRINTLN(F("Can not read file header"));
        diskfile.close();
        return;
    }
    // check for signature and other settings
    if (header[0]!=0x96 || header[1]!=0x02)
    {   PRINTLN(F("Magic number not present"));
        diskfile.close();
        return;
    }
    if (header[5]!=0 or header[4]!=128)
    {   PRINTLN(F("Can only handle 128 byte sectors"));
        diskfile.close();     
        return;
     }

    unsigned long paragraphs = header[6];
    paragraphs = (paragraphs<<8) | header[3];
    paragraphs = (paragraphs<<8) | header[2];
    if (paragraphs<8 || paragraphs>0x70000)
    {   PRINTLN(F("Disk file size out of range."));
        diskfile.close();
        return;
    }
  
    // seems to be a valid diskfile - use it
    disksize = (paragraphs) >> 3;
    
    PRINT(F("SIZE: "));
    PRINT(disksize);
    PRINT(F(" sectors of 128 bytes"));
    if (readonly) { PRINTLN(F(" (read only)")); }
    else          { PRINTLN(F(" (writeable)")); }
}

bool isrequesteddiskfile(File f, byte drive, int index)
{
    char* n = f.name();
    if (n[0]>='0' && n[0]<='9' && n[1]>='0' && n[1]<='9' && ((n[0]-'0')*10 + n[1]-'0')==index) 
    {   if (n[2]=='_' && drive==0) { return true; }
        if (n[2]>='B' && n[2]<='H' && n[2]-'A'==drive) { return true; }
    }
    return false;
}

bool readsector(unsigned int sector, byte* data)
{   
    if (!diskfile || sector>=disksize) { return false; }
    if (!diskfile.seek(16+((unsigned long)sector)*128) ) { return false; }
    return diskfile.read(data, 128) == 128;
}

bool writesector(unsigned int sector, byte* data)
{
    if (!diskfile || sector>=disksize) { return false; }
    if (!diskfile.seek(16+((unsigned long)sector)*128)) { return false; }
    return diskfile.write(data, 128) == 128;
}

bool diskavailable()
{
    if (diskfile) { return true; }
    else          { return false; }
}


// --------------------- SIO PROTOCOL -----------------------

void sendwithchecksum(byte* data, int len)
{
    int sum = 0;
    int i;
    for (i=0; i<len; i++)
    {   sum += data[i];
        if (sum>=256)
        {  sum = (sum - 256) + 1;  // add carry into sum
        }  
    }
    SIO.write(data,len);
    SIO.write(sum);
}

byte receivebyte()
{
    int d = SIO.read();
    while (d<0)
    {  d = SIO.read();
    }
    return d;
}

bool receivewithchecksum(byte* data, int len)
{
    int sum = 0;
    int i;
    for (i=0; i<len; i++)
    {   data[i] = receivebyte();
        sum += data[i];
        if (sum>=256)
        {   sum = (sum - 256) + 1;  // add carry into sum
        }  
    }
    byte check = receivebyte();
    return check==sum;    
}

void logdata(byte* data, int length)
{
    int i;
    PRINT(F("DATA: "));
    for (i=0; i<length; i++)
    {   PRINT(data[i], HEX);
        PRINT(" ");
    }
    PRINTLN();  
}

void handlecommand_status()
{
    byte status[4];
    if (diskavailable())
    {   
        status[0] = 0x10;  // default status flags byte 0 (motor=on, single density)
        status[1] = 0x00;  // status flags byte 1
        status[2] = 0xe0;  // format timeout 
        status[3] = 0x00;  // copy of internal registers          
        if (disksize>720) status[0] |= 0x80; // enhanced density
    }
    else
    {   status[0] = 0x00;  // status flags byte 0  (motor=off)
        status[1] = 0x80;  // status flags byte 1  (not ready)
        status[2] = 0xe0;  // format timeout 
        status[3] = 0x00;  // copy of internal registers  
    }
    SIO.write('C');  
    sendwithchecksum(status,4);
}

void handlecommand_read(unsigned int sector)
{
    setactivitylight();
 
    byte data[128];
    if (!readsector(sector,data))
    {   SIO.write('E');
        return;
    }      
    SIO.write('C');
    sendwithchecksum(data, 128);    
}

void handlecommand_write(unsigned int sector)
{ 
    byte data[128];
    if (!receivewithchecksum(data,128))
    {   PRINTLN(F("Received sector with invalid checksum"));
        SIO.write('E');
        return;
    } 

    delay(1);
    SIO.write('A');
   
    if (!writesector(sector,data))
    {   PRINTLN(F("Writing sector to SD card failed"));
        SIO.write('E');
        return;
    }      
  
    delay(1);      
    SIO.write('C');
}

void handle_sio()
{
    int i;
    unsigned int drive;
    unsigned int sector;
    
    // must wait until the command line goes low 
    while (digitalRead(PIN_SIOCOMMAND)!=LOW)
    {   int r = SIO.read();  // discard any data garbage that may have arrived
        if (r>=0)
        {   PRINT(F("Received while CMD not active: "));
            PRINTLN(r,HEX);
        }
    }
    // read the command - but abort when command line goes high prematurely
    byte command[5];
    int commandlength=0;
    while (commandlength<5 && digitalRead(PIN_SIOCOMMAND)==LOW)
    {   int r = SIO.read();
        if (r>=0)
        {   command[commandlength] = (byte) r;
            commandlength++;
        }
    }
    // when no command was sent at all, ignore the CMD being low for short time
    if (commandlength<=0) { return; }; 
    
    if (commandlength<5)
    {   PRINT(F("CMD went inactive before receiving full command: "));
        for (i=0; i<commandlength; i++)
        {   PRINT(command[i], HEX);
        }
        PRINTLN();
        return;
    }
    // waiting for command line to go high again, so it is allowed so send
    while (digitalRead(PIN_SIOCOMMAND)!=HIGH)
    {   int r = SIO.read();
        if (r>=0)
        {   commandlength++;
        }
    }
    // log incomming commands
    PRINT(F("CMD: "));
    for (i=0; i<5; i++)
    {   PRINT(command[i], HEX);
        PRINT(F(" "));
    }
    if (commandlength>5) 
    {   PRINT(F("too many bytes: "));
        PRINT(commandlength-5);
    }
    PRINTLN();
    
    // check if command has correct checksum
    int sum = 0;
    for (i=0; i<4; i++)
    {   sum += command[i];
        if (sum>=256)
        {  sum = (sum - 256) + 1;  // add carry into sum
        }
    }
    if (sum!=command[4])
    {   PRINTLN(F("Ignored command with invalid checksum"));
        return;
    }
    // when the command is not intended for floppy device D1 to D3, ignore it
    if (command[0]<0x31 || command[0]>0x33)
    {   PRINT(F("Received command for different device "));
        PRINTLN(command[0], HEX);
        return;       
    }
    
    switch (command[1])
    {   case 0x53:  // STATUS
        {   delay(1);
            SIO.write('A');
            delay(2);
            drive = command[0]-0x31;
            opendiskfile(drive, getdiskselectorvalue());
            handlecommand_status();
            break;
        }
        case 0x52:  // READ
        {   delay(1);
            SIO.write('A');
            delay(2);
            drive = command[0]-0x31;
            sector = ((unsigned int)command[2]) + (((unsigned int)command[3])<<8) - 1;
            opendiskfile(drive, getdiskselectorvalue());
            handlecommand_read(sector);
            break;
        }
        case 0x57:   // WRITE WITH VERIFY
        {   delay(1);
            SIO.write('A');
            delay(2);
            drive = command[0]-0x31;
            sector = ((unsigned int)command[2]) + (((unsigned int)command[3])<<8) - 1;
            opendiskfile(drive, getdiskselectorvalue());
            handlecommand_write(sector);
            break;
        }
        default:
        {   delay(1);
            SIO.write('N');            
            break;
        }
    }
}


// ----------------- INITIALIZATION  AND RUN LOOP ----------------------
void setup() 
{
    // start serial monitor for debugging
    PRINTBEGIN(9600);

    // configure connection to the SIO interface
    pinMode(PIN_SIOCOMMAND,INPUT);
    SIO.begin(19200, SERIAL_8N1);  
  
    // start displaying digits  
    initdiskselector();

}

void loop()
{
    handle_sio();
/*
    delay(1000);
    opendiskfile(0,0);
    
    byte data[128];
    if (readsector(0,data))
    {
        int i;
        for (i=0; i<128; i++)
        {  PRINT(data[i],HEX);
        }
        PRINTLN();
    }
*/    
}



