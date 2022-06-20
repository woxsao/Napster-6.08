#include <mpu6050_esp32.h>
#include<math.h>
#include<string.h>
#include <TFT_eSPI.h> 
#include <SPI.h>
#include <WiFi.h>
TFT_eSPI tft = TFT_eSPI();  

// Minimalist C Structure for containing a musical "riff"
//can be used with any "type" of note. Depending on riff and pauses,
// you may need treat these as eighth or sixteenth notes or 32nd notes...sorta depends
struct Riff {
  double notes[1024]; //the notes (array of doubles containing frequencies in Hz. I used https://pages.mtu.edu/~suits/notefreqs.html
  int length; //number of notes (essentially length of array.
  float note_period; //the timing of each note in milliseconds (take bpm, scale appropriately for note (sixteenth note would be 4 since four per quarter note) then
};

//Kendrick Lamar's HUMBLE
// needs 32nd notes to sound correct...song is 76 bpm, so that's 100.15 ms per 32nd note though the versions on youtube vary by a few ms so i fudged it
Riff humble = {{
    311.13, 311.13, 0 , 0, 311.13, 311.13, 0, 0,  //beat 1
    0, 0, 0, 0, 329.63, 329.63, 0, 0,             //beat 2
    311.13, 311.13, 0, 0, 207.65, 0, 207.65, 0,   //beat 3
    0, 0, 207.65, 207.65, 329.63, 329.63, 0, 0    //beat 4
  }, 32, 100.15 //32 32nd notes in measure, 100.15 ms per 32nd note
};

//Beyonce aka Sasha Fierce's song Formation off of Lemonade. Don't have the echo effect
// needs 16th notes and two measures to sound correct...song is 123 bpm, so that's around 120.95 ms per 16th note though the versions on youtube
// vary even within the same song quite a bit, so sorta I matched to her video for the song.
Riff formation = {{
    261.63, 0, 261.63 , 0,   0, 0, 0, 0, 261.63, 0, 0, 0, 0, 0, 0, 0, //measure 1 Y'all haters corny with that illuminati messssss
    311.13, 0, 311.13 , 0,   0, 0, 0, 0, 311.13, 0, 0, 0, 0, 0, 0, 0 //measure 2 Paparazzi catch my fly and my cocky freshhhhhhh
  }, 32, 120.95 //32 32nd notes in measure, 120.95 ms per 32nd note
};

//Justin Bieber's Sorry:
// only need the 16th notes to make sound correct. 100 beats (notes) per minute in song means 150 ms per 16th note
// riff starts right at the doo doo do do do do doo part rather than the 2-ish beats leading up to it. That way you
// can go right into the good part with the song. Sorry if that's confusing.
Riff sorry = {{ 1046.50, 1244.51 , 1567.98, 0.0, 1567.98, 0.0, 1396.91, 1244.51, 1046.50, 0, 0, 0, 0, 0, 0, 0}, 16, 150};


//create a song_to_play Riff that is one of the three ones above. 
Riff song_to_play = formation;  //select one of the riff songs


const uint32_t READING_PERIOD = 150; //milliseconds
double MULT = 1.059463094359; //12th root of 2 (precalculated) for note generation
double A_1 = 55; //A_1 55 Hz  for note generation
const uint8_t NOTE_COUNT = 97; //number of notes set at six octaves from

//buttons! and their pins
uint8_t BUTTON1 = 45;
uint8_t BUTTON2 = 39;
uint8_t BUTTON3 = 38;
uint8_t BUTTON4 = 34;

//pins for LCD and AUDIO CONTROL
uint8_t LCD_CONTROL = 21;
uint8_t AUDIO_TRANSDUCER = 14;

//PWM Channels. The LCD will still be controlled by channel 0, we'll use channel 1 for audio generation
uint8_t LCD_PWM = 0;
uint8_t AUDIO_PWM = 1;

//arrays you need to prepopulate for use in the run_instrument() function
double note_freqs[NOTE_COUNT];

Riff song; //riff for playback node
double frequencies[1024];
int counter;

//RECORDING VARIABLES-----------------------------------------------
//global variables to help your code remember what the last note was to prevent double-playing a note which can cause audible clicking
char octave_notes[12][4] = {"A", "Bf", "B", "C", "Df", "D", "Ef", "E", "F", "Gf", "G", "Af"}; //name of the notes in an octave, f indicates a flat
float new_note = 0;
float old_note = 0;
int octave = 0; //keeps track of which octave we're in (0 indicates first)
int note_counter = 0; //keeps track of which note we're on, (0 indicates 'A')
double record_note_duration; //user specified recording note duration
Riff recorded_song;
double record_song[1024]; //stores frequencies as we record
int record_song_index;
bool added_silence = 0;
int riff_player_counter = 0;
unsigned long riff_player_timer;
char str_song_rep[7000]; //converts our riff into a proper string
char body[7000]; //for body of the http post request

