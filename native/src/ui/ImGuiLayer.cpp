#include "onow/ui/ImGuiLayer.h"

#include "onow/Fs.h"
#include "onow/Log.h"

#include "imgui.h"
#include "imgui_internal.h"

#if defined(ONOW_PLATFORM_WIN32)
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"
#elif defined(ONOW_PLATFORM_GLFW)
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#endif

namespace onow {

namespace {

const char* const kDisplayCandidates[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/montserrat-bold.ttf",
    "C:/Windows/Fonts/seguisb.ttf",
    "C:/Windows/Fonts/segoeuib.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/Supplemental/Montserrat-Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/SFNS.ttf",
#else
    "/usr/share/fonts/truetype/montserrat/Montserrat-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
#endif
};

const char* const kBodyCandidates[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/montserrat.ttf",
    "C:/Windows/Fonts/Montserrat-Regular.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/Supplemental/Montserrat-Regular.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/SFNS.ttf",
#else
    "/usr/share/fonts/truetype/montserrat/Montserrat-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
};

const char* const kMonoCandidates[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/cour.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Monaco.ttf",
#else
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
#endif
};

} // namespace

void* ImGuiLayer::load_font(const char* path, float size_pixels) {
  if (!path || !*path) return nullptr;
  if (!fs_is_file(path)) return nullptr;
  ImFontConfig config;
  config.PixelSnapH = true;
  config.OversampleH = 2;
  config.OversampleV = 1;
  return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size_pixels * content_scale_, &config);
}

void ImGuiLayer::load_family(const char* const* paths, size_t count, float size,
                             ImFont** out) {
  for (size_t i = 0; i < count; ++i) {
    ImFont* font = static_cast<ImFont*>(load_font(paths[i], size));
    if (font) {
      *out = font;
      return;
    }
  }
}

bool ImGuiLayer::init(Platform& platform, bool install_backend) {
  platform_ = &platform;
  content_scale_ = platform.content_scale();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr; // the app owns its layout, not an .ini file
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

#if defined(ONOW_PLATFORM_WIN32) || defined(ONOW_PLATFORM_GLFW)
  if (install_backend) {
    if (!ImGui_ImplOpenGL3_Init("#version 150")) {
      ONOW_ERROR("UI", "ImGui_ImplOpenGL3_Init failed");
      return false;
    }
#if defined(ONOW_PLATFORM_WIN32)
    if (!ImGui_ImplWin32_Init(platform.native_window_handle())) {
      ONOW_ERROR("UI", "ImGui_ImplWin32_Init failed");
      return false;
    }
#elif defined(ONOW_PLATFORM_GLFW)
    if (!ImGui_ImplGlfw_InitForOpenGL(
            static_cast<GLFWwindow*>(platform.native_window_handle()), true)) {
      ONOW_ERROR("UI", "ImGui_ImplGlfw_InitForOpenGL failed");
      return false;
    }
#endif
    backend_installed_ = true;
  }
#endif

  ImFont* fallback = ImGui::GetIO().Fonts->AddFontDefault();

  // The web design's four type steps, in the display and body families.
  load_family(kDisplayCandidates, sizeof(kDisplayCandidates) / sizeof(char*), 24.0f,
              &fonts_.display);
  if (!fonts_.display) fonts_.display = fallback;
  load_family(kBodyCandidates, sizeof(kBodyCandidates) / sizeof(char*), 14.0f, &fonts_.body);
  if (!fonts_.body) fonts_.body = fallback;
  load_family(kBodyCandidates, sizeof(kBodyCandidates) / sizeof(char*), 18.0f, &fonts_.title);
  if (!fonts_.title) fonts_.title = fonts_.body;
  load_family(kBodyCandidates, sizeof(kBodyCandidates) / sizeof(char*), 11.0f,
              &fonts_.caption);
  if (!fonts_.caption) fonts_.caption = fonts_.body;

  ImFont* mono = nullptr;
  load_family(kMonoCandidates, sizeof(kMonoCandidates) / sizeof(char*), 14.0f, &mono);
  fonts_.mono = mono ? mono : fonts_.body;

#if defined(ONOW_PLATFORM_HEADLESS)
  // No renderer backend is linked here, so nothing else will rasterise the
  // atlas; do it ourselves or ImGui refuses to start a frame.
  if (!ImGui::GetIO().Fonts->Build()) {
    ONOW_ERROR("UI", "font atlas build failed");
    return false;
  }
#endif
  set_type_scale(fonts_);
  initialized_ = true;
  ONOW_INFO("UI", "font atlas built (%d glyphs, %.0f px scale)",
            ImGui::GetIO().Fonts->IsBuilt() ? -1 : 0, content_scale_);
  return true;
}

void ImGuiLayer::shutdown() {
  if (!initialized_) return;
  initialized_ = false;
#if defined(ONOW_PLATFORM_WIN32)
  if (backend_installed_) ImGui_ImplWin32_Shutdown();
#elif defined(ONOW_PLATFORM_GLFW)
  if (backend_installed_) ImGui_ImplGlfw_Shutdown();
#endif
#if defined(ONOW_PLATFORM_WIN32) || defined(ONOW_PLATFORM_GLFW)
  if (backend_installed_) ImGui_ImplOpenGL3_Shutdown();
#endif
  ImGui::DestroyContext();
}

void ImGuiLayer::new_frame() {
  if (!initialized_) return;
#if defined(ONOW_PLATFORM_HEADLESS)
  // No window system drives the io struct, so publish the synthetic surface
  // ourselves — otherwise ImGui's sanity checks trip on a zero DisplaySize.
  int width = 0;
  int height = 0;
  platform_->framebuffer_size(width, height);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(width > 0 ? (float)width : 1280.0f, height > 0 ? (float)height : 720.0f);
  io.DeltaTime = 1.0f / 60.0f;
#endif
#if defined(ONOW_PLATFORM_WIN32)
  if (backend_installed_) ImGui_ImplWin32_NewFrame();
#elif defined(ONOW_PLATFORM_GLFW)
  if (backend_installed_) ImGui_ImplGlfw_NewFrame();
#endif
#if defined(ONOW_PLATFORM_WIN32) || defined(ONOW_PLATFORM_GLFW)
  if (backend_installed_) ImGui_ImplOpenGL3_NewFrame();
#endif
  ImGui::NewFrame();
}

void ImGuiLayer::render() {
  if (!initialized_) return;
  ImGui::Render();
#if defined(ONOW_PLATFORM_WIN32) || defined(ONOW_PLATFORM_GLFW)
  if (backend_installed_) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

} // namespace onow
