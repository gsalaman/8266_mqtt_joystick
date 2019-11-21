/******************************************************************************
MQTT joystick on 8266 thing.
******************************************************************************/
// WiFi and MQTT headers
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Header for persistent memory
#include <EEPROM.h>

// I2C header and info
#include <Wire.h>
#define JOYSTICK_ADDR 0x20

// LED pin definitions
#define LED_PIN_POWER   5
#define LED_PIN_WIFI    0
#define LED_PIN_BROKER  4
#define LED_PIN_ACTIVE  13

#define PLAYER_MAX_LEN 10
char player[PLAYER_MAX_LEN];
volatile bool registration_complete = false;

WiFiClient wclient;
PubSubClient client(wclient); // Setup MQTT client

/*  Storing all relevent configuration data in non-volatile memory (via EEPROM).  
 *  Creating a structure to hold all that data.
 */
#define SSID_SIZE      40
#define PASSWORD_SIZE  20
#define CLIENT_ID_SIZE 21   // 20 chars plus the null
typedef struct 
{
  char ssid[SSID_SIZE];
  char password[PASSWORD_SIZE];
  
  // PubSubClient needs broker address as 4 integers seperated by commas 
  // (example:  10,0,0,7 instead of 10.0.0.7)
  //  Thus, I'm gonna store them as 4 bytes.  
  unsigned char broker_addr[4];

  // I'm going to limit client ID to 20 characters
  char client_id[CLIENT_ID_SIZE];
} nv_data_type;

nv_data_type nv_data;

/* state of "how far" we're connected */
typedef enum
{
  
  STATE_OFFLINE = 0,             // Not looking for network...inputting parameters.
  STATE_DISCONNECT,              // Disconnected, but looking for WiFi
  STATE_LOOKING_FOR_BROKER,      // Connected to WiFi and looking for broker
  STATE_REGISTERING_WITH_GAME,   // Broker connection active, trying to get a player ID
  STATE_ACTIVE,                  // Connected to broker and exchanging MQTT messages for joystick position
  STATE_JOYSTICK_TEST            // Debug state to check out the joystick I2C reads.
} state_type;

// Forward function definitions for our state machine
void init_offline_state();
state_type process_offline_state();
void init_disconnect_state();
state_type process_disconnect_state();
void init_looking_for_broker();
state_type process_looking_for_broker();
void init_registering_with_game();
state_type process_registering_with_game();
void init_active();
state_type process_active();
void init_joystick_test();
state_type joystick_test();

/*=============================================================================
 * JOYSTICK DRIVER FUNCTIONALITY
 ==============================================================================*/

// We're only reading the 8 MSBs from the joystick, so the value can range from 
// zero to 255.
#define MAX_JOYSTICK_VALUE 255

// Generic enum for joystick position...we'll use this to map the 0-255 from
// the joystick to a "low, mid, or high" reading...and then we'll map low and high
// to directions on one axis.  Note we're not assuming orientation at this point.
typedef enum 
{
  JOYSTICK_LOW = 0,
  JOYSTICK_MID,
  JOYSTICK_HIGH
} joystick_position_type;

// Describe how the joystick is oriented in the case
typedef enum
{
  JS_ORIENT_VERT = 0,  // vertical orientation.  
  JS_ORIENT_RH         // right-handed, horizontal orientation.
} js_orient_type;

// hardcoding this for now...eventually may want it as part of NV.
js_orient_type joystick_orientation=JS_ORIENT_VERT;

// JOYSTICK_BUFFER is used to determine whether joystick is in a "mid" position, or 
// whether it's "high" or "low".  Example:
//   If the joystick axis reading is 220 and the buffer is 50, we'll map that to "high", 
//   as 220 is within 50 of our max value (255). 
#define JOYSTICK_BUFFER 20

