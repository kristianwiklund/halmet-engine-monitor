#pragma once

// ============================================================
//  SwitchMetadata.h — SKMetadata subclass for boolean switch
//  paths that advertise supportsPut:true (needed by KIP).
// ============================================================

#include <sensesp/signalk/signalk_output.h>

class SwitchMetadata : public sensesp::SKMetadata {
 public:
  explicit SwitchMetadata(const String& display_name)
      : display_name_(display_name) {}
  void add_entry(const String& sk_path, JsonArray& meta) override {
    JsonObject json = meta.add<JsonObject>();
    json["path"] = sk_path;
    JsonObject val = json["value"].to<JsonObject>();
    val["displayName"] = display_name_;
    val["supportsPut"] = true;
  }
 private:
  String display_name_;
};
