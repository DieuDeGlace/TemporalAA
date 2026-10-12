#pragma once
#include "SmaaShader.h"
#include "vendor/SMAA/AreaTex.h"
#include "vendor/SMAA/SearchTex.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>

// Official SMAA 1x/medium: edge and weight passes. Its neighborhood blend
// is fused into the existing temporal resolve; no extra color-copy pass.
class SmaaPipeline {
    ID3D11PixelShader *edgeShader = nullptr, *weightShader = nullptr;
    ID3D11Buffer *metrics = nullptr;
    ID3D11SamplerState *pointSampler = nullptr;
    ID3D11Texture2D *edges = nullptr, *weights = nullptr;
    ID3D11RenderTargetView *edgeTarget = nullptr, *weightTarget = nullptr;
    ID3D11ShaderResourceView *edgeView = nullptr, *weightView = nullptr;
    ID3D11ShaderResourceView *areaView = nullptr, *searchView = nullptr;
    UINT width = 0, height = 0;
    bool attempted = false, ready = false;
    template <class T> static void release(T *&p) {
        if (p)
            p->Release();
        p = nullptr;
    }
    void clearTargets() {
        release(edgeTarget);
        release(weightTarget);
        release(edgeView);
        release(weightView);
        release(edges);
        release(weights);
        width = height = 0;
    }
    static bool lookup(ID3D11Device *device, UINT w, UINT h, DXGI_FORMAT format, const void *data,
                       UINT pitch, ID3D11ShaderResourceView **view) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.Format = format;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{data, pitch, 0};
        ID3D11Texture2D *texture = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)))
            return false;
        HRESULT result = device->CreateShaderResourceView(texture, nullptr, view);
        texture->Release();
        return SUCCEEDED(result);
    }

  public:
    ~SmaaPipeline() {
        clearTargets();
        release(edgeShader);
        release(weightShader);
        release(metrics);
        release(pointSampler);
        release(areaView);
        release(searchView);
    }
    bool initialize(ID3D11Device *device, void (*log)(const char *)) {
        if (attempted)
            return ready;
        attempted = true;
        const char *entries = R"SMAA(
Texture2D sceneTex:register(t0);Texture2D edgesTex:register(t1);
Texture2D areaTex:register(t2);Texture2D searchTex:register(t3);
float4 edgePS(float4 p:SV_POSITION):SV_TARGET{
 float2 uv=p.xy*SMAA_RT_METRICS.xy;float4 offsets[3];SMAAEdgeDetectionVS(uv,offsets);
 return float4(SMAALumaEdgeDetectionPS(uv,offsets,sceneTex),0,0);
}
float4 weightPS(float4 p:SV_POSITION):SV_TARGET{
 float2 uv=p.xy*SMAA_RT_METRICS.xy;float2 pixel;float4 offsets[3];
 SMAABlendingWeightCalculationVS(uv,pixel,offsets);
 return SMAABlendingWeightCalculationPS(uv,pixel,offsets,edgesTex,areaTex,searchTex,0);
}
)SMAA";
        std::string source = std::string(smaaLibrary) + entries;
        for (unsigned i = 0; i < 2; ++i) {
            ID3DBlob *blob = nullptr, *errors = nullptr;
            HRESULT result = D3DCompile(source.data(), source.size(), "SMAA", nullptr, nullptr,
                                        i ? "weightPS" : "edgePS", "ps_4_0",
                                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
            if (errors) {
                log((const char *)errors->GetBufferPointer());
                release(errors);
            }
            if (FAILED(result)) {
                release(blob);
                return false;
            }
            result = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                               nullptr, i ? &weightShader : &edgeShader);
            release(blob);
            if (FAILED(result))
                return false;
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 16;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&cb, nullptr, &metrics)))
            return false;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        ready = SUCCEEDED(device->CreateSamplerState(&sampler, &pointSampler)) &&
                lookup(device, AREATEX_WIDTH, AREATEX_HEIGHT, DXGI_FORMAT_R8G8_UNORM, areaTexBytes,
                       AREATEX_PITCH, &areaView) &&
                lookup(device, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, DXGI_FORMAT_R8_UNORM,
                       searchTexBytes, SEARCHTEX_PITCH, &searchView);
        if (ready)
            log("SMAA medium resources ready; neighborhood blending fused with TAA.");
        return ready;
    }
    bool prepare(ID3D11Device *device, ID3D11DeviceContext *context, UINT w, UINT h,
                 ID3D11ShaderResourceView *scene, ID3D11SamplerState *linear,
                 void (*draw)(ID3D11DeviceContext *, UINT, UINT), void (*log)(const char *)) {
        if (!initialize(device, log))
            return false;
        if (width != w || height != h) {
            clearTargets();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = w;
            desc.Height = h;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            bool ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &edges)) &&
                      SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &weights)) &&
                      SUCCEEDED(device->CreateRenderTargetView(edges, nullptr, &edgeTarget)) &&
                      SUCCEEDED(device->CreateRenderTargetView(weights, nullptr, &weightTarget)) &&
                      SUCCEEDED(device->CreateShaderResourceView(edges, nullptr, &edgeView)) &&
                      SUCCEEDED(device->CreateShaderResourceView(weights, nullptr, &weightView));
            if (!ok) {
                clearTargets();
                return false;
            }
            width = w;
            height = h;
        }
        float dimensions[] = {1.f / w, 1.f / h, (float)w, (float)h};
        context->UpdateSubresource(metrics, 0, nullptr, dimensions, 0, 0);
        context->PSSetConstantBuffers(1, 1, &metrics);
        ID3D11SamplerState *samplers[] = {linear, pointSampler};
        context->PSSetSamplers(0, 2, samplers);
        float zero[4]{};
        context->ClearRenderTargetView(edgeTarget, zero);
        context->OMSetRenderTargets(1, &edgeTarget, nullptr);
        context->PSSetShaderResources(0, 1, &scene);
        context->PSSetShader(edgeShader, nullptr, 0);
        draw(context, 3, 0);
        ID3D11ShaderResourceView *empty[4]{};
        context->PSSetShaderResources(0, 4, empty);
        context->ClearRenderTargetView(weightTarget, zero);
        context->OMSetRenderTargets(1, &weightTarget, nullptr);
        ID3D11ShaderResourceView *inputs[] = {nullptr, edgeView, areaView, searchView};
        context->PSSetShaderResources(0, 4, inputs);
        context->PSSetShader(weightShader, nullptr, 0);
        draw(context, 3, 0);
        context->PSSetShaderResources(0, 4, empty);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        return true;
    }
    void bindResolve(ID3D11DeviceContext *context, ID3D11SamplerState *linear) {
        context->PSSetConstantBuffers(1, 1, &metrics);
        ID3D11SamplerState *samplers[] = {linear, pointSampler};
        context->PSSetSamplers(0, 2, samplers);
        context->PSSetShaderResources(4, 1, &weightView);
    }
};
