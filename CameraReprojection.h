#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace reprojection {
inline void multiply(const float *a, const float *b, float *out) {
    float result[16]{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k)
                result[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
    memcpy(out, result, sizeof(result));
}
inline bool inverse(const float *matrix, float *out) {
    double rows[4][8]{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c)
            rows[r][c] = matrix[r * 4 + c];
        rows[r][r + 4] = 1;
    }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::abs(rows[r][c]) > std::abs(rows[pivot][c]))
                pivot = r;
        if (!std::isfinite(rows[pivot][c]) || std::abs(rows[pivot][c]) < 1e-12)
            return false;
        for (int k = 0; k < 8; ++k)
            std::swap(rows[c][k], rows[pivot][k]);
        double divisor = rows[c][c];
        for (int k = 0; k < 8; ++k)
            rows[c][k] /= divisor;
        for (int r = 0; r < 4; ++r) {
            if (r == c)
                continue;
            double factor = rows[r][c];
            for (int k = 0; k < 8; ++k)
                rows[r][k] -= factor * rows[c][k];
        }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double value = rows[r][c + 4];
            if (!std::isfinite(value))
                return false;
            out[r * 4 + c] = (float)value;
        }
    return true;
}
struct Parameters {
    float resolve[4]{};
    float options[4]{}; // Blur, camera reprojection enabled, depth threshold, spare.
    float inverseCurrent[16]{};
    float previous[16]{};
    float previousJitter[4]{};
};
static_assert(sizeof(Parameters) == 176);
class CameraHistory {
    ID3D11Texture2D *currentDepth = nullptr, *previousDepth = nullptr;
    ID3D11ShaderResourceView *currentView = nullptr, *previousView = nullptr;
    ID3D11Texture2D *multisampledDepth = nullptr;
    ID3D11ShaderResourceView *multisampledView = nullptr;
    ID3D11UnorderedAccessView *resolvedUav = nullptr;
    ID3D11ComputeShader *depthResolve = nullptr;
    UINT width = 0, height = 0, sampleCount = 0, sampleQuality = 0;
    DXGI_FORMAT family = DXGI_FORMAT_UNKNOWN;
    bool valid = false;
    const char *statusText = "not prepared";
    float previousMatrix[16]{}, currentMatrix[16]{};
    float previousJitter[2]{}, currentJitter[2]{};
    template <class T> static void release(T *&p) {
        if (p)
            p->Release();
        p = nullptr;
    }
    void clear() {
        release(multisampledView);
        release(resolvedUav);
        release(multisampledDepth);
        release(currentView);
        release(previousView);
        release(currentDepth);
        release(previousDepth);
        width = height = sampleCount = sampleQuality = 0;
        family = DXGI_FORMAT_UNKNOWN;
        valid = false;
    }

