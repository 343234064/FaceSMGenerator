// FacialShadowMapGenerator.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "imgui/imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "ThreadProcess.h"

#include <ShObjIdl_core.h>
#include <stdio.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb-master/stb_image.h"

#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <tchar.h>



// Data
static ID3D11Device* g_pd3dDevice = NULL;
static ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
static IDXGISwapChain* g_pSwapChain = NULL;
static ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

typedef ID3D11ShaderResourceView EngineTextureID;

struct TextureboxItem
{
public:
    TextureboxItem(std::string InPath = "None", bool InSelected = false, bool InLoaded = false, int InWidth = 0, int InHeight = 0, unsigned char* InData = NULL) :
        Path(InPath),
        Selected(InSelected),
        Loaded(InLoaded),
        Width(InWidth),
        Height(InHeight),
        Data(InData),
        TextureID(NULL),
        SDFData(NULL),
        SDFTextureID(NULL)
    {}

    std::string Path;
    bool Selected;
    bool Loaded;
    int Width;
    int Height;
    unsigned char* Data;
    EngineTextureID* TextureID;

    unsigned char* SDFData;
    EngineTextureID* SDFTextureID;
};


template<typename ... Args>
std::string string_format(const std::string& format, Args ... args)
{
    int size = snprintf(nullptr, 0, format.c_str(), args ...) + 1;
    if (size <= 0) { throw std::runtime_error("Error during formatting."); }
    std::unique_ptr<char[]> buf(new char[size]);
    snprintf(buf.get(), size, format.c_str(), args ...);
    return std::string(buf.get(), buf.get() + size - 1); 
}

static void ShowHelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string ToUtf8(const std::wstring& str)
{
    std::string ret;
    int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0, NULL, NULL);
    if (len > 0)
    {
        ret.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.length(), &ret[0], len, NULL, NULL);
    }
    return ret;
}

std::vector<std::string> BrowseFile()
{
    IFileOpenDialog* pfd = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pfd));

    std::vector<std::string> SeletedFiles;
    if (SUCCEEDED(hr))
    {
        DWORD dwFlags;
        COMDLG_FILTERSPEC rgSpec[] =
        {
            { L"PNG", L"*.png" },
            { L"JPG", L"*.jpg;*.jpeg" }
        };

        hr = pfd->GetOptions(&dwFlags);
        hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
        hr = pfd->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
    
        hr = pfd->Show(NULL);
        if (SUCCEEDED(hr))
        {
            IShellItemArray* psiaResults;
            hr = pfd->GetResults(&psiaResults);
            if (SUCCEEDED(hr))
            {
                DWORD resultNum;
                hr = psiaResults->GetCount(&resultNum);
                for (DWORD i = 0; i < resultNum; i++)
                {
                    IShellItem* psiaResult;
                    hr = psiaResults->GetItemAt(i, &psiaResult);
                    PWSTR pszFilePath = NULL;
                    hr = psiaResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr))
                    {
                        //MessageBoxW(NULL, pszFilePath, L"File Path", MB_OK);
                        std::wstring FilePath = std::wstring(pszFilePath);
                        std::string UTF8FilePath = ToUtf8(FilePath);
                        SeletedFiles.push_back(UTF8FilePath);
                    }
                    CoTaskMemFree(pszFilePath);
                }
            }
            psiaResults->Release();
        }
        pfd->Release();
    }
    CoUninitialize();

    return SeletedFiles;
}



bool CreateTextureResource(unsigned char* ImageData, int ImageWidth, int ImageHeight, int channel, EngineTextureID** OutResource)
{
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = ImageWidth;
    desc.Height = ImageHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = ImageData;
    subResource.SysMemPitch = desc.Width * channel;
    subResource.SysMemSlicePitch = 0;

    HRESULT hr;
    hr = g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    if (!SUCCEEDED(hr))
        return false;

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    hr = g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, OutResource);
    if (!SUCCEEDED(hr))
        return false;

    pTexture->Release();

    return true;
}


