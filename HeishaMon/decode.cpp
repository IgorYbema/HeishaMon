#include "decode.h"
#include "commands.h"

unsigned long nextalldatatime = 0;
unsigned long nextalloptdatatime = 0;

String getBit1and2(byte input) {
  return String((input  >> 6) - 1);
}

String getBit3and4(byte input) {
  return String(((input >> 4) & 0b11) - 1);
}

String getBit5and6(byte input) {
  return String(((input >> 2) & 0b11) - 1);
}

String getBit7and8(byte input) {
  return String((input & 0b11) - 1);
}

String getBit3and4and5(byte input) {
  return String(((input >> 3) & 0b111) - 1);
}


String getLeft5bits(byte input) {
  return String((input >> 3) - 1);
}

String getRight3bits(byte input) {
  return String((input & 0b111) - 1);
}

String getIntMinus1(byte input) {
  int value = (int)input - 1;
  return (String)value;
}

String getIntMinus128(byte input) {
  int value = (int)input - 128;
  return (String)value;
}

String getIntMinus1Div5(byte input) {
  return String((((float)input - 1) / 5), 1);

}
String getIntMinus1Times10(byte input) {
  int value = (int)input - 1;
  return (String)(value * 10);

}
String getIntMinus1Times50(byte input) {
  int value = (int)input - 1;
  return (String)(value * 50);
}

String unknown(byte input) {
  return "-1";
}

String getOpMode(byte input) {
  switch ((int)(input & 0b111111)) {
    case 18:
      return "0";
    case 19:
      return "1";
    case 25:
      return "2";
    case 33:
      return "3";
    case 34:
      return "4";
    case 35:
      return "5";
    case 41:
      return "6";
    case 26:
      return "7";
    case 42:
      return "8";
    default:
      return "-1";
  }
}

String getModel(char* data) { // TOP92 //
  byte model[10] = { data[129], data[130], data[131], data[132], data[133], data[134], data[135], data[136], data[137], data[138]};
  byte modelResult = -1;
  for (unsigned int i = 0 ; i < sizeof(knownModels)/sizeof(knownModels[0]) ; i++) {
    if (memcmp_P(model, knownModels[i], 10) == 0) {
      modelResult = i;
    }
  }
  return String(modelResult);
}

String getEnergy(byte input)
{
  int value = ((int)input - 1) * 200;
  return (String)value;
}

String getPumpFlow(char* data) {  // TOP1 //
  int PumpFlow1 = (int)data[170];
  float PumpFlow2 = (((float)data[169] - 1) / 256);
  float PumpFlow = PumpFlow1 + PumpFlow2;
  return String(PumpFlow, 2);
}

String getErrorInfo(char* data) { // TOP44 //
  int Error_type = (int)(data[113]);
  int Error_number = ((int)(data[114])) - 17;
  char Error_string[10];
  switch (Error_type) {
    case 177:                  //B1=F type error
      sprintf(Error_string, "F%02X", Error_number);
      break;
    case 161:                  //A1=H type error
      sprintf(Error_string, "H%02X", Error_number);
      break;
    default:
      sprintf(Error_string, "No error");
      break;
  }
  return String(Error_string);
}

void broadcast_heatpump_data(String actData[], PubSubClient &mqtt_client, void (*log_message)(char*), char* mqtt_topic_base) {
  char log_msg[256];
  char mqtt_topic[256];

  for (unsigned int Topic_Number = 0 ; Topic_Number < NUMBER_OF_TOPICS ; Topic_Number++) {
    sprintf(log_msg, "received TOP%d %s: %s", Topic_Number, topics[Topic_Number], actData[Topic_Number].c_str());
    log_message(log_msg);
    sprintf(mqtt_topic, "%s/%s/%s", mqtt_topic_base, mqtt_topic_values, topics[Topic_Number]);
    mqtt_client.publish(mqtt_topic, actData[Topic_Number].c_str(), MQTT_RETAIN_VALUES);
  }
}