/*=============================================================
 * JOYSTIC_READ
 * 
 * This function reads an x and y position from the joystick
 * via I2C.  The positions are returned as reference parameters, whilst
 * the function itself returns an indicator on whether the I2C read was successful.
 */
bool joystick_read(int *horiz, int *vert)
{
  Wire.beginTransmission(JOYSTICK_ADDR);
  Wire.write(0x03);
  Wire.endTransmission();

  Wire.requestFrom(JOYSTICK_ADDR,4);
  if (Wire.available()==4)
  {
    // only using the msb for horizontal and vertical
    *horiz = Wire.read();
    Wire.read();
    *vert = Wire.read();
    Wire.read();
    return true;
  }
  else
  {
    Serial.println("wire read failed.  I2C issue.");
    return false;
  }
}

/*===============================================================
 * MAP_JOYSTICK
 * 
 * This function maps a raw joystick read to either LOW, MID, or HIGH.
 * Note this function is axis and direction agnostic.
 */
joystick_position_type map_joystick(int value)
{
  if (value < JOYSTICK_BUFFER) return JOYSTICK_LOW;
  if (value < (MAX_JOYSTICK_VALUE - JOYSTICK_BUFFER)) return JOYSTICK_MID;
  return JOYSTICK_HIGH;
}

/*==============================================================
 * MQTT UTILITIES
 ==============================================================*/
/*==============================================================
 * MQTT Callback function
 * 
 * This function is used in the "registering with game" state to 
 * process respnoses.  
 *  
 * See the readme at https://github.com/gsalaman/mqtt_gamepad
 * for more info on the message flow between the game and the gamepad.
 */
void mqtt_callback(char* topic, byte* payload, unsigned int length) 
{

  if (length > PLAYER_MAX_LEN) 
  {
    Serial.println("player length too long!!!");
    return;
  }
  
  for (int i = 0; i < length; i++) {
    player[i] = (char)payload[i];
  }
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(player);

  registration_complete = true;

  Serial.println("Registration Complete");
}

/*=============================================================
 * SERIAL HELPER FUNCITONS
 */
 
/*===============================================
 * serial_read_number
 * 
 * Reads a number from the serial port.  Returns -1 if invalid.
 * Blocks.  Only allows 3 digit positive numbers.
 */
int serial_read_number( void )
{
  char c;
  int number=-1;
  
  // keep going until either we get a number (followed by \n), or an invalid character.
  while (true)
  {
    if (Serial.available())
    {
      c = Serial.read();

      if (c == '\n') return number;
      if (c < '0') return -1;
      if (c > '9') return -1;

      // If we get to here, that means the c char is a digit.  Add it in to our number.
      if (number =- -1) number = 0;

      number = number * 10;
      number = number + (c - '0');
    }
  }
  
}
/*===============================================
 * serial_read_string
 * 
 * Reads a string from the serial port.
 */
void serial_read_string( char *str, int max_chars )
{
  char c;
  int char_cnt=0;

  //Serial.println("serial_read_string");
  
  // keep going until either we get a number (followed by \n), or an invalid character.
  while (true)
  {
    if (Serial.available())
    {
      c = Serial.read();

      //Serial.print("char: ");
      //Serial.println(c);

      // if we've got a \n, then we want to properly terminate the string and return.
      if (c == '\n')
      {
        *str = NULL;
        return;
      }
      // otherwise, keep building the string.
      else
      {
        *str = c;
        str++;
        char_cnt++;
        
        if (char_cnt == (max_chars -1))
        {
          *str = NULL;
          return;
        }
      }
    } 
  }
}

/*===============================================
 * print_broker_addr
 * 
 * Prints a line with our broker address out the serial port.
 */
void print_broker_addr( void )
{
  int i;

  for (i=0;i<4;i++)
  {
    Serial.print(nv_data.broker_addr[i]);
    Serial.print(".");
  }
  Serial.println();
}

/*===================================================================
 * OFFLINE STATE FUNCTIONS 
 * =================================================================*/

