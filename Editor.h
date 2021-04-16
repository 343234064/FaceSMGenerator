#pragma once

#include "imgui/imgui.h"
#include "ThreadProcess.h"

#include <ShObjIdl_core.h>
#include <stdio.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>



#include <d3d11.h>


typedef ID3D11ShaderResourceView EngineTextureID;
typedef ID3D11Device EngineDevice;

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





class PreviewTextureRenderer
{
#define PREVIEW_TEXTURE_SIZE 256

    struct float2
    {
        float2(float a = 0.0f, float b = 0.0f) : x(a), y(b) {}
        float x, y;
    };
    struct float3 : public float2
    {
        float3(float a = 0.0f, float b = 0.0f, float c = 0.0f) : float2(a, b), z(c) {}
        float z;
    };
    struct VertexType
    {
        VertexType() {}
        float3 position;
        float2 uv;
    };


public:
    PreviewTextureRenderer() :
        RenderTargetTexture(nullptr),
        RenderTargetView(nullptr),
        RenderTargetShaderResourceView(nullptr),
        VertexBuffer(nullptr),
        IndexBuffer(nullptr),
        VSInputLayout(nullptr),
        VSConstantBuffer(nullptr),
        VertexShader(nullptr),
        VertexShaderBlob(nullptr),
        PSConstantBuffer(nullptr),
        PixelShader(nullptr),
        PixelShaderBlob(nullptr),
        TextureSampler(nullptr),
        RasterizerState(nullptr),
        BlendState(nullptr),
        DepthStencilState(nullptr),
        Initialized(false),
        Threshold(0.5f),
        StartThreshold(0.0f),
        EndThreshold(1.0f)
    {
        TextureViewsToRender[0] = nullptr;
        TextureViewsToRender[1] = nullptr;
    }
    ~PreviewTextureRenderer()
    {
        Uninitialize();
    }

    __forceinline EngineTextureID* GetPreviewTexture() { return RenderTargetShaderResourceView; }

    bool Initialize(ID3D11Device* pd3dDevice)
    {
        if (Initialized) return true;

        if (!SetupRenderTarget(pd3dDevice)) {
            std::cerr << "Preview Renderer: Render target init failed" << std::endl;
            return false;
        }
        if (!SetupVertexAndIndexBuffer(pd3dDevice)) {
            std::cerr << "Preview Renderer: Vertex and index buffer init failed" << std::endl;
            return false;
        }
        if (!SetupRenderState(pd3dDevice)) {
            std::cerr << "Preview Renderer: Render state init failed" << std::endl;
            return false;
        }
        if (!SetupShaderResources(pd3dDevice)) {
            std::cerr << "Preview Renderer: Shader resources init failed" << std::endl;
            return false;
        }
        
        std::cout << "Preview renderer init finished" << std::endl;

        TextureViewsToRender[0] = nullptr;
        TextureViewsToRender[1] = nullptr;
        TextureViews.clear();
        Threshold = 0.5f;

        StartThreshold = 0.0f;
        EndThreshold = 1.0f;

        Initialized = true;
        return true;
    }

    void Uninitialize()
    {
        if (VSConstantBuffer != nullptr) { VSConstantBuffer->Release(); VSConstantBuffer = nullptr;}
        if (VSInputLayout != nullptr) { VSInputLayout->Release(); VSInputLayout = nullptr; }
        if (VertexShader != nullptr) { VertexShader->Release(); VertexShader = nullptr; }
        if (VertexShaderBlob != nullptr) { VertexShaderBlob->Release(); VertexShaderBlob = nullptr; }

        if (TextureSampler != nullptr) { TextureSampler->Release(); TextureSampler = nullptr; }

        if (PSConstantBuffer != nullptr) { PSConstantBuffer->Release(); PSConstantBuffer = nullptr; }
        if (PixelShader != nullptr) { PixelShader->Release(); PixelShader = nullptr; }
        if (PixelShaderBlob != nullptr) { PixelShaderBlob->Release(); PixelShaderBlob = nullptr; }

        if (RasterizerState != nullptr) { RasterizerState->Release(); RasterizerState = nullptr; }
        if (BlendState != nullptr) { BlendState->Release(); BlendState = nullptr; }
        if (DepthStencilState != nullptr) { DepthStencilState->Release(); DepthStencilState = nullptr; }

        if (VertexBuffer != nullptr) { VertexBuffer->Release(); VertexBuffer = nullptr; }
        if (IndexBuffer != nullptr) { IndexBuffer->Release(); IndexBuffer = nullptr; }

        if (RenderTargetShaderResourceView != nullptr) { RenderTargetShaderResourceView->Release(); RenderTargetShaderResourceView = nullptr; }
        if (RenderTargetView != nullptr) { RenderTargetView->Release(); RenderTargetView = nullptr; }
        if (RenderTargetTexture != nullptr) { RenderTargetTexture->Release(); RenderTargetTexture = nullptr; }

        TextureViewsToRender[0] = nullptr;
        TextureViewsToRender[1] = nullptr;
        TextureViews.clear();

        Threshold = 0.5f;

        StartThreshold = 0.0f;
        EndThreshold = 1.0f;

        Initialized = false;
    }

