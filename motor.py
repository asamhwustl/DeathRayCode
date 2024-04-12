import numpy
import time

#Creates a Motor object that manages a specific axis motor from 'x', 'y', or 'z'. 
class Motor:
    #Each Motor needs to have a specified axis to control and a serial port to communicate over. 
    #Each motor can also be instantiated with a different ppr (pulses per rotation) and pos (initial position). ppr defaults  The position is tracked in inches.
    #Each motor also has:
    #    Its tpi (turns of the motor per inch of movement)
    #    The startbyte and endbyte for communication with the Arduino
    #    The posmin (minimum) and posmax (maximum) values the position can take. When a limit switch is hit the Arduino will send a message and pos will be set to the appropriate value (posmax or posmin).
    #The above values are assumed to be consistent across motors and are not natively provided as initializable values.
    def __init__(self, axis, ser, ppr=400, pos=None):
        self.axis = axis
        self.ser = ser
        self.ppr = ppr
        self.pos = pos
        
        self.tpi = 625/123
        self.startbyte = 0x07
        self.endbyte = 0x0a
        self.posmin = 0
        self.posmax = 9.8
        
    #Sends a message to the motor to move as specified, relative to its current position.
    #direction specifies which direction the motor will turn.
    #distance specifies how far the position will move. Defaults to zero.
    #unit specifies the unit for distance. Defaults to 'in' (inches). Other options are: 'mm' (millimeters) and 'cm' (centimeters).
    #gotolimit, when True, specifies that the motor should move the maximum possible distance until a limit switch is triggered. Defaults to False.
    def move(self, direction, distance = 0, unit='in', gotolimit=False):
        
        #The next section translates the desired action into a command that the Arduino code will understand.
        #The command is sent as 8 bytes, with individual functions specified as below:
        #    Byte 0:             The startbyte specified in __init__. Checked by the Arduino to make sure what it received is a command.
        #    Byte 1:             Encodes the Motor's axis and turn direction. The first two bits encode the axis: with x being 00, y being 01, and z being 10. 
        #                        The third bit encodes the turn direction: with 0 being positive and 1 being negative.
        #                        The eigth bit encodes whether (1) or not (0) to send back if the remote was used. If it is 1 all other parts of the command are ignored
        #    Byte 2:             Indicates whether or not gotolimit is enabled: with 11111111 being enabled and 00000000 being disabled. 
        #                        When enabled the Arduino will not send an error when a limit is hit.
        #    Bytes 3 through 6:  Encodes the number of pulses the arduino will be sent and therefore how many steps the motor will perform.
        #                        The number will be interpreted by the Arduino as a 32 bit unsigned long, 
        #                        and therefore the maximum number of pulses able to be performed from a single command is 4,294,967,296. Transmitted Big Endian. 
        #                        The equation to get the distance (in inches) the motor will move is: distance = pulses/(ppr*tpi).
        #    Byte 7:             The endbyte specified in __init__. Checked by the Arduino to make sure what it received is a command.

        #Encodes axis into Byte 1. If an axis not 'x', 'y', or 'z' is specified, throws an error.
        onebyte = 0b00000000
        if self.axis == 'x':
            onebyte += 0b00000000
        elif self.axis == 'y':
            onebyte += 0b01000000
        elif self.axis == 'z':
            onebyte += 0b10000000
        else:
            raise Exception("The specified axis is not available, please enter 'x', 'y', or 'z'.")
        
        #Encodes direction into Byte 1. If a direction not '+' or '-' is specified, throws an error.
        if direction == '+':
            onebyte += 0b00000000
        elif direction == '-':
            onebyte += 0b00100000
        else:
            raise Exception("The specified direction is not available, please enter '+' or '-'.")
        
        #Encodes gotolimit into Byte 2.
        twobyte = 0b00000000
        if gotolimit:
            twobyte += 0b11111111
        
        #Direction is not specified through distance's sign, so only positive numbers are accepted. If the distance is negative, throws an error.
        if distance < 0:
            raise Exception("Please enter a positive distance.")
        
        #Converts the distance in other units into the distance in inches. If a unit not 'in', 'cm', or 'mm' is specified, throws an error.
        inchdistance = 0
        if unit == 'in':
            inchdistance += distance
        elif unit == 'cm':
            inchdistance += distance/2.54
        elif unit == 'mm':
            inchdistance += distance/25.4
        else:
            raise Exception("The specified unit is not available, please enter 'in', 'cm', or 'mm'.")
        
        #Reverses the above pulses to distance formula to calculate the number of pulses needed for the motor to move the desired distance.
        pulses = inchdistance*self.ppr*self.tpi
        
        #makes sure that the motor can actually move the specified pulses.
        if pulses > 4294967296:
            raise Exception("The entered distance is too large to be completed in one step, please break the movement into multiple commands.")
        
        #Converts the calculated pulses into four bytes        
        threebyte = (int(numpy.round(pulses)) & 0b11111111000000000000000000000000) >> 24
        
        fourbyte = (int(numpy.round(pulses)) & 0b00000000111111110000000000000000) >> 16
        
        fivebyte = (int(numpy.round(pulses)) & 0b00000000000000001111111100000000) >> 8
        
        sixbyte = int(numpy.round(pulses)) & 0b0000000000000000000000011111111
        
        #Consolidates the eight command bytes into an array.
        command = [self.startbyte, onebyte, twobyte, threebyte, fourbyte, fivebyte, sixbyte, self.endbyte]
        
        #Clears the serial read buffer to make sure there were no random transmissions.
        self.ser.reset_input_buffer()

        #Sends the command to the Arduino.
        self.ser.write(command)
        
        #The next section receives and processes the Arduino's response
        #The response is sent as 4 bytes, with individual functions specified as below:
        #    Byte 0:  The startbyte specified in __init__. Checked by the code to make sure what it received is the Arduino's response.
        #    Byte 1:  Stores whether the command was successfully executed (0b11110000), unexpectedly hit a limit (0b00001111), or was reset during movement (0b11111111). 
        #             Includes an error for if none of these conditions were received.
        #    Byte 2:  Unused.
        #    Byte 3:  The endbyte specified in __init__. Checked by the code to make sure what it received is the Arduino's response.
        
        #Creates an array to hold the Arduino's response
        response = [0b0, 0b0, 0b0, 0b0]
        
        #Once the Arduino succeeds or fails at performing a command, it sends a response which is read into the response array here.
        self.ser.readinto(response)

        #Checks to make sure what he code read in is a valid recponse.
        if (response[0] != self.startbyte) or (response[3] != self.endbyte):
            raise Exception("The Arduino sent something not in the correct format")
        
        #Checks the various conditions of Byte 1 as described above.
        elif response[1] != 0b11110000:
            if response[1] == 0b00001111:
                raise Exception("Unexpectedly hit a limit.")
                
            if self.pos != None:
                if direction == '+':
                    self.pos = self.posmax
                else:
                    self.pos = self.posmin
            
            elif response[1] == 0b11111111:
                self.pos = None
                raise Exception("The Arduino was reset during movement. If using absolute positioning, the program has no idea where it is now.")
            
            else:
                raise Exception("The Arduino sent something in the correct format but it's not a success or an unexpectedly hit limit.")
        
        #This section deals with absolute positioning.
        else:
            #If the gotolimit flag is True, uses what direction the motor is turning to set the position to posmax or posmin.
            if gotolimit:
                if direction == '+':
                    self.pos = self.posmax
                else:
                    self.pos = self.posmin
            
            #If the position has not been set, either by using tolimit or move(gotolimit = True) or initializing the Motor to a certain position (Motor(pos = x)), nothing happens. 
            #If the position has been set to something adds or subtracts the moved distance appropriately depending on the movement direction.
            elif (self.pos != None):
                if direction == '+':
                    self.pos += inchdistance
                else:
                    self.pos -= inchdistance
        
        #Clears the serial read buffer to make sure the remote used transmission is the first one received by future move commands.
        self.ser.reset_input_buffer()
    
    #Moves a motor in the direction specified until it hits a limit switch. Used to enable absolute positioning if not already enabled.
    def tolimit(self, direction):
        
        self.move(direction, gotolimit = True)
    
    #Moves the motor to the specified coordinate if absolute positioning is enabled.
    def moveto(self, distance, unit='inch'):
        #This section sends the arduino a command to check if the remote has been used
        #Consolidates the eight remotecheckcommand bytes into an array with byte1 set to 0b00000001 to tell the arduino to send a remoteused response.
        remotecheckcommand = [self.startbyte, 0b00000001, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, self.endbyte]

        self.ser.reset_input_buffer()

        #Sends the remotecheckcommand
        self.ser.write(remotecheckcommand)

        #This section checks if the remote has been used
        #Creates an array to hold the Arduino's remote used message.
        remoteused = [0b0, 0b0, 0b0, 0b0]
        
        #If the remote has been used, it sends a message saying so, which is read into remoteused.
        self.ser.readinto(remoteused)
        
        #Checks to make sure what he code read in is a valid recponse.
        if (remoteused[0] != self.startbyte) or (remoteused[3] != self.endbyte):
            raise Exception("The Arduino sent what should be a remote used message, but it was not in the correct format")
        
        #Checks what the arduino sent and clears the position value if the remote was used.
        if remoteused[2] == 0b00000001:
            self.pos = None
            #raise Exception("TEST THE CODE REACHED HERE 1")
        elif remoteused[2] == 0b00000000:
            #do nothing command
            #raise Exception("TEST THE CODE REACHED HERE 2")
            remoteused[2] = 0b00000000
        else:
            raise Exception("The Arduino sent something in the correct format but it's not a message that the remote was used.")
        
        self.ser.reset_input_buffer()
        
        #Checks to make sure the motor knows wher it is before moving. Raises an error if not.
        if self.pos == None:
            raise Exception("Use tolimit() to establish current location before using absolute positioning and position().")

        #Converts the distance in other units into the distance in inches. If a unit not 'in', 'cm', or 'mm' is specified, throws an error.
        inchdistance = 0
        if unit == 'inch':
            inchdistance += distance
        elif unit == 'cm':
            inchdistance += distance/2.54
        elif unit == 'mm':
            inchdistance += distance/25.4
        else:
            raise Exception("The specified unit is not available, please enter 'inch', 'cm', or 'mm'.")

        #Calculates hoe much the motor needs to move to get to the specified coordinate.
        moveamount = inchdistance - self.pos

        #Figures out the direction the motor needs to turn to get ot the desired coordinate.
        if moveamount < 0:
            movedirection = '-'
        else:
            movedirection = '+'

        #Moves the correct amount in the correct direction to get the motor to the specified coordinate.
        self.move(movedirection, numpy.absolute(moveamount))
