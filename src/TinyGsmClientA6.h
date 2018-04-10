/**
 * @file       TinyGsmClientA6.h
 * @author     Volodymyr Shymanskyy
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2016 Volodymyr Shymanskyy
 * @date       Nov 2016
 */

#ifndef TinyGsmClientA6_h
#define TinyGsmClientA6_h

//#define TINY_GSM_DEBUG Serial

#if !defined(TINY_GSM_RX_BUFFER)
  #define TINY_GSM_RX_BUFFER 256
#endif

#define TINY_GSM_MUX_COUNT 8

#include <TinyGsmCommon.h>

#define GSM_NL "\r\n"
static const char GSM_OK[] TINY_GSM_PROGMEM = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;

enum SimStatus {
  SIM_ERROR = 0,
  SIM_READY = 1,
  SIM_LOCKED = 2,
};

enum RegStatus {
  REG_UNREGISTERED = 0,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_OK_HOME      = 1,
  REG_OK_ROAMING   = 5,
  REG_UNKNOWN      = 4,
};


class TinyGsm
{

public:

class GsmClient : public Client
{
  friend class TinyGsm;
  typedef TinyGsmFifo<uint8_t, TINY_GSM_RX_BUFFER> RxFifo;

public:
  GsmClient() {}

  GsmClient(TinyGsm& modem) {
    init(&modem);
  }

  bool init(TinyGsm* modem) {
    this->at = modem;
    this->mux = -1;
    sock_connected = false;

    return true;
  }

public:
  virtual int connect(const char *host, uint16_t port) {
    TINY_GSM_YIELD();
    rx.clear();
    uint8_t newMux = -1;
    sock_connected = at->modemConnect(host, port, &newMux);
    if (sock_connected) {
      mux = newMux;
      at->sockets[mux] = this;
    }
    return sock_connected;
  }

  virtual int connect(IPAddress ip, uint16_t port) {
    String host; host.reserve(16);
    host += ip[0];
    host += ".";
    host += ip[1];
    host += ".";
    host += ip[2];
    host += ".";
    host += ip[3];
    return connect(host.c_str(), port);
  }

  virtual void stop() {
    TINY_GSM_YIELD();
    at->sendAT(GF("+CIPCLOSE="), mux);
    sock_connected = false;
    at->waitResponse();
  }

  virtual size_t write(const uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    //at->maintain();
    return at->modemSend(buf, size, mux);
  }

  virtual size_t write(uint8_t c) {
    return write(&c, 1);
  }

  virtual size_t print(const String &s) {
    return write((const uint8_t *)s.c_str(), s.length());
  }

  virtual size_t print(const char str[]) {
    return write((const uint8_t *)str,strlen(str));
  }

  virtual int available() {
    TINY_GSM_YIELD();
    if (!rx.size() && sock_connected) {
      at->maintain();
    }
    return rx.size();
  }

  virtual int read(uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    size_t cnt = 0;
    while (cnt < size) {
      size_t chunk = TinyGsmMin(size-cnt, rx.size());
      if (chunk > 0) {
        rx.get(buf, chunk);
        buf += chunk;
        cnt += chunk;
        continue;
      }
      // TODO: Read directly into user buffer?
      if (!rx.size() && sock_connected) {
        at->maintain();
        //break;
      }
    }
    return cnt;
  }

  virtual int read() {
    uint8_t c;
    if (read(&c, 1) == 1) {
      return c;
    }
    return -1;
  }

  virtual int peek() { return -1; } //TODO
  virtual void flush() { at->stream.flush(); }

  virtual uint8_t connected() {
    if (available()) {
      return true;
    }
    return sock_connected;
  }
  virtual operator bool() { return connected(); }

  /*
   * Extended API
   */

  String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;

private:
  TinyGsm*      at;
  uint8_t       mux;
  bool          sock_connected;
  RxFifo        rx;
};

public:

  TinyGsm(Stream& stream)
    : stream(stream)
  {
    memset(sockets, 0, sizeof(sockets));
  }