class UIManager
{
#define DIGIT_INPUTTEXT_SIZE 8
#define TEXTUREBOX_STATE_TEXT_SIZE 32
#define HINT_TEXT_SIZE 128

 typedef std::vector<TextureboxItem>::iterator ItemIter;
public:
    std::vector<TextureboxItem> TextureboxList;
    
    char SampleTimesText[DIGIT_INPUTTEXT_SIZE] = "200";
    char BlurSizeText[DIGIT_INPUTTEXT_SIZE] = "4";
    char TextureboxStateText[TEXTUREBOX_STATE_TEXT_SIZE] = "";
    char GenerateHintText[HINT_TEXT_SIZE] = "";
    char ProgressHintText[HINT_TEXT_SIZE] = "";
    
    EngineTextureID* PreviewTexture = nullptr;
    
    const float PreviewSliderMin = 0.0f;
    const float PreviewSliderMax = 1.0f;
    float PreviewSliderValue = 0.5f;

private:
    ThreadProcesser* AsyncProcesser = nullptr;

public:
    UIManager() {}
    ~UIManager()
    {
        if (AsyncProcesser != nullptr)
        {
            delete AsyncProcesser;
            AsyncProcesser = nullptr;
        }

        ItemIter it;
        for (it = TextureboxList.begin(); it != TextureboxList.end(); it++)
        {
            UnLoadTextureData(it);
        }
    }

    void OnAddButtonClicked()
    {
        snprintf(ProgressHintText, HINT_TEXT_SIZE, "");
        if (AsyncProcesser != nullptr)
        {
            if (AsyncProcesser->IsWorking())
            {
                snprintf(ProgressHintText, HINT_TEXT_SIZE, "There are stll some work handing, please wait..");
                return;
            }
        }

        std::vector<std::string> FilesPaths = BrowseFile();
        int FilesNum = (int)FilesPaths.size();
       
        for (int i = 0; i < FilesNum; i++)
        {
            TextureboxItem Item;
            Item.Path = FilesPaths[i].c_str();
            TextureboxList.push_back(Item);
        }

        snprintf(TextureboxStateText, TEXTUREBOX_STATE_TEXT_SIZE, "%d Textures Added", (int)TextureboxList.size());

        ItemIter it;
        for (it = TextureboxList.begin(); it != TextureboxList.end(); it++)
        {
            LoadTextureData(it);
        }
    }

    void OnDeleteButtonClicked()
    {
        snprintf(ProgressHintText, HINT_TEXT_SIZE, "");
        if (AsyncProcesser != nullptr)
        {
            if (AsyncProcesser->IsWorking())
            {
                snprintf(ProgressHintText, HINT_TEXT_SIZE, "There are stll some work handing, please wait..");
                return;
            }
        }

        ItemIter it;
        for (it = TextureboxList.begin(); it != TextureboxList.end(); )
        {
            if ((*it).Selected)
            {
                UnLoadTextureData(it);
                it = TextureboxList.erase(it);
            }
            else
            {
                it++;
            }
        }

        snprintf(TextureboxStateText, TEXTUREBOX_STATE_TEXT_SIZE, "%d Textures Added", (int)TextureboxList.size());
    }

    void OnDeleteAllButtonClicked()
    {
        snprintf(ProgressHintText, HINT_TEXT_SIZE, "");
        if (AsyncProcesser != nullptr)
        {
            if (AsyncProcesser->IsWorking())
            {
                snprintf(ProgressHintText, HINT_TEXT_SIZE, "There are stll some work handing, please wait..");
                return;
            }
        }

        ItemIter it;
        for (it = TextureboxList.begin(); it != TextureboxList.end(); it++)
        {
            UnLoadTextureData(it);
        }
        TextureboxList.clear();
        PreviewTexture = nullptr;

        snprintf(TextureboxStateText, TEXTUREBOX_STATE_TEXT_SIZE, "%d Textures Added", (int)TextureboxList.size());
    }