/*======================================
 * configure_ssid
 * 
 * This function reads a string from the serial port and uses it to 
 * set our SSID.
 */
void configure_ssid( void )
{ 
  Serial.println("Enter SSID");
  
  serial_read_string(nv_data.ssid, SSID_SIZE);

  Serial.print("Set SSID to ");
  Serial.println(nv_data.ssid);
  
  EEPROM.put(0,nv_data);
  EEPROM.commit();

}

/*======================================
 * configure_password
 * 
 * This function reads a string from the serial port and uses it to 
 * set our WiFi password.
 */
void configure_pasword( void )
{ 
  Serial.println("Enter password");
  
  serial_read_string(nv_data.password, PASSWORD_SIZE);

  Serial.print("Set password");
  
  EEPROM.put(0,nv_data);
  EEPROM.commit();

}

/*======================================
 * configure_broker
 * 
 * This function reads a string from the serial port and uses it to 
 * set our broker address.  Note that this string needs to be in IPV4 format.
 */
#define BROKER_STR_SIZE 16
void configure_broker( void )
{
  char broker_string[BROKER_STR_SIZE];
  int  addr_int;
  int  addr_index;
  char *str_ptr=broker_string;
  
  Serial.println("Enter broker address (format:  127.0.0.1)");

  //start by reading in the string from the serial port.
  serial_read_string(broker_string, BROKER_STR_SIZE);

  // Now that we've got the string, we need to parse it.  
  // We're expecting IPV4 format (eg "127.0.0.1")
  // We'll use the addr_int variable to accumulate each address byte, and
  // addr_index to tell which byte we're looking at.
  //    Example above:  127 is the 0th addr_index, 1 is the 3rd.
  addr_int = -1;
  addr_index = 0;
  while (true)
  {

    // If we've got a dot, go to the next address index.
    if (*str_ptr == '.')
    {
      // if we haven't gotten a valid number, print an error and exit.
      if ( (addr_int < 0) || (addr_int > 255) )
      {
        Serial.println("Invalid byte in address");
        return;
      }
      else
      {
        //Serial.println("Found a dot...going to next address");
        
        nv_data.broker_addr[addr_index] = addr_int;
        str_ptr++;
        addr_int = -1;

        // make sure we don't have too many dots...
        if (addr_index >= 3)
        {
           Serial.println("Invalid address...more than 4 entries");
           return; 
        }
        else
        {
          addr_index++;
        }
      }
    }
    // if this is the end of our string, make sure we're on the fourth byte, and confirm
    // that it's in range.
    else if (*str_ptr == NULL)
    {
      if ((addr_index == 3) && (addr_int >= 0) && (addr_int <=255))
      {
        //Serial.println("Found End of String.");
        nv_data.broker_addr[addr_index] = addr_int;
        break;
      }
      else
      {
        Serial.println("invalid address");
        return;
      }
    }
    // If we've got a digit, build our current address.
    else if ( (*str_ptr >= '0') && (*str_ptr <= '9') )
    {
      if (addr_int == -1) addr_int = 0;
      addr_int = addr_int * 10;
      addr_int = addr_int + (*str_ptr - '0');      

      // Serial.print("Saw a ");
      // Serial.print(*str_ptr);
      // Serial.print("...number = ");
      // Serial.println(addr_int);
      
      str_ptr++;
    }
    
    // If we got here, this is an invalid character.
    else
    {
      Serial.print("Invalid char in parsing broker address: ");
      Serial.println(*str_ptr);
      return;
    }
  }

  // at this point, we should have built a valid address.  Commit it to NV.
  Serial.print("Broker Address = ");
  Serial.print(nv_data.broker_addr[0]);
  Serial.print(".");
  Serial.print(nv_data.broker_addr[1]);
  Serial.print(".");
  Serial.print(nv_data.broker_addr[2]);
  Serial.print(".");
  Serial.println(nv_data.broker_addr[3]);

  
  EEPROM.put(0,nv_data);
  EEPROM.commit();
  
}

