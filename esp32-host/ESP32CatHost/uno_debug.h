#pragma once
// All access runs under stateLock on the control loop.
struct DebugRow { String line; uint32_t at=0; };
DebugRow hardwareRows[13], unoConfigRow;
const char *hardwareKeys[]={"STATE","ML","MR","EL","ER","SL","SR","T_TOP","T_LEFT","T_RIGHT","T_REAR","OUT","FRONT"};
String debugResult="", pendingManual, configCommands[3];
uint32_t debugId=0, debugAt=0, manualUntil=0, manualFrontAt=0, manualDuration=0;
int debugStage=0, configStep=-1;
float configExpected[4]={};
bool configReadback=false;
bool benchBypass=false, manualBypass=false, manualForward=false;
String manualFront(){
  bool fresh=observation["at"].is<uint32_t>() && millis()-observation["at"].as<uint32_t>()<config["visionMaxAgeMs"].as<uint32_t>();
  bool source=(mode=="SIM_UNO")==(String(observation["source"]|"")=="SIM_CAMERA");
  String front=fresh&&source?String(observation["front"]|"INVALID"):"INVALID";
  return front=="CLEAR"||front=="BLOCKED"?front:"INVALID";
}
void debugReset(){for(auto &r:hardwareRows){r.line="";r.at=0;}unoConfigRow.line="";unoConfigRow.at=0;debugId=manualUntil=0;debugStage=0;configStep=-1;pendingManual="";benchBypass=manualBypass=manualForward=false;debugResult="等待 Uno 回报";}
void debugCancel(){debugId=manualUntil=0;debugStage=0;configStep=-1;pendingManual="";manualBypass=manualForward=false;}
void debugSend(const String &command){debugId=sendUno(command);debugAt=millis();}
void debugFail(const String &reason){debugCancel();policyControl("stop");debugResult=reason;logLine("DEBUG_ERROR",reason);}
void debugAccept(const String &line){
  if(line.startsWith("READY")){debugReset();return;}
  unsigned id=0;char kind[16]={},area[16]={};
  if(sscanf(line.c_str(),"DATA %u HW %15s %15s",&id,kind,area)>=3){
    String key=String(kind)=="T"?String("T_")+area:String(kind);
    for(int i=0;i<13;i++)if(key==hardwareKeys[i]){hardwareRows[i].line=line;hardwareRows[i].at=millis();break;}
  }
  if(line.startsWith("DATA ")&&line.indexOf(" CONFIG ")>=0){unoConfigRow.line=line;unoConfigRow.at=millis();
    float w=0,t=0,l=0,r=0;unsigned slots=0;
    if(configStep==3&&sscanf(line.c_str(),"DATA %u CONFIG WHEEL_MM=%f TRACK_MM=%f SLOTS_PER_REV=%u SAFE_LEFT_CM=%f SAFE_RIGHT_CM=%f",&id,&w,&t,&slots,&l,&r)==6&&id==debugId)
      configReadback=fabs(l-configExpected[0])<0.11f&&fabs(r-configExpected[1])<0.11f&&fabs(w-configExpected[2])<0.11f&&fabs(t-configExpected[3])<0.11f;
  }
  if(!debugId)return;
  if(sscanf(line.c_str(),"ERROR %u",&id)==1&&id==debugId){debugFail(line);return;}
  if(sscanf(line.c_str(),"DONE %u",&id)!=1||id!=debugId)return;
  debugId=0;
  if(configStep>=0){if(++configStep<3)debugSend(configCommands[configStep]);else if(configStep==3){configReadback=false;debugSend("GET_CONFIG");debugResult="Arduino 已确认设置，正在回读核对";}else{configStep=-1;debugResult=configReadback?"Arduino 设置成功，四项参数回读一致":"回读缺失或不一致；请重新读取并核对，部分设置可能已生效";}return;}
  if(debugStage==1||debugStage==2){debugStage=0;sendUno(pendingManual);pendingManual="";manualUntil=millis()+manualDuration+100;debugResult=manualBypass?"限时测试已下发：前向、侧向和触碰避障已绕过":"限时测试已下发：避障保持开启";}
}
void debugSnapshot(JsonDocument &d){
  JsonArray rows=d["unoTelemetry"].to<JsonArray>();
  for(int i=0;i<13;i++)if(hardwareRows[i].line.length()){JsonObject r=rows.add<JsonObject>();r["line"]=hardwareRows[i].line;r["at"]=hardwareRows[i].at;r["source"]=mode=="UART"?"UNO_RX":"SIM_RX";}
  d["unoConfig"]["line"]=unoConfigRow.line;d["unoConfig"]["at"]=unoConfigRow.at;d["unoConfig"]["source"]=mode=="UART"?"UNO_RX":"SIM_RX";
  d["debugResult"]=debugResult;d["debugBusy"]=bool(debugId||manualUntil||configStep>=0);d["benchBypass"]=benchBypass;
}
void debugTick(){
  if(debugId&&millis()-debugAt>2500){debugFail("Uno 确认超时；请检查 UART 接线与固件");return;}
  if(!manualUntil)return;
  if((int32_t)(millis()-manualUntil)>=0){debugCancel();policyControl("stop");debugResult="限时测试结束，已发送 STOP / SESSION_STOP";return;}
  if(!debugStage&&!manualBypass&&manualForward&&(config["frontAvoidanceEnabled"]|true)&&manualFront()!="CLEAR")debugFail("ESP 前向观测无效或存在障碍，已停止测试");
}
void debugInput(JsonDocument &d){
  String op=d["op"]|"",action=d["action"]|"";
  if(op=="manual_test"&&action=="stop"){debugCancel();policyControl("stop");debugResult="已发送停止命令";return;}
  if(phase!="IDLE"&&phase!="STOPPED"||!stopReady()){debugResult="请先停止巡游";logLine("DEBUG_ERROR",debugResult);return;}
  if(debugId||manualUntil||configStep>=0){debugResult="请等待当前操作完成，或点击停止";logLine("DEBUG_ERROR",debugResult);return;}
  if(op=="bench_safety"){
    if(!d["bypass"].is<bool>()){debugResult="避障开关必须是布尔值";return;}
    benchBypass=d["bypass"].as<bool>();debugResult=benchBypass?"手动限时测试绕过全部避障；自动巡游仍检查避障":"手动测试避障已恢复";return;
  }
  if(op=="uno_config"){
    float l=d["leftCm"]|0.0f,r=d["rightCm"]|0.0f,w=d["wheelMm"]|0.0f,t=d["trackMm"]|0.0f;
    if(!isfinite(l)||!isfinite(r)||!isfinite(w)||!isfinite(t)||l<1||l>100||r<1||r>100||w<20||w>150||t<50||t>300){debugResult="参数范围错误";logLine("DEBUG_ERROR",debugResult);return;}
    configCommands[0]="SET_SAFE_DISTANCE LEFT "+String(l,1);configCommands[1]="SET_SAFE_DISTANCE RIGHT "+String(r,1);configCommands[2]="SET_GEOMETRY "+String(w,1)+" "+String(t,1);
    configExpected[0]=l;configExpected[1]=r;configExpected[2]=w;configExpected[3]=t;
    configStep=0;debugResult="正在逐项设置，等待 Arduino 确认";debugSend(configCommands[0]);return;
  }
  String command;
  if(action=="ping")command="PING";
  else if(action=="status")command="GET_STATUS";
  else if(action=="config")command="GET_CONFIG";
  else if(action=="telemetry")command="GET_TELEMETRY";
  else if(action=="lock")command="SESSION_STOP";
  else if(action=="unlock")command="SESSION_START";
  else if(action=="led_on")command="LED ON";
  else if(action=="led_off")command="LED OFF";
  else if(action=="beep")command="BEEP 180";
  else if(action=="forward"||action=="backward"||action=="left"||action=="right"||action=="left_forward"||action=="left_reverse"||action=="right_forward"||action=="right_reverse"){
    if(!d["pwm"].is<int>()||!d["durationMs"].is<int>()){debugResult="PWM 和时长必须是整数";return;}
    int pwm=d["pwm"],duration=d["durationMs"];
    if(pwm<1||pwm>255||duration<50||duration>2000){debugResult="PWM 1–255；时长 50–2000 ms";return;}
    int left=0,right=0;
    if(action=="forward")left=right=pwm;
    else if(action=="backward")left=right=-pwm;
    else if(action=="left"){left=-pwm;right=pwm;}
    else if(action=="right"){left=pwm;right=-pwm;}
    else if(action=="left_forward")left=pwm;
    else if(action=="left_reverse")left=-pwm;
    else if(action=="right_forward")right=pwm;
    else right=-pwm;
    if(!benchBypass&&(config["frontAvoidanceEnabled"]|true)&&(left>0&&right>=0||right>0&&left>=0)&&manualFront()!="CLEAR"){debugResult="前向观测无效；可打开测试绕过避障，或先完成标定";logLine("DEBUG_ERROR",debugResult);return;}
    manualBypass=benchBypass;manualForward=(left>0&&right>=0)||(right>0&&left>=0);
    pendingManual="TEST_MOTOR "+String(left)+" "+String(right)+" "+String(duration)+(manualBypass?" BYPASS":" SAFE");sendUno("STOP");debugStage=1;manualDuration=duration;debugSend("SESSION_START");manualUntil=millis()+duration+5500;debugResult=manualBypass?"等待会话解锁；本次测试绕过全部避障":"等待会话解锁；前向判定由 ESP 负责";return;
  }else{debugResult="未知测试动作";return;}
  sendUno(command);debugResult="已下发 "+command+"；请查看 Uno 回报";
}