//STATE MACHINE VARIABLES----------------------------------------------
const int IDLE = 0;
const int PRESS = 1;
const int RELEASE = 2;
const int SECOND_PRESS = 3;
const int SECOND_RELEASE = 4;
int state_39 = IDLE;
int state_45 = IDLE;
int state_38 = IDLE;
int state_34 = IDLE;
int playback_state = IDLE;
bool playback = 1;
bool play_riff_bool = 0;
bool reset_riff_timer = 0;
unsigned long button_45_timer;
unsigned long button_39_timer;

unsigned long note_timer;
MPU6050 imu; //imu object called, appropriately, imu
float x, y, z; //variables for grabbing x,y,and z values



//WIFI-------------------------------------------------------
const char USER[] = "mochi";
//byte bssid[] = {0x5C, 0x5B, 0x35, 0xEF, 0x59, 0xC3}; //6 byte MAC address of AP you're targeting. Next House 5 west
byte bssid[] = {0x5C, 0x5B, 0x35, 0xEF, 0x59, 0x03}; //3C
//byte bssid[] = {0xD4, 0x20, 0xB0, 0xC4, 0x9C, 0xA3}; //quiet side stud
char network[] = "MIT";
char password[] = "";
const int RESPONSE_TIMEOUT = 6000; //ms to wait for response from host
const int POSTING_PERIOD = 6000; //ms to wait between posting step
const uint16_t IN_BUFFER_SIZE = 7000; //size of buffer to hold HTTP request
const uint16_t OUT_BUFFER_SIZE = 7000; //size of buffer to hold HTTP response
char request_buffer[IN_BUFFER_SIZE]; //char array buffer to hold HTTP request
char response_buffer[OUT_BUFFER_SIZE]; //char array buffer to hold HTTP response
unsigned long posting_timer;


void setup() {
  Serial.begin(115200);
  while (!Serial); // wait for Serial to show up
  Wire.begin();
  delay(50); //pause to make sure comms get set up
  if (imu.setupIMU(1)) {
    Serial.println("IMU Connected!");
  } else {
    Serial.println("IMU Not Connected :/");
    Serial.println("Restarting");
    ESP.restart(); // restart the ESP (proper way)
  }
  tft.init(); //initialize the screen
  tft.setRotation(2); //set rotation for our layout
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  //wifi
  WiFi.begin(network, password, 1, bssid);
  uint8_t count = 0; //count used for Wifi check times
  Serial.print("Attempting to connect to ");
  Serial.println(network);
  while (WiFi.status() != WL_CONNECTED && count<12) {
    delay(500);
    Serial.print(".");
    count++;
  }
  delay(2000);
  if (WiFi.isConnected()) { //if we connected then print our IP, Mac, and SSID we're on
    Serial.println("CONNECTED!");
    Serial.println(WiFi.localIP().toString() + " (" + WiFi.macAddress() + ") (" + WiFi.SSID() + ")");
    delay(500);
  } else { //if we failed to connect just Try again.
    Serial.println("Failed to Connect :/  Going to restart");
    Serial.println(WiFi.status());
    ESP.restart(); // restart the ESP (proper way)
  }
  double note_freq = A_1;
  //fill in note_freq with appropriate frequencies from 55 Hz to 55*(MULT)^{NOTE_COUNT-1} Hz
  for(int i = 0; i < NOTE_COUNT; i++){
    note_freqs[i] = note_freq;
    note_freq *= MULT;
  }
  //print out your accelerometer boundaries as you make them to help debugging
  //Serial.printf("Accelerometer thresholds:\n");
  //fill in accel_thresholds with appropriate accelerations from -1 to +1
  
  //start new_note as at middle A or thereabouts.
  //new_note = note_freqs[NOTE_COUNT - NOTE_COUNT / 2]; //set starting note to be middle of range.

  //four pins needed: two inputs, two outputs. Set them up appropriately:
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  pinMode(BUTTON3, INPUT_PULLUP);
  pinMode(BUTTON4, INPUT_PULLUP);

  pinMode(AUDIO_TRANSDUCER, OUTPUT);
  pinMode(LCD_CONTROL, OUTPUT);

  //set up AUDIO_PWM which we will control in this lab for music:
  ledcSetup(AUDIO_PWM, 200, 12);//12 bits of PWM precision
  ledcWrite(AUDIO_PWM, 0); //0 is a 0% duty cycle for the NFET
  ledcAttachPin(AUDIO_TRANSDUCER, AUDIO_PWM);

  //set up the LCD PWM and set it to 
  pinMode(LCD_CONTROL, OUTPUT);
  ledcSetup(LCD_PWM, 100, 12);//12 bits of PWM precision
  ledcWrite(LCD_PWM, 0); //0 is a 0% duty cycle for the PFET...increase if you'd like to dim the LCD.
  ledcAttachPin(LCD_CONTROL, LCD_PWM);


  //play A440 for testing.
  //comment OUT this line for the latter half of the lab.
  //ledcWriteTone(AUDIO_PWM, 440); //COMMENT THIS OUT AFTER VERIFYING WORKING
  //delay(2000);
  //ledcWriteTone(AUDIO_PWM, 0); //COMMENT THIS OUT AFTER VERIFYING WORKING

}