/*======================================
 * configure_client_id
 * 
 * This function reads a string from the serial port and uses it to 
 * set our MQTT client id.
 */
void configure_client_id( void )
{ 
  Serial.println("Enter client ID");
  Serial.println("  (no spaces, 20 chars max)");

  serial_read_string(nv_data.client_id, CLIENT_ID_SIZE);

  Serial.print("Set client to ");
  Serial.println(nv_data.client_id);
  
  
  EEPROM.put(0,nv_data);
  EEPROM.commit();

}

/*=====================================
 * check_for_offline_transitions
 * 
 * In all states other than offline, we use the serial port 
 * as a mechniasm to transition to offline mode.  This function
 * does that check, and if so, it will initialize offline state and
 * return TRUE.  
 */
bool check_for_offline_transitions( void )
{
  // any serial input will kick us to offline.
  if (Serial.available())
  { 
    Serial.println("Going to offline state...");

    // Note that initing offline state will clear the serial buffer 
    // so that we get a fresh start.
    init_offline_state();
    
    return true;
  }
  else
  {
    return false;
  }
}

/*=====================================
 * print_offline_menu
 */
void print_offline_menu( void )
{
  Serial.println("Configuration:");
  Serial.println("1....set WiFi SSID");
  Serial.println("2....set WiFi password");
  Serial.println("3....set MQTT Broker");
  Serial.println("4....set MQTT Client Name");
  Serial.println("5....exit offline mode");
  Serial.println("6....run joystick test");
}

/*=============================================================
 * INIT_OFFLINE_STATE
 */
void init_offline_state( void )
{
  bool buffer_flushed = false;
  char c;

  // Leave the power LED on, but turn all the others off.
  digitalWrite(LED_PIN_WIFI, LOW);
  digitalWrite(LED_PIN_BROKER, LOW);
  digitalWrite(LED_PIN_ACTIVE, LOW);
  
  // To get to offline state, the user needed to type something in the 
  // serial window...which means we have a buffer of chars up to a '/n'.  Flush those.
  while (!buffer_flushed)
  {
    if (Serial.available())
    {
      c = Serial.read();
      if (c == '\n')
      {
        buffer_flushed = true;
      }
    }
  }  
}

/*==================================================
 * PROCESS_OFFLINE_STATE
 * 
 * This is the state handler function for the offline state.
 * In offline state, we allow the user to program (via the serial port)
 * our WiFi and MQTT configuration data. 
 */
state_type process_offline_state( void )
{
  int input;

  print_offline_menu();

  input = serial_read_number();

  //Serial.print("Read a ");
  //Serial.println(input);

    switch (input)
    {
      case 1:
        configure_ssid();
      break;

      case 2:
        configure_pasword();
      break;
      
      case 3:
        configure_broker();
      break;

      case 4:
        configure_client_id();
      break;

      case 5:
        init_disconnect_state();
        return STATE_DISCONNECT;

      case 6:
        return STATE_JOYSTICK_TEST;
        
      default:
        Serial.println("Unknown command");
        print_offline_menu();
    }

  return STATE_OFFLINE;
  
}

/*=================================================================
 * DISCONNECT STATE FUNCTIONS
 =================================================================*/

/*=========================================
 * init_disconnect_state
 * 
 * Initializes our disconnected state by starting to look for WiFi
 */
void init_disconnect_state( void )
{
  Serial.print("\nConnecting to network");
  WiFi.begin(nv_data.ssid, nv_data.password); // Connect to network
}

/*=========================================
 * process_disconnect_state
 * 
 * Processes our disconnect state by continously checking our WiFi status.
 * We'll stay in disconnect if we're not connected..otherwise we'll start
 * looking for our broker.
 */
