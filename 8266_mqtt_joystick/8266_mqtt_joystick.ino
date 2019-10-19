/******************************************************************************
MQTT joystick on 8266 thing.
******************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <EEPROM.h>

const byte SWITCH_PIN = 0;           // Pin to control the light with
const char *ID = "Example_Switch";  // Name of our device, must be unique
const char *TOPIC = "room/light";  // Topic to subcribe to

//IPAddress broker(10,0,0,17); // IP address of your MQTT broker eg. 192.168.1.50
WiFiClient wclient;

PubSubClient client(wclient); // Setup MQTT client

#define SSID_SIZE     40
#define PASSWORD_SIZE 20

typedef struct 
{
  char ssid[SSID_SIZE];
  char password[PASSWORD_SIZE];
  
  // PubSubClient needs broker address as 4 integers seperated by commas 
  // (example:  10,0,0,7 instead of 10.0.0.7)
  //  Thus, I'm gonna store them as 4 bytes.  
  unsigned char broker_addr[4];

  // I'm going to limit client ID to 20 characters
  char client_id[21];
} nv_data_type;

nv_data_type nv_data;

typedef enum
{
  STATE_OFFLINE = 0,
  STATE_DISCONNECT,
  STATE_LOOKING_FOR_BROKER,
  STATE_REGISTERING_WITH_GAME,
  STATE_ACTIVE
} state_type;

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

  //Serial.println("serial_read_number");
  
  // keep going until either we get a number (followed by \n), or an invalid character.
  while (true)
  {
    if (Serial.available())
    {
      c = Serial.read();

      //Serial.print("char: ");
      //Serial.println(c);

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

void configure_client_id( void )
{ 
  Serial.println("Enter client ID");
  Serial.println("  (no spaces, 20 chars max)");

  serial_read_string(nv_data.client_id, 21);

  Serial.print("Set client to ");
  Serial.println(nv_data.client_id);
  
  
  EEPROM.put(0,nv_data);
  EEPROM.commit();

}


void configure_ssid( void )
{ 
  Serial.println("Enter SSID");
  
  serial_read_string(nv_data.ssid, SSID_SIZE);

  Serial.print("Set SSID to ");
  Serial.println(nv_data.ssid);
  
  EEPROM.put(0,nv_data);
  EEPROM.commit();

}


void configure_pasword( void )
{ 
  Serial.println("Enter password");
  
  serial_read_string(nv_data.password, PASSWORD_SIZE);

  Serial.print("Set password");
  
  EEPROM.put(0,nv_data);
  EEPROM.commit();

}

#define BUF_SIZE 40
#define TIMEOUT_TICK_COUNT 120
void configure_wifi( void )
{
  char ssid[BUF_SIZE];
  char pass[BUF_SIZE];
  char *curr_char = ssid;
  int timeout_ticks;

  // So this is kinda funky.  I'm cheating by connecting to the network inside of "configure_wifi" and using the 
  // intrinsic 8266 functionality to store ssid and password.
  // I'm gonnna disconnect at the end so that we always enter "disconnect" the same way.
  
  // disconnect from any currently assigned networks.
  //WiFi.disconnect();
  
  Serial.println("Enter SSID");

  serial_read_string(ssid, BUF_SIZE);
  Serial.print("SSID=");
  Serial.println(ssid);

  Serial.println("Enter Password");

  serial_read_string(pass, BUF_SIZE);
  
  Serial.print("Pass=");
  Serial.println(pass);

  Serial.print("\nLooking for network");
  
  WiFi.begin(ssid, pass); // Connect to network

  // Keep looking until we either connect or timeout.
  timeout_ticks = 0;
  while ((WiFi.status() != WL_CONNECTED) && (timeout_ticks < TIMEOUT_TICK_COUNT)) 
  { // Wait for connection
    delay(1000);
    Serial.print(".");
    timeout_ticks++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
     Serial.println("WiFi found");   
  }
  else
  {
    Serial.println("Couldn't connect to network");
  }

  // I'm putting this disconnect here...in the case we're connected, we'll disconnect (since we set this in offline)
  // In the case we weren't able to connect, I think this stops the "WiFi.begin()" so we stop looking.
  WiFi.disconnect();
  
}

void init_disconnect_state( void )
{
  Serial.print("\nConnecting to network");
  WiFi.begin(nv_data.ssid, nv_data.password); // Connect to network
}

state_type process_disconnect_state( void )
{
    if (WiFi.status() != WL_CONNECTED) 
    { 
      // Wait for connection
      delay(500);
      Serial.print(".");
      return STATE_DISCONNECT;
    }
    else
    {
      init_looking_for_broker();
      return STATE_LOOKING_FOR_BROKER;
    }
}

void print_offline_menu( void )
{
  Serial.println("Configuration:");
  Serial.println("1....set WiFi SSID");
  Serial.println("2....set WiFi passwork");
  Serial.println("3....set MQTT Broker");
  Serial.println("4....set MQTT Client Name");
  Serial.println("5....exit offline mode");
}

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
        
      default:
        Serial.println("Unknown command");
        print_offline_menu();
    }

  return STATE_OFFLINE;
  
}

void init_looking_for_broker( void )
{ 
  Serial.print("Looking for broker: ");
  print_broker_addr();
  
  IPAddress broker(nv_data.broker_addr[0],nv_data.broker_addr[1],nv_data.broker_addr[2],nv_data.broker_addr[3]); 
  
  client.setServer(broker, 1883);
}

state_type process_looking_for_broker( void )
{
  if (!client.connected())
  {
     Serial.print("Attempting MQTT connection...");

    if (client.connect(nv_data.client_id)) 
    {
      Serial.println("connected");
      return STATE_REGISTERING_WITH_GAME;
    } 
    else 
    {
      Serial.println(" try again in 2 seconds");
      // Wait 2 seconds before retrying
      delay(2000);
      return STATE_LOOKING_FOR_BROKER;
    }
  }
}

// Reconnect to client...UNUSED
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(ID)) {
      Serial.println("connected");
      Serial.print("Publishing to: ");
      Serial.println(TOPIC);
      Serial.println('\n');

    } else {
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() 
{
  Serial.begin(9600); 

  EEPROM.begin(sizeof(nv_data_type));
  EEPROM.get(0, nv_data);

  Serial.println();
  Serial.println("Initializing 8266 MQTT Joystick");
  Serial.print("Client id: ");
  Serial.println(nv_data.client_id);
  Serial.print("Broker addr: ");
  print_broker_addr();
 
  Serial.println("Init complete");

  //delay(100);
  //init_disconnect_state();

  /*
  configure_wifi();
  pinMode(SWITCH_PIN,INPUT);  // Configure SWITCH_Pin as an input
  digitalWrite(SWITCH_PIN,HIGH);  // enable pull-up resistor (active low)
  delay(100);
  //setup_wifi(); // Connect to network
  client.setServer(broker, 1883);
  */
}

void loop()
{
  static state_type current_state=STATE_OFFLINE;  

  switch (current_state)
  {
    case STATE_OFFLINE:
      current_state = process_offline_state();
    break;
    
    case STATE_DISCONNECT:
      // any serial input will kick us to offline.
      if (Serial.available())
      {
        Serial.println("Going to offline state...");
        current_state = STATE_OFFLINE;
      }
      else
      {
        current_state = process_disconnect_state();
      }
    break;

    case STATE_LOOKING_FOR_BROKER:
      if (Serial.available())
      {
        Serial.println("Going to offline state....");
        current_state = STATE_OFFLINE;
      }
      else
      {
        current_state = process_looking_for_broker();
      }
    break;

    case STATE_REGISTERING_WITH_GAME:
      Serial.println("You WIN!!!");
      delay(5000);
    break;

    case STATE_ACTIVE:
    break;

    default:
      Serial.println("Unexpected STATE!!!");
  }  // end of switch on current state
    
}
