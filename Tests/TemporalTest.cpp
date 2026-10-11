#include "../TemporalAA.cpp"
#include <cassert>
#include <vector>
int main() {
    ID3D11Device *d = nullptr;
    ID3D11DeviceContext *c = nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &d, nullptr, &c)));
    taa::oldD = (taa::D)(*(void ***)c)[13];
    assert(taa::initialize(c));
    for (auto size : {std::pair<UINT, UINT>{1280, 720}, {1920, 1080}, {512, 512}}) {
        ID3D11Texture2D *t = nullptr, *staging = nullptr;
        ID3D11RenderTargetView *rt = nullptr;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = size.first;
        td.Height = size.second;
        td.MipLevels = td.ArraySize = 1;
        td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        assert(SUCCEEDED(d->CreateTexture2D(&td, nullptr, &t)));
        assert(SUCCEEDED(d->CreateRenderTargetView(t, nullptr, &rt)));
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        assert(SUCCEEDED(d->CreateTexture2D(&td, nullptr, &staging)));
        auto resolve = [&](int low, int high) {
            std::vector<unsigned char> p(size.first * size.second * 4);
            for (UINT y = 0; y < size.second; y++)
                for (UINT x = 0; x < size.first; x++) {
                    int value = (x + y) % 2 ? high : low;
                    auto i = (y * size.first + x) * 4;
                    p[i] = p[i + 1] = p[i + 2] = (unsigned char)value;
                    p[i + 3] = 255;
                }
            c->UpdateSubresource(t, 0, nullptr, p.data(), size.first * 4, 0);
            c->OMSetRenderTargets(1, &rt, nullptr);
            taa::target = rt;
            rt->AddRef();
            taa::viewport = {0, 0, (float)size.first, (float)size.second, 0, 1};
            taa::finished = false;
            taa::jitterX = taa::jitterY = 0;
            assert(taa::apply());
            ID3D11RenderTargetView *restored = nullptr;
            c->OMGetRenderTargets(1, &restored, nullptr);
            assert(restored == rt);
            taa::drop(restored);
            c->CopyResource(staging, t);
            D3D11_MAPPED_SUBRESOURCE map{};
            assert(SUCCEEDED(c->Map(staging, 0, D3D11_MAP_READ, 0, &map)));
            int result = ((unsigned char *)map.pData)[2 * map.RowPitch + 2 * 4];
            c->Unmap(staging, 0);
            return result;
        };
        taa::historyValid = false;
        assert(resolve(70, 185) == 70);
        int accumulated = resolve(51, 204);
        assert(accumulated < 70 && accumulated >= 63);
        // A newly bright pixel must reject its dark stale history immediately.
        assert(resolve(240, 240) >= 239);
        ID3DDeviceContextState *saved = nullptr;
        taa::context1->SwapDeviceContextState(taa::state, &saved);
        ID3D11RenderTargetView *parked = nullptr;
        c->OMGetRenderTargets(1, &parked, nullptr);
        assert(!parked);
        ID3D11ShaderResourceView *views[2]{};
        c->PSGetShaderResources(0, 2, views);
        assert(!views[0] && !views[1]);
        taa::context1->SwapDeviceContextState(saved, nullptr);
        taa::drop(saved);
        c->OMSetRenderTargets(0, nullptr, nullptr);
        taa::drop(rt);
        taa::drop(t);
        taa::drop(staging);
        printf("History accumulation, rejection and resize/state: %ux%u PASS\n", size.first,
               size.second);
    }
    for (UINT i = 1; i <= 8; i++) {
        assert(taa::halton(i, 2) >= 0 && taa::halton(i, 2) < 1);
        assert(taa::halton(i, 3) >= 0 && taa::halton(i, 3) < 1);
    }
    taa::drop(c);
    taa::drop(d);
}