  /*
   * Basic functions
   */
  bool begin() {
    return init();
  }

  bool init() {
    if (!testAT()) {
      return false;
    }
    sendAT(GF("&FZE0"));  // Factory + Reset + Echo Off
    if (waitResponse() != 1) {
      return false;
    }
    sendAT(GF("+CMEE=0"));
    waitResponse();

    sendAT(GF("+CMER=3,0,0,2"));
    waitResponse();
  }

  bool setSmsStorage() {
    // PREFERRED SMS STORAGE
    sendAT(GF("+CPMS=\"SM\",\"SM\",\"SM\""));
    return waitResponse() == 1;
  }

  void setBaud(unsigned long baud) {
    sendAT(GF("+IPR="), baud);
  }

  bool testAT(unsigned long timeout = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      sendAT(GF(""));
      if (waitResponse(200) == 1) {
          delay(100);
          return true;
      }
      delay(100);
    }
    return false;
  }

  void maintain() {
    //while (stream.available()) {
      waitResponse(10, NULL, NULL);
    //}
  }

  bool factoryDefault() {
    sendAT(GF("&FZE0&W"));  // Factory + Reset + Echo Off + Write
    waitResponse();
    sendAT(GF("&W"));       // Write configuration
    return waitResponse() == 1;
  }

  String getModemInfo() {
    sendAT(GF("I"));
    String res;
    if (waitResponse(1000L, res) != 1) {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }

  String getFirmwareVersion() {
    sendAT(GF("+CGMR"));
    String res;
    if (waitResponse(1000L, res) != 1) {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }

  bool isActive() {
    sendAT(GF(""));
    if (waitResponse(200) == 1) {
      return true;
    }
    return false;
  }

  /*
   * Power functions
   */

  bool restart() {
    if (!testAT()) {
      return false;
    }
    sendAT(GF("+RST=1"));
    delay(3000);
    return init();
  }

  bool poweroff() {
    sendAT(GF("+CPOF"));
    return waitResponse() == 1;
  }

  bool radioOff() TINY_GSM_ATTR_NOT_IMPLEMENTED;

  bool sleepEnable(bool enable = true) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  /*
   * SIM card functions
   */

  bool simUnlock(const char *pin) {
    sendAT(GF("+CPIN=\""), pin, GF("\""));
    return waitResponse() == 1;
  }

  String getSimCCID() {
    sendAT(GF("+CCID"));
    if (waitResponse(GF(GSM_NL "+SCID: SIM Card ID:")) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  String getIMEI() {
    sendAT(GF("+GSN"));
    if (waitResponse(GF(GSM_NL)) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  SimStatus getSimStatus() {
      sendAT(GF("+CPIN?"));
      int status = waitResponse(GF("READY"), GF("SIM PIN"), GF("SIM PUK"));
      waitResponse();
      switch (status) {
      case 2:
      case 3:  return SIM_LOCKED;
      case 1:  return SIM_READY;
      default: return SIM_ERROR;
      }
    return SIM_ERROR;
  }

  RegStatus getRegistrationStatus() {
    sendAT(GF("+CREG?"));
    if (waitResponse(GF(GSM_NL "+CREG:")) != 1) {
      return REG_UNKNOWN;
    }
    streamSkipUntil(','); // Skip format (0)
    int status = stream.readStringUntil('\n').toInt();
    waitResponse();
    return (RegStatus)status;
  }

  String getOperator() {
    sendAT(GF("+COPS=3,0")); // Set format
    waitResponse();

    sendAT(GF("+COPS?"));
    if (waitResponse(GF(GSM_NL "+COPS:")) != 1) {
      return "";
    }
    streamSkipUntil('"'); // Skip mode and format
    String res = stream.readStringUntil('"');
    waitResponse();
    return res;
  }

  /*
   * Generic network functions
   */

  int getSignalQuality() {
    sendAT(GF("+CSQ"));
    if (waitResponse(GF(GSM_NL "+CSQ:")) != 1) {
      return 99;
    }
    int res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

  bool isNetworkConnected() {
    RegStatus s = getRegistrationStatus();
    return (s == REG_OK_HOME || s == REG_OK_ROAMING);
  }

  bool waitForNetwork(unsigned long timeout = 60000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      if (isNetworkConnected()) {
        return true;
      }
      delay(250);
    }
    return false;
  }

  /*
   * GPRS functions
   */
  bool gprsConnect(const char* apn, const char* user, const char* pwd) {
    gprsDisconnect();

    sendAT(GF("+CGATT=1"));
    if (waitResponse(20000L) != 1)
      return false;

    // TODO: wait AT+CGATT?

    sendAT(GF("+CGDCONT=1,\"IP\",\""), apn, '"');
    waitResponse();

    if (!user) user = "";
    if (!pwd)  pwd = "";
    sendAT(GF("+CSTT=\""), apn, GF("\",\""), user, GF("\",\""), pwd, GF("\""));
    if (waitResponse(20000L) != 1) {
      return false;
    }

    sendAT(GF("+CGACT=1,1"));
    waitResponse(20000L);

    sendAT(GF("+CIPMUX=1"));
    if (waitResponse() != 1) {
      return false;
    }

    return true;
  }

  bool gprsDisconnect() {
    sendAT(GF("+CIPSHUT"));
    waitResponse(5000L);

    for (int i = 0; i<3; i++) {
      sendAT(GF("+CGATT=0"));
      if (waitResponse(5000L) == 1)
        return true;
    }

    return false;
  }

  bool isGprsConnected() {
    sendAT(GF("+CGATT?"));
    if (waitResponse(GF(GSM_NL "+CGATT:")) != 1) {
      return false;
    }
    int res = stream.readStringUntil('\n').toInt();
    waitResponse();
    return (res == 1);
  }

  String getLocalIP() {
    sendAT(GF("+CIFSR"));
    String res;
    if (waitResponse(10000L, res) != 1) {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, "");
    res.trim();
    return res;
  }

  IPAddress localIP() {
    return TinyGsmIpFromString(getLocalIP());
  }

  /*
   * Phone Call functions
   */

  bool setGsmBusy(bool busy = true) TINY_GSM_ATTR_NOT_AVAILABLE;

  bool callAnswer() {
    sendAT(GF("A"));
    return waitResponse() == 1;
  }

  // Returns true on pick-up, false on error/busy
  bool callNumber(const String& number) {
    if (number == GF("last")) {
      sendAT(GF("DLST"));
    } else {
      sendAT(GF("D\""), number, "\";");
    }

    if (waitResponse(5000L) != 1) {
      return false;
    }

    if (waitResponse(60000L,
        GF(GSM_NL "+CIEV: \"CALL\",1"),
        GF(GSM_NL "+CIEV: \"CALL\",0"),
        GFP(GSM_ERROR)) != 1)
    {
      return false;
    }

    int rsp = waitResponse(60000L,
              GF(GSM_NL "+CIEV: \"SOUNDER\",0"),
              GF(GSM_NL "+CIEV: \"CALL\",0"));

    int rsp2 = waitResponse(300L, GF(GSM_NL "BUSY" GSM_NL), GF(GSM_NL "NO ANSWER" GSM_NL));

    return rsp == 1 && rsp2 == 0;
  }

  bool callHangup() {
    sendAT(GF("H"));
    return waitResponse() == 1;
  }

  // 0-9,*,#,A,B,C,D
  bool dtmfSend(char cmd, unsigned duration_ms = 100) {
    duration_ms = constrain(duration_ms, 100, 1000);

    // The duration parameter is not working, so we simulate it using delay..
    // TODO: Maybe there's another way...

    //sendAT(GF("+VTD="), duration_ms / 100);
    //waitResponse();

    sendAT(GF("+VTS="), cmd);
    if (waitResponse(10000L) == 1) {
      delay(duration_ms);
      return true;
    }
    return false;
  }

  /*
   * Audio functions
   */

  bool audioSetHeadphones() {
    sendAT(GF("+SNFS=0"));
    return waitResponse() == 1;
  }

  bool audioSetSpeaker() {
    sendAT(GF("+SNFS=1"));
    return waitResponse() == 1;
  }

  bool audioMuteMic(bool mute) {
    sendAT(GF("+CMUT="), mute);
    return waitResponse() == 1;
  }

  /*
   * Messaging functions
   */

  String sendUSSD(const String& code) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    sendAT(GF("+CSCS=\"HEX\""));
    waitResponse();
    sendAT(GF("+CUSD=1,\""), code, GF("\",15"));
    if (waitResponse(10000L) != 1) {
      return "";
    }
    if (waitResponse(GF(GSM_NL "+CUSD:")) != 1) {
      return "";
    }
    stream.readStringUntil('"');
    String hex = stream.readStringUntil('"');
    stream.readStringUntil(',');
    int dcs = stream.readStringUntil('\n').toInt();

    if (dcs == 15) {
      return TinyGsmDecodeHex7bit(hex);
    } else if (dcs == 72) {
      return TinyGsmDecodeHex16bit(hex);
    } else {
      return hex;
    }
  }

  int8_t getSMSInterrupt(void){
    sendAT(GF("+CFGRI?"));
    if(waitResponse(GF(GSM_NL "+CFGRI:")) != 1) return -1;
    return stream.readStringUntil('\n').toInt();
  }

  bool setSMSInterrupt(uint8_t status){
    sendAT(GF("+CFGRI="), status);
    if(waitResponse() != 1) return false;
    return true;
  }

  int8_t countSMS(void){
    sendAT(GF("+CMGF=1"));
    if(waitResponse() != 1) return -1;

    sendAT(GF("+CPMS?"));
    if(waitResponse(GF(GSM_NL "+CPMS:")) != 1) return -1;

    streamSkipUntil(',');
    uint8_t count = stream.readStringUntil(',').toInt() - 1;
    waitResponse();

    return count;
  }

  bool deleteSMS(){
    sendAT(GF("+CMGF=1"));
    if(waitResponse() != 1) return false;

    sendAT(GF("+CMGD=1,4"));
    if(waitResponse() != 1) return false;

    return true;
  }

  bool deleteSMS(uint8_t i){
    sendAT(GF("+CMGD="), i);
    if(waitResponse() != 1) return false;
    delay(200);
    DBG("SMS deleted OK");
    return true;
  }

  String deleteSMSOpt() {
    sendAT(GF("+CMGD=?"));
    if (waitResponse() != 1) {
      return "";
    }
    if (waitResponse(10000L, GF(GSM_NL "+CMGD::")) != 1) {
      return "";
    }
    stream.readStringUntil('"');
    String indexes = stream.readStringUntil('"');
    stream.readStringUntil(',');
    String options = stream.readStringUntil('\n');
    return indexes;
  }

  bool readSMS(uint8_t i, String& msg, String& number) {
    // +CMGR: message_status,address,[address_text],service_center_time_stamp[,address_type,TPDU_first_octet,protocol_identifier,data_coding_scheme,service_center_address,service_center_address_type,sms_message_body_length]<CR><LF>sms_message_body
    sendAT(GF("+CMGF=1"));
    if(waitResponse() != 1) return false;
    sendAT(GF("+CSDH=1"));
    if(waitResponse() != 1) return false;
    sendAT(GF("+CSCS=\"GSM\""));
    if(waitResponse() != 1) return false;

    sendAT(GF("+CMGR="), i);
    uint8_t cmgrResponse = waitResponse(GF(GSM_NL "+CMGR:"));
    if ( cmgrResponse == 1 ) {
      streamSkipUntil(','); // skip status
      streamSkipUntil('"');
      number = stream.readStringUntil('"');
      streamSkipUntil('\n');
      msg = stream.readStringUntil('\n');
      return true;
    }

    return false;
  }

  bool readAllSMSRaw(String& msg){
    sendAT(GF("+CMGF=1"));
    if(waitResponse() != 1) return false;
    sendAT(GF("+CSDH=1"));
    if(waitResponse() != 1) return false;
    sendAT(GF("+CSCS=\"GSM\""));
    if(waitResponse() != 1) return false;

    sendAT(GF("+CMGL=\"ALL\""));

    msg = "";
    const unsigned long timeout = 10000L;
    unsigned long startMillis = millis();
    bool isTimeout = false;
    String line;
    do {
      line = stream.readStringUntil('\n');
      line.trim();
      if ( line != ""  && line != "OK" ) {
        msg = msg + line + String("\r\n");
      }
      isTimeout = (millis() - startMillis) > timeout;
      delay(0);
      if ( isTimeout ) {
        DBG("timeout");
        break;
      }
    } while (line != "OK");

    return (line == "OK");
  }

  int8_t readAllSMSIndex(uint8_t indexes[], byte size){
    sendAT(GF("+CMGF=1"));
    if(waitResponse() != 1) return -1;
    sendAT(GF("+CSDH=1"));
    if(waitResponse() != 1) return -1;
    sendAT(GF("+CSCS=\"GSM\""));
    if(waitResponse() != 1) return -1;

    sendAT(GF("+CMGL=\"ALL\""));

    uint8_t nn = 0;
    uint8_t rez;
    while(true) {
      rez = waitResponse(GF(GSM_OK),GF(GSM_ERROR),GF("+CMGL:"));
      // DBG("rez=",rez);
      if(rez == 1)
        break;
      if(rez == 2)
        return -1;
      if(rez == 3) {
        if(nn >= size) {
          waitResponse();
          break;
        }
        indexes[nn] = (uint8_t)stream.readStringUntil(',').toInt();
        //DBG("SMS:",indexes[nn]);
        nn++;
      }
    }
    return nn;
  }

  bool sendSMS(const String& number, const String& text) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    delay(200);
    sendAT(GF("+CMGS=\""), number, GF("\""));
    delay(200);
    int ii = waitResponse(GF(">"));
    if (ii != 1) {
      return false;
    }
    stream.print(text);
    stream.write((char)0x1A);
    stream.flush();
    ii = waitResponse(20000L);
    delay(1000);
    return ii == 1;
  }


  /*
   * Location functions
   */

  String getGsmLocation() TINY_GSM_ATTR_NOT_AVAILABLE;

  // A7 only, 
  // starts GPS NMEA stream on GPS_TXD pin of A7 at baud rate 9600
  // https://gist.github.com/mixaz/98a4216665ebf9fe9d59e501ed480b64
  bool enableGPS(bool enable) {
    sendAT(GF("+GPS="), enable?1:0);
    return waitResponse(5000) == 1;
  }

  /*
   * Battery functions
   */

  uint16_t getBattVoltage() TINY_GSM_ATTR_NOT_AVAILABLE;

  int getBattPercent() {
    sendAT(GF("+CBC?"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) {
      return false;
    }
    stream.readStringUntil(',');
    int res = stream.readStringUntil('\n').toInt();
    waitResponse();
    return res;
  }

  /*
   * Clock functions
   */

  void syncNetworkTime(){
    sendAT(GF("+COPS=2")); // de register
    waitResponse();
    sendAT(GF("+CLTS=1")); // automatic time zone update is enabled
    waitResponse();
    sendAT(GF("+COPS=0")); // register to network
    waitResponse();
  }

  String getLocalTimestamp() {
    sendAT(GF("+CCLK?"));
    if (waitResponse(GF(GSM_NL "+CCLK:")) != 1) {
      return "";
    }

    String res = stream.readStringUntil('\n');
    waitResponse();
    return res;
  }

protected:

  bool modemConnect(const char* host, uint16_t port, uint8_t* mux) {
    sendAT(GF("+CIPSTART="),  GF("\"TCP"), GF("\",\""), host, GF("\","), port);

    if (waitResponse(20000L, GF(GSM_NL "+CIPNUM:")) != 1) {
      return false;
    }
    int newMux = stream.readStringUntil('\n').toInt();

    int rsp = waitResponse(20000L,
                           GF("CONNECT OK" GSM_NL),
                           GF("CONNECT FAIL" GSM_NL),
                           GF("ALREADY CONNECT" GSM_NL));
    if (waitResponse() != 1) {
      return false;
    }
    *mux = newMux;

    return (1 == rsp);
  }

  int modemSend(const void* buff, size_t len, uint8_t mux) {
    sendAT(GF("+CIPSEND="), mux, ',', len);
    if (waitResponse(2000L, GF(GSM_NL ">")) != 1) {
      return -1;
    }
    stream.write((uint8_t*)buff, len);
    stream.flush();
    if (waitResponse(10000L, GFP(GSM_OK), GF(GSM_NL "FAIL"), GFP(GSM_ERROR)) != 1) {
      return -1;
    }
    return len;
  }

  bool modemGetConnected(uint8_t mux) {
    sendAT(GF("+CIPSTATUS")); //TODO mux?
    int res = waitResponse(GF(",\"CONNECTED\""), GF(",\"CLOSED\""), GF(",\"CLOSING\""), GF(",\"INITIAL\""));
    waitResponse();
    return 1 == res;
  }

public:

  /* Utilities */

  template<typename T>
  void streamWrite(T last) {
    stream.print(last);
  }

  template<typename T, typename... Args>
  void streamWrite(T head, Args... tail) {
    stream.print(head);
    streamWrite(tail...);
  }

  bool streamSkipUntil(char c) {
    const unsigned long timeout = 1000L;
    unsigned long startMillis = millis();
    do {
      while (!stream.available()) { TINY_GSM_YIELD(); }
      if (stream.read() == c)
        return true;
    } while (millis() - startMillis < timeout);
    return false;
  }

  template<typename... Args>
  void sendAT(Args... cmd) {
    streamWrite("AT", cmd..., GSM_NL);
    stream.flush();
    TINY_GSM_YIELD();
    // DBG("### AT:", cmd...);
  }

  // TODO: Optimize this!
  uint8_t waitResponse(uint32_t timeout, String& data,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    int index = 0;
    unsigned long startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        int a = stream.read();
        if (a <= 0) continue; // Skip 0x00 bytes, just in case
        data += (char)a;
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF("+CIPRCV:"))) {
          int mux = stream.readStringUntil(',').toInt();
          int len = stream.readStringUntil(',').toInt();
          int len_orig = len;
          if (len > sockets[mux]->rx.free()) {
            DBG("### Buffer overflow: ", len, "->", sockets[mux]->rx.free());
          } else {
            DBG("### Got: ", len, "->", sockets[mux]->rx.free());
          }
          while (len--) {
            while (!stream.available()) { TINY_GSM_YIELD(); }
            sockets[mux]->rx.put(stream.read());
          }
          if (len_orig > sockets[mux]->available()) { // TODO
            DBG("### Fewer characters received than expected: ", sockets[mux]->available(), " vs ", len_orig);
          }
          data = "";
        } else if (data.endsWith(GF("+TCPCLOSED:"))) {
          int mux = stream.readStringUntil('\n').toInt();
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT) {
            sockets[mux]->sock_connected = false;
          }
          data = "";
          DBG("### Closed: ", mux);
        }
      }
    } while (millis() - startMillis < timeout);
finish:
    if (!index) {
      data.trim();
      if (data.length()) {
        DBG("### Unhandled:", data);
      }
      data = "";
    }
    return index;
  }

  uint8_t waitResponse(uint32_t timeout,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    String data;
    return waitResponse(timeout, data, r1, r2, r3, r4, r5);
  }

  uint8_t waitResponse(GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

protected:
  Stream&       stream;
  GsmClient*    sockets[TINY_GSM_MUX_COUNT];
};

#endif
