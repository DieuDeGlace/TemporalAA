#include "../CameraReprojection.h"
#include <cassert>
#include <cstdio>
int main() {
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &device, nullptr, &context)));
    reprojection::CameraHistory camera;
    for (UINT samples : {2u, 4u}) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 32;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.SampleDesc.Count = samples;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D *depth = nullptr;
        ID3D11DepthStencilView *view = nullptr;
        assert(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &depth)));
        assert(SUCCEEDED(device->CreateDepthStencilView(depth, nullptr, &view)));
        context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, .75f, 0);
        const char *shader = R"DEPTH(
float4 vs(uint id:SV_VertexID):SV_POSITION{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}
float ps(float4 p:SV_POSITION,uint sample:SV_SampleIndex):SV_DEPTH{return sample==0?.25:.75;}
)DEPTH";
        ID3D11VertexShader *vs = nullptr;
        ID3D11PixelShader *ps = nullptr;
        ID3DBlob *blob = nullptr;
        assert(SUCCEEDED(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "vs",
                                    "vs_5_0", 0, 0, &blob, nullptr)));
        assert(SUCCEEDED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                    nullptr, &vs)));
        blob->Release();
        blob = nullptr;
        assert(SUCCEEDED(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "ps",
                                    "ps_5_0", 0, 0, &blob, nullptr)));
        assert(SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                   nullptr, &ps)));
        blob->Release();
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
        ID3D11DepthStencilState *state = nullptr;
        assert(SUCCEEDED(device->CreateDepthStencilState(&ds, &state)));
        D3D11_VIEWPORT viewport{0, 0, 32, 32, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(0, nullptr, view);
        context->OMSetDepthStencilState(state, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs, nullptr, 0);
        context->PSSetShader(ps, nullptr, 0);
        context->Draw(3, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        float identity[16]{};
        for (int i : {0, 5, 10, 15})
            identity[i] = 1;
        reprojection::Parameters parameters{};
        assert(camera.prepare(device, context, view, 32, 32, identity, parameters));
        assert(parameters.options[1] == 1);
        camera.bind(context);
        ID3D11ShaderResourceView *resolved = nullptr;
        context->PSGetShaderResources(2, 1, &resolved);
        ID3D11Resource *resource = nullptr;
        resolved->GetResource(&resource);
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D *readback = nullptr;
        assert(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)));
        context->CopyResource(readback, resource);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        assert(SUCCEEDED(context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped)));
        float value = *(float *)((char *)mapped.pData + 16 * mapped.RowPitch + 16 * sizeof(float));
        assert(fabsf(value - .25f) < .00001f);
        context->Unmap(readback, 0);
        ID3D11ShaderResourceView *empty[2]{};
        context->PSSetShaderResources(2, 2, empty);
        camera.commit(context);
        resource->Release();
        resolved->Release();
        readback->Release();
        state->Release();
        vs->Release();
        ps->Release();
        view->Release();
        depth->Release();
        printf("%ux MSAA depth: nearest sample %.5f, reprojection enabled PASS\n", samples, value);
    }
    context->ClearState();
    context->Release();
    device->Release();
}
