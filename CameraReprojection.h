#pragma once
#include <d3d11.h>
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
    UINT width = 0, height = 0;
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
        release(currentView);
        release(previousView);
        release(currentDepth);
        release(previousDepth);
        width = height = 0;
        family = DXGI_FORMAT_UNKNOWN;
        valid = false;
    }

  public:
    ~CameraHistory() {
        clear();
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
            desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1) {
            statusText = typed == DXGI_FORMAT_UNKNOWN ? "unsupported depth format"
                         : desc.SampleDesc.Count != 1 ? "multisampled depth"
                                                      : "depth dimensions/layout differ from scene";
            release(source);
            invalidate();
            return false;
        }
        if (!currentDepth || width != w || height != h || family != typeless) {
            clear();
            desc.Format = typeless;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = desc.MiscFlags = 0;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = typed;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            bool ok =
                SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &currentDepth)) &&
                SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &previousDepth)) &&
                SUCCEEDED(device->CreateShaderResourceView(currentDepth, &view, &currentView)) &&
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
        }
        // Copies stay on the GPU. No staging resources or CPU depth readback.
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
        statusText = "active";
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
