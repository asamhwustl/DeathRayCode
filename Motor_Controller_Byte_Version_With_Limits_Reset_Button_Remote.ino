//Define pins.
const int xpulpluspin = 4;
const int ypulpluspin = 5;
const int zpulpluspin = 6;
const int dirpluspin = 7;
const int limitpin = 3;

const int lrpin = A0;
const int udpin = A1;
const int gndpin = A2;
const int fivevoltpin = A3;

//Define which bytes signal the start (startbyte) and end (endbyte) of a valid command.
const char startbyte = 0x07;
const char endbyte = 0x0A;

//Define byes and booleans used globally.
byte posorneg;
byte xyorz;

boolean keeppulsing = true;
boolean limithit = false;
boolean gotcommand = false;
boolean remoteinuse = false;
boolean remoteinuselast = false;
boolean remoteused = false;
boolean checkremoteused = false;

//Define joystick values
int lrvalue = 700;
int udvalue = 700;

//When triggered, turns the active motor in the opposite direction it was going until the limitpin pin is no longer high. Then sets limithit to true and keeppulsing to false.
void limit(){
  delay(250);

  if (digitalRead(limitpin)) {
    //Makes sure a motor is active.
    if (gotcommand){
      //Decides which direction is the opposite of the one the motor is currently moving and sets it's movement to this direction.
      if(posorneg == 0) {
          directionnegative();
      }
      else if(posorneg == 1){
        directionpositive();
      }
      else {
      }

      //Chooses which motor is currently active and pulses that one until limitpin is no longer HIGH.
      while(digitalRead(limitpin) != LOW){
        if(xyorz == 0){
          pulse(1, xpulpluspin);
        }
        else if(xyorz == 1){
          pulse(1, ypulpluspin);
        }
        else if(xyorz == 2){
          pulse(1, zpulpluspin);
        }
        else{
        }
      }

      //Sets limithit to true so the main program knows that a limit was hit. Sets keeppulsing to false so that the program does not resume the pulse function once limit has concluded.
      keeppulsing = false;
      limithit = true;
    }
  }
}

//Sends pulses pulses to pin.
void pulse(unsigned long pulses, int pin){
  //Sets how long a pulse is high or low for. The total period of a pulse is pulselength. In microseconds
  const int pulselength = 1000;
  
  //Pulses pin HIGH for pulselength then LOW for pulselength, pulses number of time.
  unsigned long i = 0;
  while((i < pulses) && keeppulsing){
    digitalWrite(pin, HIGH);
    delayMicroseconds(pulselength);
    digitalWrite(pin, LOW);
    delayMicroseconds(pulselength);
    i++;
  }

  //Sets keeppulsing to true so that it will be ready to pulse even if this pulsing was interrupted by limit.
  keeppulsing = true;
}

//Sets the direction pin to the negative direction.
void directionnegative(){
  digitalWrite(dirpluspin, HIGH);
}

//Sets the direction pin to the positive direction.
void directionpositive(){
  digitalWrite(dirpluspin, LOW);
}

void setup() {
  //Enables serial communication
  Serial.begin(9600);

  //Sends the a response that says the Arduino was reset. Only impactful if sent when the motor was moving.
  byte response[4] = {startbyte, 0b11111111, 0b00000000, endbyte};
  Serial.write(response, 4);

  //Sets teh appropriate pins to be able to be used as outputs.
  pinMode(xpulpluspin, OUTPUT);
  pinMode(ypulpluspin, OUTPUT);
  pinMode(zpulpluspin, OUTPUT);
  pinMode(dirpluspin, OUTPUT);

  pinMode(gndpin, OUTPUT);
  digitalWrite(gndpin, LOW);
  pinMode(fivevoltpin, OUTPUT);
  digitalWrite(fivevoltpin, HIGH);
  

  //Sets the appropriate pins to inputs.
  pinMode(limitpin, INPUT);
  pinMode(lrpin, INPUT);
  pinMode(udpin, INPUT);

  //When limitpin experiences a rising edge, triggers the limit() function.
  attachInterrupt(digitalPinToInterrupt(limitpin), limit, HIGH);
}

