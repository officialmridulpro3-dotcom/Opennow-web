// SessionContext JSON (de)serialisation.
//
// The field names are the ones documented in docs/NATIVE_STREAMER.md, which is
// the contract the vendored Rust streamer implements. Keeping the exact names
// means a SessionContext produced here can be piped to the real streamer
// unchanged for debugging.
#include "onow/stream/StreamEngine.h"

#include "onow/Str.h"

namespace onow {
namespace stream {

Json SessionContext::to_json() const {
  Json j = Json::object();
  j["sessionId"] = Json(session_id);
  j["appId"] = Json(app_id);
  j["zone"] = Json(zone);
  j["region"] = Json(region);
  j["rtspsEndpoint"] = Json(rtsps_endpoint);
  j["serverCertificate"] = Json(server_certificate);
  j["clientId"] = Json(client_id);
  j["signalingUrl"] = Json(signaling_url);
  Json ice = Json::array();
  for (const std::string& server : ice_servers) ice.push_back(Json(server));
  j["iceServers"] = ice;
  Json video = Json::object();
  video["width"] = Json(static_cast<double>(width));
  video["height"] = Json(static_cast<double>(height));
  video["fps"] = Json(static_cast<double>(fps));
  video["bitrateKbps"] = Json(static_cast<double>(bitrate_kbps));
  video["codec"] = Json(codec);
  video["audioCodec"] = Json(audio_codec);
  j["video"] = video;
  Json input = Json::object();
  input["endpoint"] = Json(input_endpoint);
  input["mouseRelativeMode"] = Json(mouse_relative_mode);
  input["controllerSupport"] = Json(controller_support);
  j["input"] = input;
  Json auth = Json::object();
  auth["token"] = Json(auth_token);
  auth["sessionKey"] = Json(session_key);
  j["auth"] = auth;
  return j;
}

SessionContext SessionContext::from_json(const Json& json) {
  SessionContext ctx;
  ctx.session_id = json.find("sessionId")->as_string();
  ctx.app_id = json.find("appId")->as_string();
  ctx.zone = json.find("zone")->as_string();
  ctx.region = json.find("region")->as_string();
  ctx.rtsps_endpoint = json.find("rtspsEndpoint")->as_string();
  ctx.server_certificate = json.find("serverCertificate")->as_string();
  ctx.client_id = json.find("clientId")->as_string();
  ctx.signaling_url = json.find("signalingUrl")->as_string();
  if (const Json* ice = json.find("iceServers")) {
    for (size_t i = 0; i < ice->size(); ++i) {
      ctx.ice_servers.push_back(ice->at(i).as_string());
    }
  }
  if (const Json* video = json.find("video")) {
    ctx.width = video->find("width")->as_int(1920);
    ctx.height = video->find("height")->as_int(1080);
    ctx.fps = video->find("fps")->as_int(60);
    ctx.bitrate_kbps = video->find("bitrateKbps")->as_int(75000);
    ctx.codec = video->find("codec")->as_string();
    ctx.audio_codec = video->find("audioCodec")->as_string();
  }
  if (const Json* input = json.find("input")) {
    ctx.input_endpoint = input->find("endpoint")->as_string();
    ctx.mouse_relative_mode = input->find("mouseRelativeMode")->as_bool(true);
    ctx.controller_support = input->find("controllerSupport")->as_bool(true);
  }
  if (const Json* auth = json.find("auth")) {
    ctx.auth_token = auth->find("token")->as_string();
    ctx.session_key = auth->find("sessionKey")->as_string();
  }
  return ctx;
}

} // namespace stream
} // namespace onow