/*
sm_34()-------------------------------
This button is a state machine for Button at pin 34 when in instrument mode! This button is responsible for shifting the octave
Parameters:
* button_val: 0 or 1 (pressed or unpressed)
*/
void sm_34(int button_val){
  switch(state_34){
    case IDLE:
      if(button_val == 0)
        state_34 = PRESS;
      break;
    case PRESS:
      if(button_val ==1)
        state_34 = RELEASE;
      break;
    case RELEASE:
      state_34 = IDLE;
      imu.readAccelData(imu.accelCount);
      x = imu.accelCount[0] * imu.aRes; //Reads x value of imu
      if(x < 0 && octave > 0) //decrease octave
        octave-=1;
      else if(x <0 && octave == 0) //if you're already in lowest octave you cant go lower
        Serial.println("you're in the lowest octave!");
      else if(x > 0 && octave == 7)
        Serial.println("you're in the highest octave!");       
      else //increase octave!
        octave += 1;
      
      note_counter = 0; //reset note to A
      break;
            
  }
}

/*
sm_45()-------------------------------
This button is a state machine for Button at pin 45 when in instrument mode! This button is responsible for shifting the notes within an octave. A second press adds a silent note to the riff. 
Parameters:
* button_val: 0 or 1 (pressed or unpressed)
*/
void sm_45(int button_val){
  switch(state_45){
    case IDLE:
      if(button_val == 0)
        state_45 = PRESS;
      break;
    case PRESS:
      if(button_val == 1){
        button_45_timer = millis();
        state_45 = RELEASE;
      }
      break;
    case RELEASE:
      if(millis()-button_45_timer<1000){ //waiting to detect a second press
        if(button_val == 0){
          state_45 = SECOND_PRESS;
          break;
        }
      }    
      else{
        state_45 = IDLE;
        if (note_counter >= 11){ //shift octave if you're at the highest note within the octave
          if(octave < 7)
            octave += 1;
          note_counter = 0;
        }
        else
          note_counter += 1;
      }
      new_note = note_freqs[(12*octave)+ note_counter];
      break; 
      
    case SECOND_PRESS:
      if(button_val == 1)
        state_45 = SECOND_RELEASE;
             
      break;
    case SECOND_RELEASE:
      Serial.println("ADDED 0!");
      state_45 = IDLE;
      added_silence = 1;
      record_song[record_song_index] = 0;
      if(record_song_index <1024)
          record_song_index += 1;
      else
          Serial.println("Your song is too long.");
      break;
  }
}