    void OnReloadSelectedClicked()
    {

    }

    void OnGenerateButtonClicked()
    {
        bool Ready = true;
        bool FirstTexture = true;
        int Width;
        int Height;

        if (TextureboxList.size() < 2)
        {
            Ready = false;
            snprintf(GenerateHintText, HINT_TEXT_SIZE, "Please add at least 2 texture");
        }
        else {
            for (int i = 0; i < TextureboxList.size(); i++)
            {
                TextureboxItem& Item = TextureboxList[i];
                if (!Item.Loaded)
                {
                    snprintf(GenerateHintText, HINT_TEXT_SIZE, "Texture %d still unload in texture box", i);
                    Ready = false;
                    break;
                }
                if (FirstTexture)
                {
                    Width = Item.Width;
                    Height = Item.Height;
                    FirstTexture = false;
                }
                if (Width != Item.Width || Height != Item.Height)
                {
                    snprintf(GenerateHintText, HINT_TEXT_SIZE, "The width and height of Texture %d is not consistent with the others", i);
                    Ready = false;
                    break;
                }
            }
        }

        if (Ready)
        {
            snprintf(GenerateHintText, HINT_TEXT_SIZE, "");


            if (AsyncProcesser == nullptr)
            {
                AsyncProcesser = new ThreadProcesser();
            }

            std::vector<TextureData> Textures;
            for (size_t i = 0; i < TextureboxList.size(); i++)
            {
                TextureData NewData = TextureData(i, TextureboxList[i].Height, TextureboxList[i].Width, TextureboxList[i].Data);
                Textures.push_back(NewData);
            }

            bool Success = false;
            Success = AsyncProcesser->Kick(RequestType::Generate, Textures);
            if (!Success) {
                snprintf(ProgressHintText, HINT_TEXT_SIZE, "There are stll some work handing, please wait..");
            }
            else
            {
                snprintf(ProgressHintText, HINT_TEXT_SIZE, "");
            }


        }
    }

    void OnBakeButtonClicked()
    {

    }

    float GetProgress()
    {
        if (AsyncProcesser == nullptr)
            return 0.0f;

        TextureData Result;
        float Progress = AsyncProcesser->GetResult(&Result);
        if (Result.Index != -1)
        {
            if (Result.Index > TextureboxList.size() - 1)
            {
                std::cerr << "GetResult Index get error [Index:" << Result.Index << ",Actual Size:" << TextureboxList.size() << "]" << std::endl;
                return 0.0f;
            }
            TextureboxItem& Item = TextureboxList[Result.Index];
            if (Item.SDFData != nullptr)
            {
                free(Item.SDFData);
                Item.SDFData = nullptr;
            }
            if (Item.SDFTextureID != nullptr)
            {
                Item.SDFTextureID->Release();
                Item.SDFTextureID = nullptr;
            }
            TextureboxList[Result.Index].SDFData = Result.SDFData;

            EngineTextureID* Texture = nullptr;
            if (!CreateTextureResource(Item.SDFData, Item.Width, Item.Height, 4, &(Texture))) {
                stbi_image_free(Item.SDFData);
                Item.SDFData = nullptr;
            }
            Item.SDFTextureID = Texture;
            PreviewTexture = Texture;
        }

        return Progress;
    }

private:
    void LoadTextureData(ItemIter it)
    {
        if (it->Data != NULL)
            UnLoadTextureData(it);

        const char* Filename = it->Path.c_str();
        int Width = 0;
        int Height = 0;

        unsigned char* Image_data = stbi_load(Filename, &Width, &Height, NULL, 4);
        if (Image_data == NULL)
            return ;

        EngineTextureID* Texture = NULL;
        if (!CreateTextureResource(Image_data, Width, Height, 4, &(Texture))) {
            UnLoadTextureData(it);
            return;
        }

        it->TextureID = Texture;
        it->Data = Image_data;
        it->Width = Width;
        it->Height = Height;
        it->Loaded = true;

        
    }

