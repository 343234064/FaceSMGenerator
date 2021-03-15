#include "Editor.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb-master/stb_image.h"

#include <d3dcompiler.h>

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




void RenderEditorUI(Editor& UIEditor, PreviewTextureRenderer& PreviewRenderer)
{
    float window_height = 750;
    float window_width = 700;
    // Show setting window
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
        if (ImGui::TreeNodeEx("Texture box  **DO NOT has Chinese characters in file path**", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text(UIEditor.TextureboxStateText);
            if (ImGui::Button("Add"))
            {
                UIEditor.OnAddButtonClicked();
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("Delete"))
            {
                UIEditor.OnDeleteButtonClicked();
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("Delete All"))
            {
                UIEditor.OnDeleteAllButtonClicked();
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("Reload Selected"))
            {
                UIEditor.OnReloadSelectedClicked();
            }
            {
                ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
                ImGui::BeginChild("Texture box", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.9f, window_height * 0.3f), false, window_flags);
                char Description[128];
                for (int i = 0; i < UIEditor.TextureboxList.size(); i++)
                {
                    TextureboxItem& Item = UIEditor.TextureboxList[i];
                    snprintf(Description, 128, "%02d | %s | %d X %d | %s", i,
                        Item.Loaded ? "Loaded" : "UnLoad",
                        Item.Width, Item.Height,
                        Item.Path.c_str());
                    if (ImGui::Selectable(Description, Item.Selected))
                    {
                        if (!ImGui::GetIO().KeyCtrl)
                        {
                            for (int j = 0; j < UIEditor.TextureboxList.size(); j++)
                                UIEditor.TextureboxList[j].Selected = false;
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
            UIEditor.OnGenerateButtonClicked();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), UIEditor.GenerateHintText);

        ImGui::Separator();
        ImGui::Text("");
        ImGui::BulletText("3rd Step:Press bake button to bake the final facial shadow map\nYou may adjust the parameters below as you need before you bake the final map");

        ImGui::Text("");
        ImGui::Text("");
        ImGui::SameLine(20.0, 10.0);
        ImGui::InputText("Sample Times ", UIEditor.SampleTimesText, DIGIT_INPUTTEXT_SIZE, ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine(0.0, 0.0);
        ShowHelpMarker("The higher the slower, typicallly set to 200");
        ImGui::Text("");
        ImGui::SameLine(20.0, 10.0);
        ImGui::InputText("Blur Size ", UIEditor.BlurSizeText, DIGIT_INPUTTEXT_SIZE, ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine(0.0, 0.0);
        ShowHelpMarker("The higher the slower, typicallly set to 4");
        ImGui::Text("");
        ImGui::Text("");
        ImGui::SameLine(20.0, 10.0);
        if (ImGui::Button("Bake Final", ImVec2(100, 20)))
        {
            UIEditor.OnBakeButtonClicked();
        }

        ImGui::Text("");
        ImGui::Separator();

        ImGui::Text("");
        ImGui::Text("Progress:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0, 0, 1), UIEditor.ProgressHintText);

        float Progress = UIEditor.GetProgress();
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
        for (int i = 0; i < UIEditor.TextureboxList.size(); i++)
        {
            TextureboxItem& Item = UIEditor.TextureboxList[i];
            if (Item.Loaded)
            {
                ImGui::Image((void*)Item.TextureID, ImVec2(128, 128));
                if ((i + 1) % lines != 0)
                    ImGui::SameLine();
            }

        }
        ImGui::EndChild();

        ImGui::Text("");
        ImGui::Separator();
        ImGui::Text("  ");
        ImGui::Text("  ");
        ImGui::SameLine(0.0, 125.0);
        ImGui::Image((void*)PreviewRenderer.GetPreviewTexture(), ImVec2(PREVIEW_TEXTURE_SIZE, PREVIEW_TEXTURE_SIZE));

        ImGui::Text("  ");
        ImGui::Text("");
        ImGui::SameLine(0.0, 100.0);
        ImGui::Text("Preview slider");
        ImGui::Text("");
        ImGui::SameLine(0.0, 100.0);
        ImGui::SliderScalar("", ImGuiDataType_Float, &UIEditor.PreviewSliderValue, &UIEditor.PreviewSliderMin, &UIEditor.PreviewSliderMax);

        ImGui::End();
    }
}


bool Editor::CreateEngineTextureResource(unsigned char* ImageData, int ImageWidth, int ImageHeight, int channel, EngineTextureID** OutResource)
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
    hr = Device->CreateTexture2D(&desc, &subResource, &pTexture);
    if (!SUCCEEDED(hr))
        return false;

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    hr = Device->CreateShaderResourceView(pTexture, &srvDesc, OutResource);
    if (!SUCCEEDED(hr))
        return false;

    pTexture->Release();

    return true;
}



void Editor::OnAddButtonClicked()
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


void Editor::OnDeleteButtonClicked()
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


void Editor::OnDeleteAllButtonClicked()
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


void Editor::OnReloadSelectedClicked()
{

}


void Editor::OnGenerateButtonClicked()
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



void Editor::OnBakeButtonClicked()
{

}



float Editor::GetProgress()
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
        if (!CreateEngineTextureResource(Item.SDFData, Item.Width, Item.Height, 4, &(Texture))) {
            stbi_image_free(Item.SDFData);
            Item.SDFData = nullptr;
        }
        Item.SDFTextureID = Texture;
        PreviewTexture = Texture;
    }

    return Progress;
}




void Editor::LoadTextureData(ItemIter it)
{
    if (it->Data != NULL)
        UnLoadTextureData(it);

    const char* Filename = it->Path.c_str();
    int Width = 0;
    int Height = 0;

    unsigned char* Image_data = stbi_load(Filename, &Width, &Height, NULL, 4);
    if (Image_data == NULL)
        return;

    EngineTextureID* Texture = NULL;
    if (!CreateEngineTextureResource(Image_data, Width, Height, 4, &(Texture))) {
        UnLoadTextureData(it);
        return;
    }

    it->TextureID = Texture;
    it->Data = Image_data;
    it->Width = Width;
    it->Height = Height;
    it->Loaded = true;


}

void Editor::UnLoadTextureData(ItemIter it)
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







bool PreviewTextureRenderer::SetupRenderTarget(ID3D11Device* pd3dDevice)
{
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));

    desc.Width = PREVIEW_TEXTURE_SIZE;
    desc.Height = PREVIEW_TEXTURE_SIZE;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    if (FAILED(pd3dDevice->CreateTexture2D(&desc, NULL, &RenderTargetTexture)))
    {
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
    renderTargetViewDesc.Format = desc.Format;
    renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    renderTargetViewDesc.Texture2D.MipSlice = 0;

    if (FAILED(pd3dDevice->CreateRenderTargetView(RenderTargetTexture, &renderTargetViewDesc, &RenderTargetView)))
    {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
    ZeroMemory(&shaderResourceViewDesc, sizeof(shaderResourceViewDesc));
    shaderResourceViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shaderResourceViewDesc.Texture2D.MipLevels = desc.MipLevels;
    shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
    if (FAILED(pd3dDevice->CreateShaderResourceView(RenderTargetTexture, &shaderResourceViewDesc, &RenderTargetShaderResourceView)))
    {
        return false;
    }

    return true;
}

bool PreviewTextureRenderer::SetupVertexAndIndexBuffer(ID3D11Device* pd3dDevice)
{
    VertexType* vertices;
    unsigned long* indices;

    // two triangle
    int vertexCount = 6;
    int indexCount = vertexCount;

    vertices = new VertexType[vertexCount];
    indices = new unsigned long[indexCount];

    // First triangle.
    vertices[0].position = float3(0.0f, 1.0f, 0.0f);// Top left.
    vertices[0].uv = float2(0.0f, 0.0f);

    vertices[1].position = float3(1.0, 0.0f, 0.0f);  // Bottom right.
    vertices[1].uv = float2(1.0f, 1.0f);

    vertices[2].position = float3(0.0f, 0.0f, 0.0f);  // Bottom left.
    vertices[2].uv = float2(0.0f, 1.0f);

    // Second triangle.
    vertices[3].position = float3(0.0f, 1.0f, 0.0f);  // Top left.
    vertices[3].uv = float2(0.0f, 0.0f);

    vertices[4].position = float3(1.0f, 1.0f, 0.0f);  // Top right.
    vertices[4].uv = float2(1.0f, 0.0f);

    vertices[5].position = float3(1.0f, 0.0f, 0.0f);  // Bottom right.
    vertices[5].uv = float2(1.0f, 1.0f);


    for (int i = 0; i < indexCount; i++)
    {
        indices[i] = i;
    }

    D3D11_BUFFER_DESC vertexBufferDesc;
    vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    vertexBufferDesc.ByteWidth = sizeof(VertexType) * vertexCount;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    vertexBufferDesc.MiscFlags = 0;
    vertexBufferDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA vertexData;
    vertexData.pSysMem = vertices;
    vertexData.SysMemPitch = 0;
    vertexData.SysMemSlicePitch = 0;

    if (FAILED(pd3dDevice->CreateBuffer(&vertexBufferDesc, &vertexData, &VertexBuffer)))
    {
        return false;
    }

    D3D11_BUFFER_DESC indexBufferDesc;
    indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    indexBufferDesc.ByteWidth = sizeof(unsigned long) * indexCount;
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    indexBufferDesc.CPUAccessFlags = 0;
    indexBufferDesc.MiscFlags = 0;
    indexBufferDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA indexData;
    indexData.pSysMem = indices;
    indexData.SysMemPitch = 0;
    indexData.SysMemSlicePitch = 0;

    if (FAILED(pd3dDevice->CreateBuffer(&indexBufferDesc, &indexData, &IndexBuffer)))
    {
        return false;
    }

    delete[] vertices;
    delete[] indices;

    return true;
}

bool PreviewTextureRenderer::SetupRenderState(ID3D11Device* pd3dDevice)
{
    Viewport.Height = 256;
    Viewport.Width = 256;
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    Viewport.TopLeftX = 0.0f;
    Viewport.TopLeftY = 0.0f;

    D3D11_RASTERIZER_DESC RDesc;
    ZeroMemory(&RDesc, sizeof(RDesc));
    RDesc.FillMode = D3D11_FILL_SOLID;
    RDesc.CullMode = D3D11_CULL_NONE;
    RDesc.ScissorEnable = true;
    RDesc.DepthClipEnable = true;
    if (FAILED(pd3dDevice->CreateRasterizerState(&RDesc, &RasterizerState)))
    {
        return false;
    }

    D3D11_BLEND_DESC BDesc;
    ZeroMemory(&BDesc, sizeof(BDesc));
    BDesc.AlphaToCoverageEnable = false;
    BDesc.RenderTarget[0].BlendEnable = false;
    BDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(pd3dDevice->CreateBlendState(&BDesc, &BlendState)))
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC DDesc;
    ZeroMemory(&DDesc, sizeof(DDesc));
    DDesc.DepthEnable = false;
    DDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    DDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    DDesc.StencilEnable = false;
    DDesc.FrontFace.StencilFailOp = DDesc.FrontFace.StencilDepthFailOp = DDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    DDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    DDesc.BackFace = DDesc.FrontFace;
    if (FAILED(pd3dDevice->CreateDepthStencilState(&DDesc, &DepthStencilState)))
    {
        return false;
    }


    return true;
}


static const char* VertexShaderCode =
            "cbuffer vertexBuffer : register(b0) \
            {\
            float4x4 ProjectionMatrix; \
            };\
            struct VS_INPUT\
            {\
            float2 pos : POSITION;\
            float2 uv  : TEXCOORD0;\
            };\
            \
            struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float2 uv  : TEXCOORD0;\
            };\
            \
            PS_INPUT main(VS_INPUT input)\
            {\
            PS_INPUT output;\
            output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
            output.uv  = input.uv;\
            return output;\
            }";


