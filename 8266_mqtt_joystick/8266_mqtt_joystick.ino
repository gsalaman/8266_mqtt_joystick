/******************************************************************************
MQTT joystick on 8266 thing.
******************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <EEPROM.h>

const byte SWITCH_PIN = 0;           // Pin to control the light with
const char *ID = "Example_Switch";  // Name of our device, must be unique
const char *TOPIC = "room/light";  // Topic to subcribe to

IPAddress broker(10,0,0,17); // IP address of your MQTT broker eg. 192.168.1.50
WiFiClient wclient;

PubSubClient client(wclient); // Setup MQTT client

typedef struct 
{
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

#define BUF_SIZE 40
void configure_wifi( void )
{
  char ssid[BUF_SIZE];
  char pass[BUF_SIZE];
  char *curr_char = ssid;

  // disconnect from any currently assigned networks.
  WiFi.disconnect();
  
  Serial.println("Enter SSID");

  // Build up the SSID string until we hit an Enter.
  while (true)
  {
    if (Serial.available())
    {
      // Grab the next character
      *curr_char = Serial.read();
      if (*curr_char == '\n')
      {
        *curr_char = NULL;
        break;
      }
      curr_char++;
      // Need some buffer overflow checking here...
    }
  }
  Serial.print("SSID=");
  Serial.println(ssid);

  Serial.println("Enter Password");
  curr_char = pass;
  
  // Build up the password string until we hit an Enter.
  while (true)
  {
    if (Serial.available())
    {
      // Grab the next character
      *curr_char = Serial.read();
      if (*curr_char == '\n')
      {
        *curr_char = NULL;
        break;
      }
      curr_char++;
      // Need some buffer overflow checking here...
    }
  }
  Serial.print("Pass=");
  Serial.println(pass);

  Serial.print("\nConnecting to network");
  
  WiFi.begin(ssid, pass); // Connect to network

  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void init_disconnect_state( void )
{
  Serial.print("\nConnecting to network");
  WiFi.begin(); // Connect to network, using last stored credentials.
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
      return STATE_LOOKING_FOR_BROKER;
    }
}

void print_offline_menu( void )
{
  Serial.println("Configuration:");
  Serial.println("1....set WiFi SSID and password");
  Serial.println("2....set MQTT Broker");
  Serial.println("3....set MQTT Client Name");
  Serial.println("4....exit offline mode");
}

state_type process_offline_state( void )
{
  int input;

  print_offline_menu();

  input = serial_read_number();

  Serial.print("Read a ");
  Serial.println(input);

    switch (input)
    {
      case 1:
        configure_wifi();
      break;

      case 2:
        configure_broker();
      break;

      case 3:
        configure_client_id();
      break;

      case 4:
        init_disconnect_state();
        return STATE_DISCONNECT;
        
      default:
        Serial.println("Unknown command");
        print_offline_menu();
    }

  return STATE_OFFLINE;
  
}

// Connect to WiFi network using stored credentials.
void setup_wifi() 
{
  Serial.print("\nConnecting to network");
  
  //WiFi.begin(ssid, password); // Connect to network
  WiFi.begin(); // Connect to network, using last stored credentials.

  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Reconnect to client
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
      current_state = process_disconnect_state();
    break;

    case STATE_LOOKING_FOR_BROKER:
      Serial.println("You WIN!!!!");
      delay(5000);
    break;

    case STATE_REGISTERING_WITH_GAME:
    break;

    case STATE_ACTIVE:
    break;

    default:
      Serial.println("Unexpected STATE!!!");
  }  // end of switch on current state
    
}