    void UnLoadTextureData(ItemIter it)
    {
        unsigned char* Data = it->Data;
        EngineTextureID* Texture = it->TextureID;
        unsigned char* SDFData = it->SDFData;
        EngineTextureID* SDFTexture = it->SDFTextureID;

        if (SDFTexture != NULL) {
            if (SDFTexture == PreviewTexture)
            {
                PreviewTexture = nullptr;
            }
            SDFTexture->Release();
        }
        if (SDFData != NULL)
            free(SDFData);
        if (Texture != NULL)
            Texture->Release();
        if (Data != NULL)
            stbi_image_free(Data);

        it->SDFTextureID = NULL;
        it->SDFData = NULL;
        it->TextureID = NULL;
        it->Data = NULL;
        it->Loaded = false;
        it->Width = 0;
        it->Height = 0;
    }
};

UIManager gUIManager = UIManager();






void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

HRESULT CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return E_FAIL;

    CreateRenderTarget();

    return S_OK;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int main(int, char**)
{
    // Create application window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("FacialShadowMapGernerator"), NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("Facial Shadow Map Gernerator -ver 1.0  -by XJL"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

    // Initialize Direct3D
    if (CreateDeviceD3D(hwnd) < 0)
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer bindings
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'misc/fonts/README.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("imgui/Karla-Regular.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\simfang.ttf", 18.0f);
    //IM_ASSERT(font != NULL);

    // Our state
    ImVec4 clear_color = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    float window_height = 750;
    float window_width = 700;

    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (true) {
            bool show_demo_window = true;
            ImGui::ShowDemoWindow(&show_demo_window);
        }
           

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        {
            static float f = 0.0f;
            static int counter = 0;
 
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoResize;
            window_flags |= ImGuiWindowFlags_NoCollapse;

            ImGui::SetNextWindowPos(ImVec2(4, 6), ImGuiCond_FirstUseEver);
            ImGui::Begin("Setup", NULL, window_flags);     
            //ImGui::SetWindowPos(ImVec2(10, 10));
            ImGui::SetWindowSize(ImVec2(window_width, window_height));

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::Separator();
            ImGui::Text("Please follow the next 3 step"); 
            ImGui::Separator();

            ImGui::Text("");
            ImGui::BulletText("1st Step:Add shadow map to the texture box below in the order in which the face shadow changes");
            if (ImGui::TreeNodeEx("Texture box  **DO NOT has Chinese characters in the file path**", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text(gUIManager.TextureboxStateText);
                if (ImGui::Button("Add"))
                {
                    gUIManager.OnAddButtonClicked();
                }
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("Delete"))
                {
                    gUIManager.OnDeleteButtonClicked();
                }
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("Delete All"))
                {
                    gUIManager.OnDeleteAllButtonClicked();
                }
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("Reload Selected"))
                {
                    gUIManager.OnReloadSelectedClicked();
                }
                {
                    ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
                    ImGui::BeginChild("Texture box", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.9f, window_height * 0.3f), false, window_flags);
                    char Description[128];
                    for (int i = 0; i < gUIManager.TextureboxList.size(); i++)
                    {
                        TextureboxItem& Item = gUIManager.TextureboxList[i];
                        snprintf(Description, 128, "%02d | %s | %d X %d | %s" , i, 
                            Item.Loaded ? "Loaded" : "UnLoad",
                            Item.Width, Item.Height,
                            Item.Path.c_str());
                        if(ImGui::Selectable(Description, Item.Selected))
                        {
                            if (!ImGui::GetIO().KeyCtrl)
                            {
                                for (int j = 0; j < gUIManager.TextureboxList.size(); j++)
                                    gUIManager.TextureboxList[j].Selected = false;
                            }
                            Item.Selected = true;
                        }

                    }
                    ImGui::EndChild();
                }
                ImGui::TreePop();
            }

            ImGui::Separator();
            ImGui::Text("");
            ImGui::BulletText("2nd Step:Press generate button to generate preview facial shadow\nYou can slide the slider in the preivew window to preview the change of the shadow");

            ImGui::Text("");
            ImGui::Text("");
            ImGui::SameLine(20.0, 10.0);
            if (ImGui::Button("Generate", ImVec2(100, 20)))
            {
                gUIManager.OnGenerateButtonClicked();
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), gUIManager.GenerateHintText);

            ImGui::Separator();
            ImGui::Text("");
            ImGui::BulletText("3rd Step:Press bake button to bake the final facial shadow map\nYou may adjust the parameters below as you need before you bake the final map");

            ImGui::Text("");
            ImGui::Text("");
            ImGui::SameLine(20.0, 10.0);
            ImGui::InputText("Sample Times ", gUIManager.SampleTimesText, DIGIT_INPUTTEXT_SIZE, ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine(0.0, 0.0);
            ShowHelpMarker("The higher the slower, typicallly set to 200");
            ImGui::Text("");
            ImGui::SameLine(20.0, 10.0);
            ImGui::InputText("Blur Size ", gUIManager.BlurSizeText, DIGIT_INPUTTEXT_SIZE, ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine(0.0, 0.0);
            ShowHelpMarker("The higher the slower, typicallly set to 4");
            ImGui::Text("");
            ImGui::Text("");
            ImGui::SameLine(20.0, 10.0);
            if (ImGui::Button("Bake Final", ImVec2(100, 20)))
            {
                gUIManager.OnBakeButtonClicked();
            }
            
            ImGui::Text("");
            ImGui::Separator();

            ImGui::Text("");
            ImGui::Text("Progress:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), gUIManager.ProgressHintText);

            float Progress = gUIManager.GetProgress();
            ImGui::ProgressBar(Progress, ImVec2(-1.0f, 0.0f));

            ImGui::End();
        }


        // Show preview window
        {
            ImGuiWindowFlags window_flags = 0;
            window_flags |= ImGuiWindowFlags_NoResize;
            window_flags |= ImGuiWindowFlags_NoCollapse;

            ImGui::SetNextWindowPos(ImVec2(704, 35), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(560, 680), ImGuiCond_FirstUseEver);

            ImGui::Begin("Preivew", NULL, window_flags);
            
            ImGui::BeginChild("Texture Preview", ImVec2(ImGui::GetWindowContentRegionWidth(), window_height * 0.3f), false, ImGuiWindowFlags_HorizontalScrollbar);
            int lines = 4;
            for (int i = 0; i < gUIManager.TextureboxList.size(); i++)
            {
                TextureboxItem& Item = gUIManager.TextureboxList[i];
                if (Item.Loaded)
                {
                    ImGui::Image((void*)Item.TextureID, ImVec2(128, 128));
                    if ((i+1) % lines != 0)
                        ImGui::SameLine();
                }
                
            }
            ImGui::EndChild();

            ImGui::Text("");
            ImGui::Separator();
            ImGui::Text("  ");
            ImGui::Text("  ");
            ImGui::SameLine(0.0, 125.0);
            ImGui::Image((void*)gUIManager.PreviewTexture, ImVec2(256, 256));

            ImGui::Text("  ");
            ImGui::Text("");
            ImGui::SameLine(0.0, 100.0);
            ImGui::Text("Preview slider");
            ImGui::Text("");
            ImGui::SameLine(0.0, 100.0);
            ImGui::SliderScalar("", ImGuiDataType_Float, &gUIManager.PreviewSliderValue, &gUIManager.PreviewSliderMin, &gUIManager.PreviewSliderMax);

            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync
        //g_pSwapChain->Present(0, 0); // Present without vsync
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}