static const char* PixelShaderCode =
            "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float2 uv  : TEXCOORD0;\
            };\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = float4(0.0f, 0.5f, 0.5f, 1.0f); \
            return out_col; \
            }";

struct VERTEX_CONSTANT_BUFFER
{
    float   mvp[4][4];
};

bool PreviewTextureRenderer::SetupShaderResources(ID3D11Device* pd3dDevice)
{
    ID3D10Blob* VSErrorBlob;
    if (FAILED(D3DCompile(VertexShaderCode, strlen(VertexShaderCode), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &VertexShaderBlob, &VSErrorBlob))) {
        std::cerr << (const char*)VSErrorBlob->GetBufferPointer() << std::endl;
        VSErrorBlob->Release();
        return false;
    }

    if (FAILED(pd3dDevice->CreateVertexShader((DWORD*)VertexShaderBlob->GetBufferPointer(), VertexShaderBlob->GetBufferSize(), NULL, &VertexShader)))
        return false;

    D3D11_INPUT_ELEMENT_DESC LocalLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 12,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(pd3dDevice->CreateInputLayout(LocalLayout, 2, VertexShaderBlob->GetBufferPointer(), VertexShaderBlob->GetBufferSize(), &VSInputLayout)))
        return false;

    D3D11_BUFFER_DESC CDesc;
    CDesc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER);
    CDesc.Usage = D3D11_USAGE_DYNAMIC;
    CDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    CDesc.MiscFlags = 0;

    if (FAILED(pd3dDevice->CreateBuffer(&CDesc, NULL, &VSConstantBuffer)))
        return false;

    ID3D10Blob* PSErrorBlob;
    if (FAILED(D3DCompile(PixelShaderCode, strlen(PixelShaderCode), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &PixelShaderBlob, &PSErrorBlob))) {
        std::cerr << (const char*)PSErrorBlob->GetBufferPointer() << std::endl;
        PSErrorBlob->Release();
        return false;
    }

    if (FAILED(pd3dDevice->CreatePixelShader((DWORD*)PixelShaderBlob->GetBufferPointer(), PixelShaderBlob->GetBufferSize(), NULL, &PixelShader)))
        return false;


    return true;
}