/*
sm_39()-------------------------------
This button is a state machine for Button at pin 39 when in instrument mode! This button is responsible for adding the  note to the riff, or if a double press occurs, it plays the riff back and posts it to the server. 
Parameters:
* button_val: 0 or 1 (pressed or unpressed)
*/
void sm_39(int button_val){
  switch(state_39){
    case IDLE:
      if(button_val == 0)
        state_39 = PRESS;        
      break;
    case PRESS:
      if(button_val == 1){
        button_39_timer = millis();
        state_39 = RELEASE;
      }
          
      break;
    case RELEASE:
      if(millis()-button_39_timer <1000){ //double press check
        if(button_val == 0)
          state_39 = SECOND_PRESS;
      }
      else{
        tft.fillScreen(TFT_BLACK);
        added_silence = 0;
        new_note = note_freqs[(12*octave)+ note_counter];
        Serial.printf("new note: %lf \n", new_note);
        record_song[record_song_index] = new_note; //adding to our song array
        if(record_song_index <1024)
          record_song_index += 1;
        else
          Serial.println("Your song is too long.");
        note_timer = millis();
        ledcWriteTone(AUDIO_PWM, new_note);
        while(millis()-note_timer < record_note_duration){} //plays back the note added
        ledcWriteTone(AUDIO_PWM, 0);
        state_39 = IDLE;
      }    
      
      break;
    case SECOND_PRESS:
      if(button_val ==1)
        state_39 = SECOND_RELEASE;
      break;
    case SECOND_RELEASE:
      memcpy(recorded_song.notes, record_song, sizeof(record_song)); //starts adding info to the riff
      recorded_song.note_period = record_note_duration;
      recorded_song.length = record_song_index;
      song_to_play = recorded_song; //updating global variable
      play_riff_time_blocking(); //run time blocking riff so you can hear the whole thing :)
      
      convert_riff_string(str_song_rep); //format the string properly
      Serial.printf("%s \n", str_song_rep); //print out the string so you can see it
      post_song(str_song_rep);
      state_39 = IDLE;
      memset(record_song, 0, sizeof(record_song)); //empty the song for the next one

      //reset a bunch of things
      octave = 0;
      new_note = note_freqs[0];
      old_note = 0;      
      record_song_index = 0;
      note_counter = 0;    
      break;
  }
}

/*
convert_riff_string-------------------------------
This function is responsible for converting the recorded riff to a string
Parameters:
* char* str_song buffer to carry the formatted body
*/
void convert_riff_string(char* str_song){
  memset(str_song, 0, sizeof(str_song));
  sprintf(str_song, "%lf&", recorded_song.note_period); //adding duration first
  for(int i = 0; i < recorded_song.length; i++){
    char temp[30];
    if(i <recorded_song.length-1)
      sprintf(temp, "%lf,", recorded_song.notes[i]);
    else
      sprintf(temp, "%lf", recorded_song.notes[i]);
    strcat(str_song, temp);
  }  
}


/*
post_song-------------------------------
This function is responsible for posting the song to the server by formatting the proper request and calling do_http_request
Parameters:
* char* final_song buffer is our final formatted song
*/
void post_song(char* final_song){
  if (millis()-posting_timer > POSTING_PERIOD){
    
    sprintf(body,"{\"artist\":\"%s\",\"song\":\"%s\"}", USER, final_song);//generate body, posting to User, 1 step
    int body_len = strlen(body); //calculate body length (for header reporting)
    sprintf(request_buffer,"POST http://iesc-s3.mit.edu/esp32test/limewire HTTP/1.1\r\n");
    strcat(request_buffer,"Host: iesc-s3.mit.edu\r\n");
    strcat(request_buffer,"Content-Type: application/json\r\n");
    sprintf(request_buffer+strlen(request_buffer),"Content-Length: %d\r\n", body_len); //append string formatted to end of request buffer
    strcat(request_buffer,"\r\n"); //new line from header to body
    strcat(request_buffer,body); //body
    strcat(request_buffer,"\r\n"); //new line
    Serial.println(request_buffer);
    do_http_request("iesc-s3.mit.edu", request_buffer, response_buffer, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT,true);
    Serial.println(response_buffer); //viewable in Serial Terminal
    //LCD Display:
    posting_timer = millis();

  }  
}

/*
run_instrument-------------------------------
This function is responsible for running the instrument and all of its various moving parts
*/
void run_instrument() {
  sm_34(digitalRead(BUTTON4)); 
  if(added_silence == 0)
    new_note = note_freqs[(12*octave)+ note_counter];
  sm_45(digitalRead(BUTTON1)); //The state machine for adding blank note or incrementing the note by 1
  sm_39(digitalRead(BUTTON2)); //The state machine for adding a note to the riff or finishing the riff
  
  tft.setCursor(0,0,2);
  if(new_note!= 0){ //print the current note to tft; dont print anything if silence was just added. 
    new_note = note_freqs[(12*octave)+ note_counter];
    tft.printf("octave: %d \n", octave+1);  
    tft.printf("note: %s \n", octave_notes[note_counter]);
    tft.printf("frequency: %.3f \n", new_note);
  }
  
    
}