state_type process_disconnect_state( void )
{
  static int wifi_led=LOW;

  // first, check to see if we need to go to offline state
  if (check_for_offline_transitions()) return STATE_OFFLINE;
  
  if (WiFi.status() != WL_CONNECTED) 
  { 
    // Wait for connection
    delay(500);
    Serial.print(".");

    // Flash the WiFi LED whilst were trying to connect.
    wifi_led = !wifi_led;
    digitalWrite(LED_PIN_WIFI, wifi_led);
      
    return STATE_DISCONNECT;
  }
  else
  {
    digitalWrite(LED_PIN_WIFI, HIGH);
    init_looking_for_broker();
    return STATE_LOOKING_FOR_BROKER;
  }
}

/*=================================================================
 * LOOKING_FOR_BROKER STATE FUNCTIONS
 =================================================================*/
/*=====================================
 * init_looking_for_broker
 * 
 * This function assumes we're connected to wifi, and sets up a client for MQTT connection.
 */
void init_looking_for_broker( void )
{ 
  Serial.print("Looking for broker: ");
  print_broker_addr();
  
  IPAddress broker(nv_data.broker_addr[0],nv_data.broker_addr[1],nv_data.broker_addr[2],nv_data.broker_addr[3]); 
  
  client.setServer(broker, 1883);
  client.setCallback(mqtt_callback);
}

/*=====================================
 * process_looking_for_broker
 * 
 * This function attempts to connect to the MQTT broker.
 */
state_type process_looking_for_broker( void )
{
  
  // first, check to see if we need to go to offline state
  if (check_for_offline_transitions()) return STATE_OFFLINE;
  
  if (!client.connected())
  {
     Serial.print("Attempting MQTT connection...");

    // I'm using the client.connect function with last will parameters
    // to send "register/release" with client_id as the payload, as per our wrapper driver protocol.
    if (client.connect(nv_data.client_id,  // The client ID for the broker
                       "register/release", // last will message topic
                       0,                  // last will QOS
                       false,              // last will retain flag
                       nv_data.client_id))  // last will message itself
    {
      Serial.println("connected");

      digitalWrite(LED_PIN_BROKER, HIGH);
      
      init_registering_with_game();

      return STATE_REGISTERING_WITH_GAME;
    } 
    else 
    {
      //   eventually we'll want to flash the LED whilst looking, but for now we'll
      //   just leave it off.
    
      // Wait 2 seconds before retrying
      Serial.println(" try again in 2 seconds");
      delay(2000);
      
      return STATE_LOOKING_FOR_BROKER;
    }
  }
}


/*=======================================================================
 * REGISTERING-WITH-GAME STATE FUNCTIONS
 * 
 *   Now that we've got a connection to the broker, attempt to connect 
 *   to the game itself, via the the registration mechanism found here:
 *   https://github.com/gsalaman/mqtt_gamepad
 *======================================================================*/

/*===========================================
 * send_registration_request
 * 
 * Helper function to publish the request.
 */
void send_registration_request( void )
{ 
    client.publish("register/request", nv_data.client_id);
  
    Serial.print("Registering client ");
    Serial.println(nv_data.client_id);
  
}

/*============================================
 * init_registering_with_game
 * 
 * Start the registration process by publishing our client ID.
 * The response will be processed async by the mqtt callback.
 */
void init_registering_with_game( void )
{
    char subscribe_str[30]="register/";
        
    strcat(subscribe_str, nv_data.client_id);
    Serial.print("Subscribing to ");
    Serial.println(subscribe_str);  
    client.subscribe(subscribe_str);

}

/*============================================
 * process_registering_with_game
 * 
 * This function looks for the registration_complete indicator
 * (set by the MQTT callback)
 */
