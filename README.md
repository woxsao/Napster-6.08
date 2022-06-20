# Napster 🎶💻
## Monica Chan (mochan@mit.edu)

[demo video] (https://www.youtube.com/watch?v=oAYzYeMKuOo)
## Features Implemented

- Playback mode: plays riff specified in the serial monitor on loop or if button 45 is pressed, choose a new song!
- Record mode: Uses an imu based instrument to make a song and post it to the server!


## List of Functions (more detailed descriptions can be found in the code)
#### Ones given:
- char_append (used in the http get function to append to the response buffer)
- do_http_request (performs an http post/get request)

#### New ones:
- Record Mode: 
    - sm_34(): State machine associated with button 34. Responsible for instrument switching between octaves
    - sm_45(): State machine associated with button 45. Responsible for adding silences or incrementing within the octave
    - sm_39(): State machine associated with button 39. Responsible for adding the note to the riff, or posting the riff to the server
    - convert_riff_string: Converts our final riff to the string format needed for the post request. 
    - post_song: Performs a http request with our formatted body.
    - run_instrument: Runs these functions together and handles the tft in record mode. 
- Playback Mode:
    - playback_mode: runs all the playback functions together
    - getSong: retrieves the song associated with the inputted song id. 
    - playback_sm: Responsible for switching between songs; aka lets the user specify a different song. This state machine is associated with button 45 
- Non-Specific:
    - serialRead: Reads the serial monitor input
    - play_riff_time_blocking: Plays riff while blocking time
    - play_riff: Plays riff without blocking time
    - sm_38: State machine associated with button 38 to switch between playback and record mode. 


### General Structure/Tracing What Happens:
``` cpp
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
```
- The first line checks whether the button at pin 38 has been pressed; it switches modes between playback and record if a press/release sequence is detected. 
- The machine starts in  playback mode by default.
- Play_riff_bool is responsible for either playing the riff on loop or not (aka having the user input a new song id).

##### Inside Playback Mode:
- First calls serialRead to see if the user has inputted a song id. 
- If the serial buffer isn't empty, the device performs a get request to get the riff in string form.
- We then parse the response to look for the note duration (strtok) and then iterate until we hit the end of the string and add all the items to the frequenceis array. 
- Then we set all the individual components of the riff to the global variable riff and play it.
- If Button 45 is pressed, the music should stop and the screen will prompt the user to reenter a song id into the serial monitor, and the cycle repeats. 

##### Inside Record Mode:
- The run_instrument function is the crux of this mode
- First call all the state machines associated with the instrument (sm_34, sm_45, sm_39) to see what we need to set the note to: 
    - If button 34 is pressed+ released, the octave should check if it needs to be increased or decreased based on the tilt of the device. The video is best at describing this behavior. 
    - If button 45 is pressed and released once, then it should increment the note on the screen by one piano key.
    - If button 45 is pressed twice (within one second), then it should add a silent note to the riff. 
    - If button 39 is pressed once, it should add the currently displayed note to the riff, and play the note back.
    - If button 39 is pressed twice, it should push the song to the server under the hard coded user. The user can verify a successful posting via the serial monitor's message. 

### Little notes about using the device:
- The main caveat to this device is the slightly long press on button 38 to switch betwen modes. You need to do a slightly longer press because the serialRead function has a delay in it which, unfortunately, without it the serialRead function won't work properly. In practice, the long press isn't necessary all of the time, but sometimes if you catch it during the delay the press won't register. 
- To send into the serial monitor you have to hit COMMAND + enter, not just enter.


### Important Design notes/Thinking
- I'm a little intruigued by the caveats of the Serial monitor, now that I've had the chance to play with it. I'm not entirely sure why the serial read requires a delay, but multiple arduino forums online have this delay as well. Sadly it does affect the user experience a little bit, given that for the system to work fully a long press is sometimes needed.
- I felt like there were a lot more variables to keep track of and reset in this lab, which made debugging rather tricky.
- I thought the memory constraint issue was interesting (aka passing a Riff as a parameter of a function) caused a stack overflow error. It took me a while to debug that and I'm wondering whether it is good or bad practice to have as many global variables as we do for these projects?