void loop() {
  
  //This section deals with the Arduino receiving commands. 
  //The command is sent as 8 bytes, with individual functions specified as below:
  //  Byte 0:             The startbyte specified. Checked by the Arduino to make sure what it received is a command.
  //  Byte 1:             Encodes the Motor's axis and turn direction. The first two bits encode the axis: with x being 00, y being 01, and z being 10. 
  //                      The third bit encodes the turn direction: with 0 being positive and 1 being negative.
  //                      The eigth bit encodes whether (1) or not (0) to send back if the remote was used. If it is 1 all other parts of the command are ignored
  //  Byte 2:             Indicates whether or not gotolimit is enabled: with 11111111 being enabled and 00000000 being disabled. 
  //                      When enabled the Arduino will not send an error when a limit is hit.
  //  Bytes 3 through 6:  Encodes the number of pulses the arduino will be sent and therefore how many steps the motor will perform.
  //                      The number will be interpreted by the Arduino as a 32 bit unsigned long, 
  //                      and therefore the maximum number of pulses able to be performed from a single command is 4,294,967,296. Transmitted Big Endian. 
  //                      The equation to get the distance (in inches) the motor will move is: distance = pulses/(ppr*tpi).
  //  Byte 7:             The endbyte specified. Checked by the Arduino to make sure what it received is a command.

  //If the input buffer is not empty and the program is not executing another command at the time, reads through the received bytes and checks each against startbyte. 
  //If the byte matches, reads the next 7 bytes into the command array, then checks whether the last byte in the array is the endbyte. 
  //If so, sets gotcommand to true so the program knows that it has a valid command that it is executing. If not, does nothing more.
  //Alse checks if the arduino recieved a command to return whether the remote was used or not, if so cancels the move command.
  byte command[7] = {0b0, 0b0, 0b0, 0b0, 0b0, 0b0, 0b0};
  if((Serial.available() > 0) && (!gotcommand && !checkremoteused)){
    if(Serial.read() == startbyte){
      Serial.readBytes(command, 7);
      if(command[6] == endbyte){
        if((command[0] & 0b00000001) == 0b00000001){
          checkremoteused = true;
        }
        else{
          gotcommand = true;
        }
      }
      else {
      }
    }
  }

  //Checks if the Arduino has recieved a command to send whether the remote was used
  if(checkremoteused == true){

    //The next secction deals with the Arduino sending a response to a remote query.
    //The response is sent as 4 bytes, with individual functions specified as below:
    //  Byte 0:  The startbyte specified in __init__. Checked by the code to make sure what it received is the Arduino's response.
    //  Byte 1:  Unused
    //  Byte 2:  Stores whether or not the remote was used. The eigth bit is 1 if the remote was used, 0 if not.
    //  Byte 3:  The endbyte specified in __init__. Checked by the code to make sure what it received is the Arduino's response.

    //Creates the basic remoteresponse message that the remote was not used.
    byte remoteresponse[4] = {startbyte, 0b00000000, 0b00000000, endbyte};

    //If the remote was used sets the eigth bit of byte 2 to 1, does nothing if not
    if(remoteused == true){
      remoteresponse[2] = 0b00000001;
    }
    else{}

    //Sends the remoteresponse
    Serial.write(remoteresponse, 4);

    remoteused = false;
    checkremoteused = false;
  }

  //Checks if the Arduino received a valid move command (gotcommand = true) before trying to execute it.
  if(gotcommand){
    //Extracts the direction from command[0] and sets the commanded direction.
    posorneg = ((command[0] & 0b00100000) >> 5);
    if(posorneg == 0) {
      directionpositive();
    }
    else if(posorneg == 1){
      directionnegative();
    }
    else {
    }

    //Extracts the gotolimit flag from command[1] and sets it.
    boolean gotolimit = false;
    if(command[1] == 0b11111111){
      gotolimit = true;
    }

    //Extracts the number of pulses from Bytes 2 through 5 and stores it as the unsigned long pulses.
    unsigned long pulses = (unsigned long)command[5] + ((unsigned long)command[4] << 8) + ((unsigned long)command[3] << 16) + ((unsigned long)command[2] << 24);

    //Sets pulses to the maximum possible value if gotolimit is true.
    if(gotolimit){
      pulses = 0b11111111111111111111111111111111;
    }
    
    //Extracts the axis/motor from command[0] and pulses the appropriate motor.
    xyorz = ((command[0] & 0b11000000) >> 6) ;
    if(xyorz == 0){
      pulse(pulses, xpulpluspin);
    }
    else if(xyorz == 1){
      pulse(pulses, ypulpluspin);
    }
    else if(xyorz == 2){
      pulse(pulses, zpulpluspin);
    }
    else{
    }

    //The next secction deals with the Arduino sending a response.
    //The response is sent as 4 bytes, with individual functions specified as below:
    //  Byte 0:  The startbyte specified in __init__. Checked by the code to make sure what it received is the Arduino's response.
    //  Byte 1:  Stores whether the command was successfully executed (0b11110000), unexpectedly hit a limit (0b00001111), or was reset during movement (0b11111111). 
    //           Includes an error for if none of these conditions were received.
    //  Byte 2:  Unused.
    //  Byte 3:  The endbyte specified in __init__. Checked by the code to make sure what it received is the Arduino's response.

    //By defauls the Arduino sends a response that it was successful in executing the command.
    byte response[4] = {startbyte, 0b11110000, 0b00000000, endbyte};

    //If the limit was hit and the Arduino was not expecting that to happen (gotolimit = False), changes teh response to say that it unexpectedly hit a limit.
    if(limithit && !gotolimit){
      response[1] = 0b00001111;
    }

    //Sends the Arduino's response.
    Serial.write(response, 4);
  }

  //If the arduino does not currently have a command, the joystick remote is able to be used.
  else{
    if(remoteinuselast == false) {
      delay(100);
    }

    gotcommand = true;

    lrvalue = analogRead(lrpin);
    udvalue = analogRead(udpin);

    if((lrvalue - 700) > 200) {
      directionnegative();
      posorneg = 1;
      xyorz = 0;
      pulse(1, xpulpluspin);
      remoteinuse = true;
    }
    else if((lrvalue - 700) < -400){
      directionpositive();
      posorneg = 0;
      xyorz = 0;
      pulse(1, xpulpluspin);
      remoteinuse = true;
    }
    else if((udvalue - 700) > 200) {
      directionpositive();
      posorneg = 0;
      xyorz = 1;
      pulse(1, ypulpluspin);
      remoteinuse = true;
    }
    else if((udvalue - 700) < -300){
      directionnegative();
      posorneg = 1;
      xyorz = 1;
      pulse(1, ypulpluspin);
      remoteinuse = true;
    }

    if (remoteinuse == true) {
      remoteused = true;
    }

    remoteinuselast = remoteinuse;
    remoteinuse = false;
  }

  //Resets the limithit flag if it was changed.
  limithit = false;

  //Changes gotcommand to false to signify that it has finished executing a command and is ready to receive new commands.
  gotcommand = false;

  //Changes checkremoteused to false to signify that it has finished executing a command and is ready to receive new commands.
  checkremoteused = false;
}