#define REGISTRATION_RESEND_MS 5000 
state_type process_registering_with_game( void )
{
  static unsigned long last_sent_time=0;
  unsigned long curr_time;

  // first, check to see if we need to go to offline state
  if (check_for_offline_transitions()) return STATE_OFFLINE;

  // Need to call client.loop() to allow the callback to do it's thing.
  client.loop();
  
  // Is it time to resend a registration?
  curr_time = millis();
  if (curr_time > last_sent_time + REGISTRATION_RESEND_MS)
  {
    send_registration_request();
    last_sent_time = curr_time;
  }
  
  if (registration_complete) 
  {
    Serial.println("Registration Complete!!!");
    
    init_active();
    
    return STATE_ACTIVE;
  }
  else
  {
    // Is it time to resend a registration?
    curr_time = millis();
    if (curr_time > last_sent_time + REGISTRATION_RESEND_MS)
    {
      send_registration_request();
      last_sent_time = curr_time;
    }
    
    return STATE_REGISTERING_WITH_GAME;
  }
}

/*=======================================================================
 * ACTIVE STATE FUNCTIONS
 *======================================================================*/
void init_active ( void )
{
  digitalWrite(LED_PIN_ACTIVE, HIGH);  
}

/*=======================================================================
 * process_active
 * 
 * This is the main processing function for active state.  In here, we
 * look for direction changes on our joystick and publish them through MQTT
 */
state_type process_active( void )
{
  int x;
  int y;
  static joystick_position_type last_vert=JOYSTICK_MID;
  static joystick_position_type last_horiz=JOYSTICK_MID;
  joystick_position_type curr_vert;
  joystick_position_type curr_horiz;


  // first, check to see if we need to go to offline state
  if (check_for_offline_transitions()) return STATE_OFFLINE;
   
  joystick_read(&x,&y);

  if (joystick_orientation == JS_ORIENT_RH)
  {
    //for right-handed orientation, the joystick y axis runs horizontally, with 0 on the right.
    //  The x axis runs vertially, with 0 on top.
    curr_horiz = map_joystick(y);
    curr_vert = map_joystick(x);
  
    if (last_horiz == JOYSTICK_MID)
    {
      if (curr_horiz == JOYSTICK_LOW)
      {
        client.publish(player, "right");
        Serial.println("RIGHT");
      }
      else if (curr_horiz == JOYSTICK_HIGH)
      {
        client.publish(player, "left");
        Serial.println("LEFT");
      }
    }
    last_horiz = curr_horiz;

    if (last_vert == JOYSTICK_MID)
    {
      if (curr_vert == JOYSTICK_LOW)
      {
        client.publish(player, "up");
        Serial.println("UP");
      }
      else if (curr_vert == JOYSTICK_HIGH)
      {
        client.publish(player, "down");
        Serial.println("DOWN");
      }
    }
    last_vert = curr_vert;
  } // end of right-hand orientation.

  else if (joystick_orientation == JS_ORIENT_VERT)
  {
    //for vertical orientation, the joystick x axis runs horizontally, with 0 on the left.
    //  The y axis runs vertially, with 0 on top.
    curr_horiz = map_joystick(x);
    curr_vert = map_joystick(y);
  
    if (last_horiz == JOYSTICK_MID)
    {
      if (curr_horiz == JOYSTICK_LOW)
      {
        client.publish(player, "left");
        Serial.println("LEFT");
      }
      else if (curr_horiz == JOYSTICK_HIGH)
      {
        client.publish(player, "right");
        Serial.println("RIGHT");
      }
    }
    last_horiz = curr_horiz;

    if (last_vert == JOYSTICK_MID)
    {
      if (curr_vert == JOYSTICK_LOW)
      {
        client.publish(player, "up");
        Serial.println("UP");
      }
      else if (curr_vert == JOYSTICK_HIGH)
      {
        client.publish(player, "down");
        Serial.println("DOWN");
      }
    }
    last_vert = curr_vert;
  } // end of vertical orientation.
  else
  {
    Serial.println("INVALID JOYSTICK ORIENTATION!!!");
  }

  // this is needed to send out the keepalives to detect we're still connected.
  client.loop();
  
  return STATE_ACTIVE;
}

