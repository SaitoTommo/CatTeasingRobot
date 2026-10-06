#pragma once
#include <ArduinoJson.h>

// Publish readback only after the full candidate has been persisted.
template<class Storage>
bool persistPolicySettings(Storage &storage,JsonDocument &candidate,JsonDocument &active){
  String saved;serializeJson(candidate,saved);
  if(storage.putString("config",saved)!=saved.length())return false;
  active.set(candidate);return true;
}