// Decode ////////////////////////////////////////////////////////////////////////////
void decode_heatpump_data(char* data, String actData[], PubSubClient &mqtt_client, void (*log_message)(char*), char* mqtt_topic_base) {
  char log_msg[256];
  char mqtt_topic[256];

  for (unsigned int Topic_Number = 0 ; Topic_Number < NUMBER_OF_TOPICS ; Topic_Number++) {
    byte Input_Byte;
    String Topic_Value;
    switch (Topic_Number) { //switch on topic numbers, some have special needs
      case 1:
        Topic_Value = getPumpFlow(data);
        break;
      case 11:
        Topic_Value = String(word(data[183], data[182]) - 1);
        break;
      case 12:
        Topic_Value = String(word(data[180], data[179]) - 1);
        break;
      case 90:
        Topic_Value = String(word(data[186], data[185]) - 1);
        break;
      case 91:
        Topic_Value = String(word(data[189], data[188]) - 1);
        break;
      case 44:
        Topic_Value = getErrorInfo(data);
        break;
      case 92:
        Topic_Value = getModel(data);
        break;
      default:
        byte cpy;
        memcpy_P(&cpy, &topicBytes[Topic_Number], sizeof(byte));
        Input_Byte = data[cpy];
        Topic_Value = topicFunctions[Topic_Number](Input_Byte);
        break;
    }
    if (( actData[Topic_Number] != Topic_Value )) {
      actData[Topic_Number] = Topic_Value;
      sprintf_P(log_msg, PSTR("received TOP%d %s: %s"), Topic_Number, topics[Topic_Number], Topic_Value.c_str());
      log_message(log_msg);
      sprintf(mqtt_topic, "%s/%s/%s", mqtt_topic_base, mqtt_topic_values, topics[Topic_Number]);
      mqtt_client.publish(mqtt_topic, Topic_Value.c_str(), MQTT_RETAIN_VALUES);
    }
  }
}

void broadcast_optional_heatpump_data(String actData[], PubSubClient &mqtt_client, void (*log_message)(char*), char* mqtt_topic_base) {
  char log_msg[256];
  char mqtt_topic[256];

  for (unsigned int Topic_Number = 0 ; Topic_Number < NUMBER_OF_OPT_TOPICS ; Topic_Number++) {
    sprintf(log_msg, "received OPT%d %s: %s", Topic_Number, optTopics[Topic_Number], actData[Topic_Number].c_str());
    log_message(log_msg);
    sprintf(mqtt_topic, "%s/%s/%s", mqtt_topic_base, mqtt_topic_pcbvalues, optTopics[Topic_Number]);
    mqtt_client.publish(mqtt_topic, actData[Topic_Number].c_str(), MQTT_RETAIN_VALUES);
  }
}

void decode_optional_heatpump_data(char* data, String actOptData[], PubSubClient & mqtt_client, void (*log_message)(char*), char* mqtt_topic_base) {
  char log_msg[256];
  char mqtt_topic[256];

  for (unsigned int Topic_Number = 0 ; Topic_Number < NUMBER_OF_OPT_TOPICS ; Topic_Number++) {
    byte Input_Byte;
    String Topic_Value;
    switch (Topic_Number) { //switch on topic numbers, some have special needs
      case 0:
        Topic_Value = String(data[4] >> 7);
        break;
      case 1:
        Topic_Value = String((data[4] >> 5) & 0b11);
        break;
      case 2:
        Topic_Value = String((data[4] >> 4) & 0b1);
        break;
      case 3:
        Topic_Value = String((data[4] >> 2) & 0b11);
        break;
      case 4:
        Topic_Value = String((data[4] >> 1) & 0b1);
        break;
      case 5:
        Topic_Value = String((data[4] >> 0) & 0b1);
        break;
      case 6:
        Topic_Value = String((data[5] >> 0) & 0b1);
        break;
      default:
        break;
    }
    if (( actOptData[Topic_Number] != Topic_Value )) {
      actOptData[Topic_Number] = Topic_Value;
      sprintf_P(log_msg, PSTR("received OPT%d %s: %s"), Topic_Number, optTopics[Topic_Number], Topic_Value.c_str());
      log_message(log_msg);
      sprintf(mqtt_topic, "%s/%s/%s", mqtt_topic_base, mqtt_topic_pcbvalues, optTopics[Topic_Number]);
      mqtt_client.publish(mqtt_topic, Topic_Value.c_str(), MQTT_RETAIN_VALUES);
    }
  }
  //response to heatpump should contain the data from heatpump on byte 4 and 5
  byte valueByte4 = data[4];
  optionalPCBQuery[4] = valueByte4;
  byte valueByte5 = data[5];
  optionalPCBQuery[5] = valueByte5;
}