/*
playback_mode-------------------------------
This function is responsible for reading the serial monitor using serialRead and retrieving it with a GET request, then playing it on loop.
*/
void playback_mode(){
  
  char song_id[100];
    
  song_id[0] = 0;
  
  serialRead(song_id);

  if(strcmp(song_id, "\0")!=0){ //if you actually have an ID; otherwise dont do the get request
    getSong(song_id);
    Serial.println("song: ");
    Serial.println(response_buffer);

    //parsing the response to format it into our riff
    char* note_duration_ptr = strtok(response_buffer, "&");
    float note_duration = atof(note_duration_ptr);
    char* ptr = strtok(NULL, ",");
    
    counter = 0;
    while(ptr != NULL){
      frequencies[counter] = atof(ptr);
      ptr = strtok(NULL, ",");
      counter+=1;
    }
    memcpy(song.notes, frequencies, sizeof(frequencies));
    song.note_period = note_duration;
    song.length = counter;
    play_riff_bool = 1;
    memset(frequencies, 0, sizeof(frequencies));
    
  }

}

/*
getSong-------------------------------
This function is responsible for performing a get request to get the riff from limewire. 
Parameters:
* char* song_id buffer to carry the string form of the song id
*/
void getSong(char* song_id){
  memset(request_buffer, 0, sizeof(request_buffer));
  sprintf(request_buffer,"GET http://iesc-s3.mit.edu/esp32test/limewire?song_id=%s HTTP/1.1\r\n", song_id);
  strcat(request_buffer,"Host: iesc-s3.mit.edu\r\n");
  strcat(request_buffer,"\r\n"); //new line from header to body
  do_http_request("iesc-s3.mit.edu", request_buffer, response_buffer, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT,false);
    
}

/*
serialRead-------------------------------
This function is responsible for reading the serial monitor/ 
Parameters:
* char* buffer to carry whatever is in the serial monitor. 
*/
void serialRead(char* buffer){
  delay(500); //without this delay the serial system can't catch up to itself; need this unfortunately. 
  int avail = Serial.available();
  char tempBuffer[2000];
  for(int i=0; i< avail; i++){
      sprintf(tempBuffer, "%c", Serial.read());
      strcat(buffer, tempBuffer);
  }
}


//make the riff player. Play whatever the current riff is (specified by struct instance song_to_play)
//function can be blocking (just for this lab :) )

/*
play_riff_time_blocking()-------------------------------
This function is responsible for playing the riff by blocking time (aka can't hit button while this is running sadly)
*/
void play_riff_time_blocking() {
  tft.fillScreen(TFT_BLACK);
  for(int i = 0; i < song_to_play.length; i++){
    new_note = song_to_play.notes[i];
    tft.setCursor(0,0,4);
    tft.println(song_to_play.notes[i]);
    if(new_note != old_note){
      ledcWriteTone(AUDIO_PWM, new_note);
    }
    note_timer = millis();
    while(millis()-note_timer <= song_to_play.note_period){}
    old_note = new_note;
  }
  ledcWriteTone(AUDIO_PWM, 0);
  tft.fillScreen(TFT_BLACK);
}

/*
play_riff()-------------------------------
This function is responsible for playing the riff without blocking time!!!
*/
void play_riff() {
  if(riff_player_counter==0){
    tft.fillScreen(TFT_BLACK);
  }
    
  if(riff_player_counter < song_to_play.length){
    new_note = song_to_play.notes[riff_player_counter];
    tft.setCursor(0,0,4);
    tft.println(song_to_play.notes[riff_player_counter]);
    if(reset_riff_timer == 1){
      riff_player_timer = millis();
      reset_riff_timer =0;
    }
    if(new_note != old_note){
      ledcWriteTone(AUDIO_PWM, new_note);
      old_note = new_note;
    }
    if(millis()-riff_player_timer >= song_to_play.note_period){
      riff_player_counter+=1;
      riff_player_timer = millis();
    }
    
  }
  else{
    ledcWriteTone(AUDIO_PWM, 0);
    riff_player_counter = 0;
    new_note = 0;
    old_note = 0;
  }
  
}
/*----------------------------------
 * do_http_request Function:
 * Arguments:
 *    char* host: null-terminated char-array containing host to connect to
 *    char* request: null-terminated char-arry containing properly formatted HTTP request
 *    char* response: char-array used as output for function to contain response
 *    uint16_t response_size: size of response buffer (in bytes)
 *    uint16_t response_timeout: duration we'll wait (in ms) for a response from server
 *    uint8_t serial: used for printing debug information to terminal (true prints, false doesn't)
 * Return value:
 *    void (none)
 */