void PreviewTextureRenderer::Render(ID3D11DeviceContext* deviceContext)
{
    const float ClearColor[4] = { 0.5f, 0.0f, 0.0f, 1.0f };
    const float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    unsigned int Stride = sizeof(VertexType);
    unsigned int Offset = 0;

    {
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        if (deviceContext->Map(VSConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource) != S_OK)
            return;
        VERTEX_CONSTANT_BUFFER* constant_buffer = (VERTEX_CONSTANT_BUFFER*)mapped_resource.pData;
        float L = 0.0f;
        float R = 1.0f;
        float T = 0.0f;
        float B = 1.0f;
        float mvp[4][4] =
        {
            { 2.0f / (R - L),   0.0f,           0.0f,       0.0f },
            { 0.0f,         2.0f / (T - B),     0.0f,       0.0f },
            { 0.0f,         0.0f,           0.5f,       0.0f },
            { (R + L) / (L - R),  (T + B) / (B - T),    0.5f,       1.0f },
        };
        memcpy(&constant_buffer->mvp, mvp, sizeof(mvp));
        deviceContext->Unmap(VSConstantBuffer, 0);
    }

    deviceContext->IASetInputLayout(VSInputLayout);
    deviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
    deviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    deviceContext->VSSetShader(VertexShader, NULL, 0);
    deviceContext->VSSetConstantBuffers(0, 1, &VSConstantBuffer);

    deviceContext->RSSetViewports(1, &Viewport);
    deviceContext->RSSetState(RasterizerState);

    const D3D11_RECT r = { 0, 0, PREVIEW_TEXTURE_SIZE, PREVIEW_TEXTURE_SIZE };
    deviceContext->RSSetScissorRects(1, &r);

    deviceContext->PSSetShader(PixelShader, NULL, 0);
    //deviceContext->PSSetSamplers(0, 2, TextureSampler);

    deviceContext->OMSetRenderTargets(1, &RenderTargetView, NULL);
    deviceContext->OMSetBlendState(BlendState, BlendFactor, 0xffffffff);
    deviceContext->OMSetDepthStencilState(DepthStencilState, 0);
    deviceContext->ClearRenderTargetView(RenderTargetView, ClearColor);

    //deviceContext->DrawAuto();
    deviceContext->DrawIndexed(6, 0, 0);

    return;
}