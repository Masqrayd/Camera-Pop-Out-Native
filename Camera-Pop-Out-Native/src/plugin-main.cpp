#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>
#include "crop.hpp"

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void)
{ return "Direct OBS scene camera pop-out with independent framing and native Windows Snap."; }
MODULE_EXPORT const char *obs_module_name(void) { return "Camera Pop-Out Native"; }

namespace {
constexpr wchar_t CameraClass[] = L"MasqraydCameraPopoutNativeV1";
constexpr wchar_t SettingsClass[] = L"MasqraydCameraSettingsNativeV1";
HINSTANCE dllInstance;
HWND cameraWindow = nullptr, settingsWindow = nullptr;
// Broadcast Stack marks only this plugin's existing camera HWND. Rendering,
// cropping and source ownership remain in this plugin while it is attached.
bool stacked(HWND window) { return window && GetPropW(window, L"MasqraydBroadcastStack") != nullptr; }
obs_display_t *display = nullptr;
std::mutex stateMutex;
obs_source_t *selectedSource = nullptr;
std::string selectedName, profileName;
double zoom = 1.0, centerX = .5, centerY = .5;
bool mirror = false, alwaysTop = false, locked = false, autoOpen = true;
bool dragging = false, shuttingDown = false;
POINT lastMouse{};
RECT savedRect{100,100,580,740};
HWND combo = nullptr, profileEdit = nullptr, autoCheck = nullptr, topCheck = nullptr;
std::vector<std::string> choices;
enum { Choose = 101, Open = 102, Reset = 103, Mirror = 104, Top = 105,
       Lock = 106, Close = 107, Minimize = 108, Save = 109, Apply = 201,
       Square = 301, Wide = 302, Vertical = 303 };

std::wstring wide(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), out.data(), n);
    return out;
}
std::string narrow(const std::wstring &s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
void saveSettings()
{
    if (cameraWindow && !stacked(cameraWindow) && !IsIconic(cameraWindow) && !IsZoomed(cameraWindow)) {
        GetWindowRect(cameraWindow, &savedRect); // Screen coordinates across taskbar/monitor layouts.
    }
    char *dir = obs_module_config_path("");
    if (dir) { os_mkdirs(dir); bfree(dir); }
    char *path = obs_module_config_path("settings.json");
    obs_data_t *data = obs_data_create();
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (selectedSource && !obs_source_removed(selectedSource))
            selectedName = obs_source_get_name(selectedSource);
        obs_data_set_string(data, "source", selectedName.c_str());
        obs_data_set_string(data, "profile", profileName.c_str());
        obs_data_set_double(data, "zoom", zoom);
        obs_data_set_double(data, "center_x", centerX);
        obs_data_set_double(data, "center_y", centerY);
        obs_data_set_bool(data, "mirror", mirror);
    }
    obs_data_set_bool(data, "auto_open", autoOpen);
    obs_data_set_bool(data, "always_top", alwaysTop);
    obs_data_set_bool(data, "locked", locked);
    obs_data_set_int(data, "x", savedRect.left); obs_data_set_int(data, "y", savedRect.top);
    obs_data_set_int(data, "width", savedRect.right-savedRect.left);
    obs_data_set_int(data, "height", savedRect.bottom-savedRect.top);
    if (path && !obs_data_save_json_safe(data, path, "tmp", "bak"))
        blog(LOG_WARNING, "[camera-popout] Could not save settings");
    bfree(path); obs_data_release(data);
}
void loadSettings()
{
    char *path = obs_module_config_path("settings.json");
    obs_data_t *data = path ? obs_data_create_from_json_file(path) : nullptr;
    bfree(path);
    if (!data) return;
    obs_data_set_default_bool(data, "auto_open", true);
    obs_data_set_default_double(data, "zoom", 1);
    obs_data_set_default_double(data, "center_x", .5); obs_data_set_default_double(data, "center_y", .5);
    obs_data_set_default_int(data, "x", 100); obs_data_set_default_int(data, "y", 100);
    obs_data_set_default_int(data, "width", 480); obs_data_set_default_int(data, "height", 640);
    selectedName = obs_data_get_string(data, "source"); profileName = obs_data_get_string(data, "profile");
    zoom = std::clamp(obs_data_get_double(data, "zoom"), 1.0, 8.0);
    centerX = std::clamp(obs_data_get_double(data, "center_x"), 0.0, 1.0);
    centerY = std::clamp(obs_data_get_double(data, "center_y"), 0.0, 1.0);
    mirror = obs_data_get_bool(data, "mirror"); autoOpen = obs_data_get_bool(data, "auto_open");
    alwaysTop = obs_data_get_bool(data, "always_top"); locked = obs_data_get_bool(data, "locked");
    savedRect.left = LONG(obs_data_get_int(data,"x")); savedRect.top = LONG(obs_data_get_int(data,"y"));
    savedRect.right = savedRect.left + std::max<LONG>(160, LONG(obs_data_get_int(data,"width")));
    savedRect.bottom = savedRect.top + std::max<LONG>(90, LONG(obs_data_get_int(data,"height")));
    obs_data_release(data);
}
void setSource(obs_source_t *next)
{
    if (next) { obs_source_get_ref(next); obs_source_inc_showing(next); }
    obs_source_t *old;
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        old = selectedSource; selectedSource = next;
        if (next) selectedName = obs_source_get_name(next);
    }
    if (old) { obs_source_dec_showing(old); obs_source_release(old); }
}
void draw(void *, uint32_t width, uint32_t height)
{
    obs_source_t *source;
    double z, cx, cy; bool flip;
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        source = selectedSource ? obs_source_get_ref(selectedSource) : nullptr;
        z=zoom; cx=centerX; cy=centerY; flip=mirror;
    }
    if (!source) return;
    const uint32_t fw=obs_source_get_width(source), fh=obs_source_get_height(source);
    if (fw && fh && width && height && !obs_source_removed(source)) {
        auto c = camera::crop(fw,fh,width,height,z,cx,cy);
        gs_projection_push(); gs_viewport_push();
        gs_set_viewport(0,0,int(width),int(height));
        gs_ortho(float(flip ? c.left+c.width : c.left),
                 float(flip ? c.left : c.left+c.width),
                 float(c.top),float(c.top+c.height),-100.f,100.f);
        gs_matrix_push(); gs_matrix_identity();
        obs_source_video_render(source);
        gs_matrix_pop(); gs_viewport_pop(); gs_projection_pop();
    }
    obs_source_release(source);
}
void stopDisplay()
{
    if (display) {
        obs_display_remove_draw_callback(display, draw, nullptr);
        obs_display_destroy(display); display=nullptr;
    }
    setSource(nullptr);
}
void showSettings();
void showCamera()
{
    if (shuttingDown) return;
    obs_source_t *source = obs_get_source_by_name(selectedName.c_str());
    if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) {
        if (source) obs_source_release(source);
        showSettings(); return;
    }
    if (!cameraWindow) {
        if (!MonitorFromRect(&savedRect, MONITOR_DEFAULTTONULL)) {
            savedRect = {100,100,580,740};
        }
        cameraWindow=CreateWindowExW(WS_EX_APPWINDOW,CameraClass,L"Camera Pop-Out Native",
            WS_OVERLAPPEDWINDOW, savedRect.left,savedRect.top,
            savedRect.right-savedRect.left,savedRect.bottom-savedRect.top,
            nullptr,nullptr,dllInstance,nullptr);
        if (!cameraWindow) { obs_source_release(source); return; }
        // Hide DWM's one-pixel colored outline; preserve native resize/Snap styles.
        const COLORREF noBorder=0xfffffffe;
        DwmSetWindowAttribute(cameraWindow,34,&noBorder,sizeof(noBorder));
        RECT r; GetClientRect(cameraWindow,&r);
        gs_init_data init{};
        init.cx=std::max<LONG>(1,r.right); init.cy=std::max<LONG>(1,r.bottom);
        init.format=GS_BGRA; init.zsformat=GS_ZS_NONE; init.num_backbuffers=1;
        init.window.hwnd=cameraWindow;
        display=obs_display_create(&init,0xff000000);
        if (!display) {
            DestroyWindow(cameraWindow); cameraWindow=nullptr;
            obs_source_release(source);
            MessageBoxW(nullptr,L"OBS could not create the camera display. Check the OBS log.",L"Camera Pop-Out",MB_ICONERROR);
            return;
        }
        obs_display_add_draw_callback(display,draw,nullptr);
    }
    setSource(source); obs_source_release(source);
    SetWindowTextW(cameraWindow, wide("Camera Pop-Out: " + selectedName).c_str());
    if (stacked(cameraWindow)) {
        auto root = GetAncestor(cameraWindow, GA_ROOT);
        ShowWindow(cameraWindow, SW_SHOW); ShowWindow(root, SW_RESTORE); SetForegroundWindow(root); return;
    }
    SetWindowPos(cameraWindow,alwaysTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    ShowWindow(cameraWindow,SW_RESTORE); SetForegroundWindow(cameraWindow);
}
void contextMenu(HWND hwnd, POINT p)
{
    HMENU m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING,Choose,L"Choose OBS scene or source…");
    AppendMenuW(m,MF_STRING,Reset,L"Reset crop and zoom");
    AppendMenuW(m,MF_STRING|(mirror?MF_CHECKED:0),Mirror,L"Mirror view");
    AppendMenuW(m,MF_STRING|(alwaysTop?MF_CHECKED:0),Top,L"Always on top");
    AppendMenuW(m,MF_STRING|(locked?MF_CHECKED:0),Lock,L"Lock mouse move and resize");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,Square,L"Square window"); AppendMenuW(m,MF_STRING,Wide,L"Wide window (16:9)");
    AppendMenuW(m,MF_STRING,Vertical,L"Vertical window (9:16)");
    AppendMenuW(m,MF_STRING,Save,L"Save layout");
    AppendMenuW(m,MF_STRING,Minimize,L"Minimize"); AppendMenuW(m,MF_STRING,Close,L"Close camera window");
    if (stacked(hwnd)) {
        for (int item : {Top, Lock, Square, Wide, Vertical, Minimize})
            EnableMenuItem(m, item, MF_BYCOMMAND | MF_GRAYED);
    }
    int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);
    DestroyMenu(m);
    if (id==Choose) showSettings();
    else if (id==Close) SendMessageW(hwnd,WM_CLOSE,0,0);
    else if (id==Minimize) ShowWindow(hwnd,SW_MINIMIZE);
    else if (id==Top) { alwaysTop=!alwaysTop; SetWindowPos(hwnd,alwaysTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE); }
    else if (id==Lock) locked=!locked;
    else if (id==Reset || id==Mirror) {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (id==Reset) {zoom=1;centerX=centerY=.5;} else mirror=!mirror;
    } else if (id==Square || id==Wide || id==Vertical) {
        RECT r;GetWindowRect(hwnd,&r); int h=r.bottom-r.top;
        double ratio=id==Wide?16./9.:id==Vertical?9./16.:1.;
        SetWindowPos(hwnd,nullptr,0,0,std::max(160,int(h*ratio)),h,SWP_NOMOVE|SWP_NOZORDER);
    }
    if (id && id!=Close) saveSettings();
}
LRESULT CALLBACK cameraProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
    case WM_NCCALCSIZE:
        if (wp) {
            // Keep all native window styles, but make the entire frame client area.
            if (IsZoomed(hwnd)) {
                auto *p=reinterpret_cast<NCCALCSIZE_PARAMS *>(lp);
                MONITORINFO mi{sizeof(mi)};
                if (GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi)) p->rgrc[0]=mi.rcWork;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        if (stacked(hwnd)) return HTCLIENT;
        if (locked) return HTCLIENT;
        RECT r;GetWindowRect(hwnd,&r);
        int x=GET_X_LPARAM(lp), y=GET_Y_LPARAM(lp);
        if (GetKeyState(VK_CONTROL)&0x8000) return HTCAPTION;
        if (!IsZoomed(hwnd)) {
            int edge=std::max(6,int(GetDpiForWindow(hwnd)*8/96));
            bool l=x<r.left+edge, rr=x>=r.right-edge, t=y<r.top+edge, b=y>=r.bottom-edge;
            if (t&&l) return HTTOPLEFT; if (t&&rr) return HTTOPRIGHT;
            if (b&&l) return HTBOTTOMLEFT; if (b&&rr) return HTBOTTOMRIGHT;
            if (l) return HTLEFT; if(rr) return HTRIGHT; if(t) return HTTOP; if(b) return HTBOTTOM;
        }
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        if (stacked(hwnd)) return 0;
        auto *r=reinterpret_cast<RECT *>(lp);
        SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;
    }
    case WM_SIZE:
        if (display) { obs_display_set_enabled(display,wp!=SIZE_MINIMIZED);
            if (wp!=SIZE_MINIMIZED) obs_display_resize(display,LOWORD(lp),HIWORD(lp)); }
        return 0;
    case WM_GETMINMAXINFO: {
        auto *p=reinterpret_cast<MINMAXINFO *>(lp);p->ptMinTrackSize={160,90};return 0;
    }
    case WM_LBUTTONDOWN: dragging=true;lastMouse={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};SetCapture(hwnd);return 0;
    case WM_MOUSEMOVE:
        if (dragging) {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};RECT r;GetClientRect(hwnd,&r);
            std::lock_guard<std::mutex> guard(stateMutex);
            if (selectedSource) {
                double fw=obs_source_get_width(selectedSource),fh=obs_source_get_height(selectedSource);
                auto c=camera::crop(fw,fh,r.right,r.bottom,zoom,centerX,centerY);
                if (fw>0 && fh>0 && r.right>0 && r.bottom>0) {
                    // Start from the clamped visible center, so edges never become sticky.
                    centerX=std::clamp((c.left+c.width/2)/fw+(mirror?1:-1)*(p.x-lastMouse.x)*c.width/(r.right*fw),0.,1.);
                    centerY=std::clamp((c.top+c.height/2)/fh-(p.y-lastMouse.y)*c.height/(r.bottom*fh),0.,1.);
                }
            }
            lastMouse=p;
        }return 0;
    case WM_LBUTTONUP: dragging=false;ReleaseCapture();saveSettings();return 0;
    case WM_CAPTURECHANGED: dragging=false;return 0;
    case WM_MOUSEWHEEL: {
        std::lock_guard<std::mutex> guard(stateMutex);
        zoom=std::clamp(zoom*std::pow(1.12,double(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA),1.,8.);return 0;
    }
    case WM_CONTEXTMENU: {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if (p.x==-1&&p.y==-1) GetCursorPos(&p);
        contextMenu(hwnd,p);return 0;
    }
    case WM_EXITSIZEMOVE: saveSettings();return 0;
    case WM_CLOSE: saveSettings();stopDisplay();DestroyWindow(hwnd);return 0;
    case WM_DESTROY: cameraWindow=nullptr;return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
HWND control(HWND parent,const wchar_t *kind,const wchar_t *text,DWORD style,int x,int y,int w,int h,int id=0)
{
    HWND result=CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(INT_PTR(id)),dllInstance,nullptr);
    SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return result;
}
bool enumerate(void *,obs_source_t *source)
{
    if (obs_source_get_output_flags(source)&OBS_SOURCE_VIDEO) choices.emplace_back(obs_source_get_name(source));
    return true;
}
void populate()
{
    choices.clear();obs_enum_scenes(enumerate,nullptr);obs_enum_sources(enumerate,nullptr);
    std::sort(choices.begin(),choices.end());choices.erase(std::unique(choices.begin(),choices.end()),choices.end());
    SendMessageW(combo,CB_RESETCONTENT,0,0);int selected=-1;
    for (size_t i=0;i<choices.size();++i) {
        auto label=wide(choices[i]);SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        if(choices[i]==selectedName) selected=int(i);
    }
    SendMessageW(combo,CB_SETCURSEL,selected,0);
}
LRESULT CALLBACK settingsProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_CREATE) {
        control(hwnd,L"STATIC",L"OBS scene or video source",0,18,18,460,20);
        combo=control(hwnd,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,18,43,460,250);
        control(hwnd,L"STATIC",L"OBS profile name (blank = all profiles)",0,18,82,460,20);
        profileEdit=control(hwnd,L"EDIT",wide(profileName).c_str(),WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,18,107,460,25);
        autoCheck=control(hwnd,L"BUTTON",L"Open automatically with OBS",BS_AUTOCHECKBOX|WS_TABSTOP,18,146,460,24);
        topCheck=control(hwnd,L"BUTTON",L"Always on top",BS_AUTOCHECKBOX|WS_TABSTOP,18,178,460,24);
        SendMessageW(autoCheck,BM_SETCHECK,autoOpen?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(topCheck,BM_SETCHECK,alwaysTop?BST_CHECKED:BST_UNCHECKED,0);
        control(hwnd,L"STATIC",L"Wheel: zoom    Drag: pan crop    Ctrl+drag: move / Snap",0,18,214,470,20);
        control(hwnd,L"BUTTON",L"Apply and open",BS_DEFPUSHBUTTON|WS_TABSTOP,300,250,178,32,Apply);
        populate();return 0;
    }
    if(msg==WM_COMMAND && LOWORD(wp)==Apply) {
        int index=int(SendMessageW(combo,CB_GETCURSEL,0,0));
        if(index<0 || index>=int(choices.size())) {MessageBoxW(hwnd,L"Choose a scene or source first.",L"Camera Pop-Out",MB_OK);return 0;}
        { std::lock_guard<std::mutex> guard(stateMutex); selectedName=choices[size_t(index)]; }
        int n=GetWindowTextLengthW(profileEdit);std::wstring text(size_t(n)+1,L'\0');GetWindowTextW(profileEdit,text.data(),n+1);text.resize(n);
        profileName=narrow(text);autoOpen=SendMessageW(autoCheck,BM_GETCHECK,0,0)==BST_CHECKED;
        alwaysTop=SendMessageW(topCheck,BM_GETCHECK,0,0)==BST_CHECKED;
        showCamera();saveSettings();DestroyWindow(hwnd);return 0;
    }
    if(msg==WM_CLOSE) {DestroyWindow(hwnd);return 0;}
    if(msg==WM_DESTROY) {settingsWindow=nullptr;combo=nullptr;return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void showSettings()
{
    if(shuttingDown)return;
    if(!settingsWindow) settingsWindow=CreateWindowExW(WS_EX_TOOLWINDOW,SettingsClass,L"Camera Pop-Out Settings",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,160,160,520,335,
        static_cast<HWND>(obs_frontend_get_main_window_handle()),nullptr,dllInstance,nullptr);
    ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);
}
bool profileMatches()
{
    if(profileName.empty())return true;
    char *name=obs_frontend_get_current_profile();bool result=name && profileName==name;bfree(name);return result;
}
void event(enum obs_frontend_event e,void *)
{
    if(e==OBS_FRONTEND_EVENT_FINISHED_LOADING || e==OBS_FRONTEND_EVENT_PROFILE_CHANGED || e==OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
        if (e==OBS_FRONTEND_EVENT_PROFILE_CHANGED && !profileMatches()) {
            if(cameraWindow)SendMessageW(cameraWindow,WM_CLOSE,0,0);
        } else if(autoOpen && profileMatches() && !selectedName.empty())showCamera();
        if(settingsWindow)populate();
    } else if(e==OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
        if(cameraWindow)SendMessageW(cameraWindow,WM_CLOSE,0,0);
    } else if(e==OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN || e==OBS_FRONTEND_EVENT_EXIT) {
        shuttingDown=true;
        if(cameraWindow)SendMessageW(cameraWindow,WM_CLOSE,0,0);
        if(settingsWindow)DestroyWindow(settingsWindow);
    }
}
void tools(void *) {showSettings();}
}
MODULE_EXPORT bool obs_module_load(void)
{
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&obs_module_load),&dllInstance);
    WNDCLASSW wc{};wc.hInstance=dllInstance;wc.lpfnWndProc=cameraProc;wc.lpszClassName=CameraClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc))return false;
    wc.lpfnWndProc=settingsProc;wc.lpszClassName=SettingsClass;
    if(!RegisterClassW(&wc)){UnregisterClassW(CameraClass,dllInstance);return false;}
    loadSettings();obs_frontend_add_tools_menu_item("Camera Pop-Out Native",tools,nullptr);
    obs_frontend_add_event_callback(event,nullptr);
    blog(LOG_INFO,"[camera-popout] Native plugin loaded");return true;
}
MODULE_EXPORT void obs_module_unload(void)
{
    shuttingDown=true;obs_frontend_remove_event_callback(event,nullptr);
    if(cameraWindow)SendMessageW(cameraWindow,WM_CLOSE,0,0);
    if(settingsWindow)DestroyWindow(settingsWindow);
    stopDisplay();UnregisterClassW(CameraClass,dllInstance);UnregisterClassW(SettingsClass,dllInstance);
}