void do_http_request(char* host, char* request, char* response, uint16_t response_size, uint16_t response_timeout, uint8_t serial){
  WiFiClient client; //instantiate a client object
  if (client.connect(host, 80)) { //try to connect to host on port 80
    if (serial) Serial.print(request);//Can do one-line if statements in C without curly braces
    client.print(request);
    memset(response, 0, response_size); //Null out (0 is the value of the null terminator '\0') entire buffer
    uint32_t count = millis();
    while (client.connected()) { //while we remain connected read out data coming back
      client.readBytesUntil('\n',response,response_size);
      //if (serial) Serial.println(response);
      if (strcmp(response,"\r")==0) { //found a blank line!
        break;
      }
      memset(response, 0, response_size);
      if (millis()-count>response_timeout) break;
    }
    memset(response, 0, response_size);  
    count = millis();
    while (client.available()) { //read out remaining text (body of response)
      char_append(response,client.read(),OUT_BUFFER_SIZE);
    }
    if (serial) Serial.println(response);
    client.stop();
    if (serial) Serial.println("-----------");  
  }else{
    if (serial) Serial.println("connection failed :/");
    if (serial) Serial.println("wait 0.5 sec...");
    client.stop();
  }
}        

/*----------------------------------
 * char_append Function:
 * Arguments:
 *    char* buff: pointer to character array which we will append a
 *    char c: 
 *    uint16_t buff_size: size of buffer buff
 *    
 * Return value: 
 *    boolean: True if character appended, False if not appended (indicating buffer full)
 */
uint8_t char_append(char* buff, char c, uint16_t buff_size) {
        int len = strlen(buff);
        if (len>buff_size) return false;
        buff[len] = c;
        buff[len+1] = '\0';
        return true;
}

/*
sm_38()-------------------------------
This button is a state machine for Button at pin 38 that's responsible for toggling between modes. 
Parameters:
* button_val: 0 or 1 (pressed or unpressed)
*/
void sm_38(int button_val){
  switch(state_38){
    case IDLE:
      if(button_val == 0)
        state_38 = PRESS;
      break;
    case PRESS:
      if(button_val == 1)
        state_38 = RELEASE;
      break;
    case RELEASE:
      tft.fillScreen(TFT_BLACK);
      if(playback){
        if(play_riff_bool){
          riff_player_counter =0;
        }        
      }
      playback = !playback;
      ledcWriteTone(AUDIO_PWM, 0);
      if(playback == 0){
        char note_d[10] = "";
        tft.setCursor(0,0,2);
        tft.println("enter the note\n duration you want \n (in the serial \n monitor)  \n up to 3000 ms with \n two decimals");
        while(strlen(note_d) == 0){
          serialRead(note_d);          
        }
        record_note_duration = atof(note_d);
        record_song_index = 0;
        octave = 0;
        Serial.printf("\n note duration specified: %lf \n", record_note_duration);
        tft.fillScreen(TFT_BLACK);
      }
      else{
        play_riff_bool = 0;
      }
        
      state_38 = IDLE;    
      break;
    }    
}

/*
playback_sm()-------------------------------
This button is a state machine for Button at pin 45 when in PLAYBACK mode! This way you can swithc between playing a riff on loop and choosing a new song.
Parameters:
* button_val: 0 or 1 (pressed or unpressed)
*/
void playback_sm(int button_val){
  switch(playback_state){
    case IDLE:
      if(button_val ==0)
        playback_state = PRESS;
      break;
    case PRESS:
      if(button_val==1)
        playback_state = RELEASE;
      break;
    case RELEASE:
      ledcWriteTone(AUDIO_PWM, 0);
      tft.fillScreen(TFT_BLACK);
      riff_player_counter = 0;
      new_note = 0;
      old_note  = 0;
      play_riff_bool = !play_riff_bool; //toggle whether to play the riff or not
      if(play_riff_bool){
        reset_riff_timer = 1;
      }
      playback_state = IDLE;
      break;
  }
}
void loop() {  
  sm_38(digitalRead(BUTTON3));
  if(playback==0)
    run_instrument();
  else{
    tft.setCursor(0,0,2);
    
    playback_sm(digitalRead(BUTTON1));
    if(play_riff_bool == 0){ //entering a new song
      tft.println("Enter a valid song \n id (in serial \n monitor)");
      playback_mode();
    }
    else{ //otherwise play the one on loop
      song_to_play = song;
      play_riff();
    }  
  }
}