  public:
    ~CameraHistory() {
        clear();
        release(depthResolve);
    }
    const char *status() const {
        return statusText;
    }
    void invalidate() {
        valid = false;
    }
    bool prepare(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11DepthStencilView *depth,
                 UINT w, UINT h, const float *matrix, Parameters &parameters) {
        if (!depth || !inverse(matrix, parameters.inverseCurrent)) {
            statusText = !depth ? "missing scene depth" : "singular camera transform";
            invalidate();
            return false;
        }
        ID3D11Resource *resource = nullptr;
        ID3D11Texture2D *source = nullptr;
        depth->GetResource(&resource);
        HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&source);
        release(resource);
        if (FAILED(hr)) {
            statusText = "depth is not a 2D texture";
            invalidate();
            return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        DXGI_FORMAT typed = DXGI_FORMAT_UNKNOWN, typeless = DXGI_FORMAT_UNKNOWN;
        switch (desc.Format) {
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
            typeless = DXGI_FORMAT_R32G8X24_TYPELESS;
            typed = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            break;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
            typeless = DXGI_FORMAT_R24G8_TYPELESS;
            typed = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            break;
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
            typeless = DXGI_FORMAT_R32_TYPELESS;
            typed = DXGI_FORMAT_R32_FLOAT;
            break;
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_TYPELESS:
            typeless = DXGI_FORMAT_R16_TYPELESS;
            typed = DXGI_FORMAT_R16_UNORM;
            break;
        default:
            break;
        }
        if (typed == DXGI_FORMAT_UNKNOWN || desc.Width != w || desc.Height != h ||
            desc.ArraySize != 1 || desc.MipLevels != 1) {
            statusText = typed == DXGI_FORMAT_UNKNOWN ? "unsupported depth format"
                                                      : "depth dimensions/layout differ from scene";
            release(source);
            invalidate();
            return false;
        }
        UINT samples = desc.SampleDesc.Count, quality = desc.SampleDesc.Quality;
        bool multisampled = samples > 1;
        if (!currentDepth || width != w || height != h || family != typeless ||
            sampleCount != samples || sampleQuality != quality) {
            clear();
            desc.Format = typeless;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = desc.MiscFlags = 0;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = typed;
            bool ok = true;
            if (multisampled) {
                view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &multisampledDepth)) &&
                     SUCCEEDED(device->CreateShaderResourceView(multisampledDepth, &view,
                                                                &multisampledView));
                desc.SampleDesc = {1, 0};
                desc.Format = DXGI_FORMAT_R32_FLOAT;
                desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
                view.Format = DXGI_FORMAT_R32_FLOAT;
            }
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            ok = ok && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &currentDepth)) &&
                 SUCCEEDED(device->CreateShaderResourceView(currentDepth, &view, &currentView));
            if (multisampled)
                ok = ok && SUCCEEDED(device->CreateUnorderedAccessView(currentDepth, nullptr,
                                                                       &resolvedUav));
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ok = ok && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &previousDepth)) &&
                 SUCCEEDED(device->CreateShaderResourceView(previousDepth, &view, &previousView));
            if (!ok) {
                statusText = "depth texture/view allocation failed";
                clear();
                release(source);
                return false;
            }
            width = w;
            height = h;
            family = typeless;
            sampleCount = samples;
            sampleQuality = quality;
        }
        // Copies and multisample resolution stay on the GPU. D3D11 cannot
        // resolve a depth/stencil texture with ResolveSubresource.
        if (multisampled) {
            if (!depthResolve) {
                const char *shader = R"DEPTH(
Texture2DMS<float> sourceDepth:register(t0);RWTexture2D<float> outputDepth:register(u0);
[numthreads(8,8,1)]void main(uint3 pixel:SV_DispatchThreadID){
 uint w,h,n;sourceDepth.GetDimensions(w,h,n);if(pixel.x>=w||pixel.y>=h)return;
 float nearest=1;[loop]for(uint i=0;i<n;i++)nearest=min(nearest,sourceDepth.Load(pixel.xy,i));
 outputDepth[pixel.xy]=nearest;
})DEPTH";
                ID3DBlob *blob = nullptr, *errors = nullptr;
                HRESULT result =
                    D3DCompile(shader, strlen(shader), "DepthResolve", nullptr, nullptr, "main",
                               "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
                release(errors);
                if (SUCCEEDED(result))
                    result = device->CreateComputeShader(
                        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &depthResolve);
                release(blob);
                if (FAILED(result)) {
                    statusText = "multisample depth resolve shader unavailable";
                    release(source);
                    invalidate();
                    return false;
                }
            }
            context->CopyResource(multisampledDepth, source);
            context->CSSetShader(depthResolve, nullptr, 0);
            context->CSSetShaderResources(0, 1, &multisampledView);
            context->CSSetUnorderedAccessViews(0, 1, &resolvedUav, nullptr);
            context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            ID3D11ShaderResourceView *emptyView = nullptr;
            ID3D11UnorderedAccessView *emptyUav = nullptr;
            context->CSSetShaderResources(0, 1, &emptyView);
            context->CSSetUnorderedAccessViews(0, 1, &emptyUav, nullptr);
            context->CSSetShader(nullptr, nullptr, 0);
        } else
            context->CopyResource(currentDepth, source);
        release(source);
        memcpy(currentMatrix, matrix, 64);
        currentJitter[0] = parameters.resolve[2];
        currentJitter[1] = parameters.resolve[3];
        if (!valid) {
            memcpy(previousMatrix, matrix, 64);
            memcpy(previousJitter, currentJitter, sizeof(previousJitter));
            context->CopyResource(previousDepth, currentDepth);
        }
        memcpy(parameters.previous, previousMatrix, 64);
        memcpy(parameters.previousJitter, previousJitter, sizeof(previousJitter));
        parameters.options[1] = 1;
        statusText = multisampled ? "active with GPU multisample depth resolve" : "active";
        return true;
    }
    void bind(ID3D11DeviceContext *context) {
        ID3D11ShaderResourceView *views[] = {currentView, previousView};
        context->PSSetShaderResources(2, 2, views);
    }
    void commit(ID3D11DeviceContext *context) {
        if (!currentDepth)
            return;
        context->CopyResource(previousDepth, currentDepth);
        memcpy(previousMatrix, currentMatrix, 64);
        memcpy(previousJitter, currentJitter, sizeof(previousJitter));
        valid = true;
    }
};
} // namespace reprojection