    void Render(ID3D11DeviceContext* deviceContext);

    void SetTexture(ID3D11ShaderResourceView* TextureID) {
        if (TextureID) {
            TextureViews.push_back(TextureID);

        }
    }
    void ClearTexture() 
    { 
        TextureViews.clear(); 
    }
    void SetThreshold(float Value) { Threshold = Value; }
    bool IsInit() { return Initialized; }

private:
    bool SetupRenderTarget(ID3D11Device* pd3dDevice);
    bool SetupVertexAndIndexBuffer(ID3D11Device* pd3dDevice);
    bool SetupRenderState(ID3D11Device* pd3dDevice);
    bool SetupShaderResources(ID3D11Device* pd3dDevice);
  
    void CalculateCurrentSampleTextureSlot();

private:
    ID3D11Texture2D* RenderTargetTexture;
    ID3D11RenderTargetView* RenderTargetView;
    ID3D11ShaderResourceView* RenderTargetShaderResourceView;

    ID3D11Buffer* VertexBuffer;
    ID3D11Buffer* IndexBuffer;

    //Shader
    ID3D11InputLayout* VSInputLayout;
    ID3D11Buffer* VSConstantBuffer;
    ID3D11VertexShader* VertexShader;
    ID3D10Blob* VertexShaderBlob;

    ID3D11SamplerState* TextureSampler;
    ID3D11Buffer* PSConstantBuffer;
    ID3D11PixelShader* PixelShader;
    ID3D10Blob* PixelShaderBlob;

    //States
    D3D11_VIEWPORT Viewport;
    ID3D11RasterizerState* RasterizerState;
    ID3D11BlendState* BlendState;
    ID3D11DepthStencilState* DepthStencilState;

    //External
    std::vector<ID3D11ShaderResourceView*> TextureViews;
    ID3D11ShaderResourceView* TextureViewsToRender[2];

    bool Initialized;

    float Threshold;

    //For textures swap
    float StartThreshold;
    float EndThreshold;
};



class Editor
{
#define TEXTUREBOX_STATE_TEXT_SIZE 32
#define HINT_TEXT_SIZE 128
#define FILE_NAME_SIZE 64

typedef std::vector<TextureboxItem>::iterator ItemIter;

public:
    std::vector<TextureboxItem> TextureboxList;

    int SampleTimes = 2000;
    int BlurSize = 1;
    char OutputFileNameText[FILE_NAME_SIZE] = "map_output.png";
    char TextureboxStateText[TEXTUREBOX_STATE_TEXT_SIZE] = "";
    char GenerateHintText[HINT_TEXT_SIZE] = "";
    char ProgressHintText[HINT_TEXT_SIZE] = "";
    char BakeHintText[HINT_TEXT_SIZE] = "";

    const float PreviewSliderMin = 0.0f;
    const float PreviewSliderMax = 1.0f;
    float PreviewSliderValue = 0.5f;

    PreviewTextureRenderer PreviewRenderer;
    
    EngineTextureID* ResultImage = nullptr;
private:
    ThreadProcesser* AsyncProcesser = nullptr;

    bool PreviewDirty = false;
    bool Generated = false;
    bool Baked = false;

public:
    void SetEngineDevice(EngineDevice* device) { Device = device; }

private:
    /*External*/
    EngineDevice* Device = nullptr;


public:
    Editor() {}
    ~Editor()
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

        if (ResultImage != nullptr) {
            ResultImage->Release();
            ResultImage = nullptr;
        }
    }

    void OnAddButtonClicked();
    void OnDeleteButtonClicked();
    void OnDeleteAllButtonClicked();
    void OnReloadSelectedClicked();
    void OnGenerateButtonClicked();
    void OnBakeButtonClicked();
    double GetProgress();



private:
    void LoadTextureData(ItemIter it);
    void UnLoadTextureData(ItemIter it);

};



void RenderEditorUI(Editor& editor);