/*==============================================================
 * JOYSTICK_TEST STATE
 * 
 * This state is used as a HW debug where
 * we periodically read the joystick and publish our x & y values.
 ================================================================*/
state_type joystick_test( void )
{
  int x=-1;
  int y=-1;
  static joystick_position_type last_vert=JOYSTICK_MID;
  static joystick_position_type last_horiz=JOYSTICK_MID;
  joystick_position_type curr_vert;
  joystick_position_type curr_horiz;
  
  // first, check to see if we need to go to offline state
  if (check_for_offline_transitions()) return STATE_OFFLINE;
  
  digitalWrite(LED_PIN_ACTIVE, HIGH);
   
  joystick_read(&x,&y);
  
  Serial.print("x: ");
  Serial.print(x);
  Serial.print(" y: ");
  Serial.println(y);
  
  delay(1000);
  
  return STATE_JOYSTICK_TEST;
}

/*==============================================================
 * LED TEST
 * 
 * This is a uick power-up LED test.
 *================================================================*/
void led_test(void)
{
  int led_delay_ms = 250; 
  Serial.println();
  Serial.println("Start LED test");
  
  digitalWrite(LED_PIN_POWER, LOW);
  digitalWrite(LED_PIN_WIFI, LOW);
  digitalWrite(LED_PIN_BROKER, LOW);
  digitalWrite(LED_PIN_ACTIVE, LOW);

  digitalWrite(LED_PIN_POWER, HIGH);
  delay(led_delay_ms);
  digitalWrite(LED_PIN_WIFI, HIGH);
  delay(led_delay_ms);
  digitalWrite(LED_PIN_BROKER, HIGH);
  delay(led_delay_ms);
  digitalWrite(LED_PIN_ACTIVE, HIGH);
  delay(led_delay_ms);

  // keeping power LED on intentionally
  digitalWrite(LED_PIN_WIFI, LOW);
  digitalWrite(LED_PIN_BROKER, LOW);
  digitalWrite(LED_PIN_ACTIVE, LOW);
  

}

/*==============================================================
 * SETUP
 ================================================================*/

void setup( void ) 
{
  pinMode(LED_PIN_POWER, OUTPUT);
  pinMode(LED_PIN_WIFI, OUTPUT);
  pinMode(LED_PIN_BROKER, OUTPUT);
  pinMode(LED_PIN_ACTIVE, OUTPUT);

  Serial.begin(9600); 

  // pin 2 data, pin 14 clock
  Wire.begin();
  Wire.setClockStretchLimit(2000);
  
  EEPROM.begin(sizeof(nv_data_type));
  EEPROM.get(0, nv_data);

  led_test();
  
  Serial.println("\nInitializing 8266 MQTT Joystick");
  Serial.print("Client id: ");
  Serial.println(nv_data.client_id);
  Serial.print("Broker addr: ");
  print_broker_addr();  
  Serial.println("Init complete");

}

/*==============================================================
 * LOOP
 ================================================================*/
void loop()
{
  static state_type current_state=STATE_DISCONNECT;  

  switch (current_state)
  {
    case STATE_OFFLINE:
      current_state = process_offline_state();
    break;
    
    case STATE_DISCONNECT:
      current_state = process_disconnect_state();
    break;

    case STATE_LOOKING_FOR_BROKER:
      current_state = process_looking_for_broker();
    break;

    case STATE_REGISTERING_WITH_GAME:
      current_state = process_registering_with_game();
    break;

    case STATE_ACTIVE:
      current_state = process_active();
    break;

    case STATE_JOYSTICK_TEST:
      current_state = joystick_test();
    break;

    default:
      Serial.println("Unexpected STATE!!!");
  }  // end of switch on current state

}
