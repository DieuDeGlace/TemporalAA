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
    taa::blurStrength = 0;
    float invertible[16] = {2, 0, 0, 3, 0, 4, 0, -2, 0, 0, .5f, 7, 0, 0, 0, 1};
    float inverted[16], identity[16];
    assert(reprojection::inverse(invertible, inverted));
    reprojection::multiply(invertible, inverted, identity);
    for (int i = 0; i < 16; ++i)
        assert(fabsf(identity[i] - (i % 5 == 0 ? 1.f : 0.f)) < .00001f);
    float singular[16]{};
    assert(!reprojection::inverse(singular, inverted));
    taa::oldUpdate = (taa::Update)(*(void ***)c)[48];
    float projection[16]{};
    projection[0] = projection[5] = projection[10] = projection[15] = 1;
    for (unsigned i = 0; i < 1000; ++i)
        taa::uploadProjection(c, projection);
    assert(taa::projectionUploads == 1);
    projection[2] = .001f;
    taa::uploadProjection(c, projection);
    assert(taa::projectionUploads == 2);
    // Read back the actual buffer to check a changed projection was uploaded.
    ID3D11Buffer *projectionReadback = nullptr;
    D3D11_BUFFER_DESC projectionDesc{};
    projectionDesc.ByteWidth = 64;
    projectionDesc.Usage = D3D11_USAGE_STAGING;
    projectionDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    assert(SUCCEEDED(d->CreateBuffer(&projectionDesc, nullptr, &projectionReadback)));
    c->CopyResource(projectionReadback, taa::jitterBuffer);
    D3D11_MAPPED_SUBRESOURCE projectionMap{};
    assert(SUCCEEDED(c->Map(projectionReadback, 0, D3D11_MAP_READ, 0, &projectionMap)));
    assert(memcmp(projectionMap.pData, projection, 64) == 0);
    c->Unmap(projectionReadback, 0);
    taa::drop(projectionReadback);
    // Both preparation paths must upload the same jittered projection and
    // return the original bound buffer for restoration.
    ID3D11Buffer *nativeBuffers[6]{};
    const UINT sizes[] = {64, 64, 64, 16, 320, 48};
    float cameraProjection[16]{};
    cameraProjection[0] = cameraProjection[5] = cameraProjection[10] = 1;
    cameraProjection[14] = 1;
    for (unsigned i = 0; i < 6; ++i) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizes[i];
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        assert(SUCCEEDED(d->CreateBuffer(&desc, nullptr, &nativeBuffers[i])));
    }
    nativeBuffers[0]->SetPrivateData(taa::cacheGuid, 64, cameraProjection);
    D3D11_VIEWPORT jitterViewport{0, 0, 1280, 720, 0, 1};
    c->RSSetViewports(1, &jitterViewport);
    taa::jitterX = .25f;
    taa::jitterY = -.25f;
    c->VSSetConstantBuffers(0, 6, nativeBuffers);
    taa::preparedDraw.valid = false;
    auto original = taa::jitter(c);
    assert(original == nativeBuffers[0]);
    float fallbackProjection[16];
    memcpy(fallbackProjection, taa::uploadedProjection, 64);
    assert(fallbackProjection[2] != 0 && fallbackProjection[6] != 0);
    taa::drop(original);
    c->VSSetConstantBuffers(0, 6, nativeBuffers);
    taa::preparedDraw.valid = true;
    memcpy(taa::preparedDraw.projection, cameraProjection, 64);
    taa::preparedDraw.viewport = jitterViewport;
    original = taa::jitter(c);
    assert(original == nativeBuffers[0]);
    assert(memcmp(fallbackProjection, taa::uploadedProjection, 64) == 0);
    taa::drop(original);
    taa::preparedDraw.valid = false;
    ID3D11Buffer *emptyBuffers[6]{};
    c->VSSetConstantBuffers(0, 6, emptyBuffers);
    for (auto &buffer : nativeBuffers)
        taa::drop(buffer);

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
        ID3D11Texture2D *depthTexture = nullptr;
        ID3D11DepthStencilView *depthView = nullptr;
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = size.first;
        depthDesc.Height = size.second;
        depthDesc.MipLevels = depthDesc.ArraySize = depthDesc.SampleDesc.Count = 1;
        depthDesc.Format = size.first == 1280   ? DXGI_FORMAT_D24_UNORM_S8_UINT
                           : size.first == 1920 ? DXGI_FORMAT_D32_FLOAT
                                                : DXGI_FORMAT_D16_UNORM;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        assert(SUCCEEDED(d->CreateTexture2D(&depthDesc, nullptr, &depthTexture)));
        assert(SUCCEEDED(d->CreateDepthStencilView(depthTexture, nullptr, &depthView)));
        c->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, .5f, 0);
        auto resolve = [&](int low, int high, bool reproject = false) {
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
            taa::sceneCameraValid = reproject;
            taa::drop(taa::sceneDepth);
            if (reproject) {
                taa::sceneDepth = depthView;
                depthView->AddRef();
            }
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
        taa::historyValid = false;
        taa::blurStrength = .12f;
        int softened = resolve(70, 185);
        assert(softened > 70 && softened < 85);
        taa::historyValid = false;
        assert(resolve(140, 140) == 140);
        taa::blurStrength = 0;
        // Actual depth-backed resolve: camera translation changes the history
        // coordinate; a newly exposed depth must reject that old surface.
        memset(taa::sceneView, 0, sizeof(taa::sceneView));
        memset(taa::sceneProjection, 0, sizeof(taa::sceneProjection));
        for (int i : {0, 5, 10, 15})
            taa::sceneView[i] = taa::sceneProjection[i] = 1;
        taa::historyValid = false;
        assert(resolve(70, 90, true) == 70);
        taa::sceneView[3] = 2.f / size.first;
        int translated = resolve(70, 90, true);
        assert(translated > 75 && translated < 90);
        c->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, .1f, 0);
        assert(resolve(70, 90, true) == 70);
        taa::sceneCameraValid = false;
        taa::drop(taa::sceneDepth);
        taa::drop(depthView);
        taa::drop(depthTexture);
        printf("Depth-backed camera reprojection and disocclusion: %ux%u PASS\n", size.first,
               size.second);
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
